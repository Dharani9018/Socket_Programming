#include "server_handlers.h"
#include <unistd.h> //close() function.
#include <fcntl.h> //fnctl() for non blocking mode.
#include <stdio.h> 
#include <string.h>//memcpy()
#include <arpa/inet.h>//inet_ntoa() -> for IP Address formatting.
#include <sys/socket.h> //socket functions.



void set_nonblocking(int fd) 
{
    int flags = fcntl(fd, F_GETFL, 0); //get the current flag.
    fcntl(fd, F_SETFL, flags | O_NONBLOCK); //Make it non blocking.
}


//Send gameState to all players.
//Called at 15Hz to update all clients with current game state.
void broadcast_snapshot(void)
{
    GameSnapshot snap; //Create a snapshot structure.
    snap.sequence     = global_sequence++; //INcrement sequence number.
    snap.timestamp    = get_time_ms(); //Add time stamp.
    snap.player_count = (uint8_t)world.player_count; //no. of players active.

    memcpy(snap.players, world.players, sizeof(world.players)); //copy currrent game state into snapshot.
    memcpy(snap.coins,   world.coins,   sizeof(world.coins)); 

    //copy last processed input sequences for client reconcialliation.
    for (int i = 0; i < MAX_PLAYERS; i++)
        snap.last_processed_input[i] = last_processed_input[i];

    //Send the snapshot to each players.
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!world.players[i].active || !client_ssl[i]) continue;
        int r = secure_send(client_ssl[i], &snap, (int)sizeof(snap));
        if (r > 0) packets_sent++;
    }
}

//Process incoming messages from a specific player.
void recv_from_player(int id) 
{
    char    buf[BUFFER_SIZE]; //Buffer for incoming data.
    int     len = secure_recv(client_ssl[id], buf, sizeof(buf)); //receive encrypted data.
    if (len <= 0) return;

    packets_received++; //Increment packet counter.
    uint8_t type = (uint8_t)buf[0]; //Firrst byte is msg type.

    switch (type) 
    {

        case MSG_INPUT: 
            { //player movement input.
            if ((size_t)len < sizeof(InputCommand) + 1) break;
            InputCommand *cmd = (InputCommand *)(buf + 1);

            //input must match player id.
            if (cmd->player_id != (uint8_t)id) break;
            if (!world.players[id].active)      break;

            push_input(&world.input_queues[id], *cmd); //Store the input in queue for next simulation tick.
            if (cmd->sequence > last_processed_input[id]) //Track the highest input sequence reived for the client reconcialliation.
                last_processed_input[id] = cmd->sequence;
            break;
        }

        case MSG_LEAVE: { //player disconnecting.
            printf("[LEAVE] Player %d left (total: %d)\n",
                   id, world.player_count - 1);
            remove_player(&world, id); //remove from the game world.
            dtls_session_free(client_ssl[id]); //close encrypted session.
            client_ssl[id] = NULL; 
            close(dedicated_sock[id]); //Close UDP socket.
            dedicated_sock[id] = -1; 
            break;
        }

        case MSG_PING: { //network latency check
            uint8_t pong = MSG_PONG; //Create a pong response
            int r = secure_send(client_ssl[id], &pong, 1); //send it back.
            if (r > 0) packets_sent++;
            break;
        }

        default:
            break;
    }
}

//Accepting and setting up new players.
void handle_new_join(struct sockaddr_in *peer) 
{
    int id = add_player(&world, peer); //add and get the player id.
    if (id < 0) {
        printf("[JOIN] Server full - rejecting %s\n", inet_ntoa(peer->sin_addr));
        return;
    }

    int dsock = socket(AF_INET, SOCK_DGRAM, 0);//Create a socket.
    if (dsock < 0) 
    {
        perror("dedicated socket");
        remove_player(&world, id);
        return;
    }

    int opt = 1; //allow socket to reuse.
    setsockopt(dsock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(dsock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(dsock, (struct sockaddr *)&local, sizeof(local)) < 0)//bind the port and ip address to the socket. 
    {
        perror("dedicated bind");
        remove_player(&world, id);
        close(dsock);
        return;
    }

    //Connect sockett to this specific client.
    if (connect(dsock, (struct sockaddr *)peer, sizeof(*peer)) < 0)
    {
        perror("dedicated connect");
        remove_player(&world, id);
        close(dsock);
        return;
    }
    //Send join response with player id.
    uint8_t resp[2] = {MSG_JOIN, (uint8_t)id};
    sendto(server_sock, resp, 2, 0,
           (struct sockaddr *)peer, sizeof(*peer));

    printf("[JOIN] Player %d @ %s - starting DTLS handshake...\n",
           id, inet_ntoa(peer->sin_addr)); 

    //Perform DTLS server handshake.
    SSL *ssl = dtls_server_session(dtls_ctx, dsock, peer);
    if (!ssl)
    {
        printf("[DTLS] Handshake failed for player %d — removing slot\n", id);
        remove_player(&world, id);
        close(dsock);
        return;
    }

    set_nonblocking(dsock); //make the socket non-blocking.

    dedicated_sock[id] = dsock; //store socket for this player.
    client_ssl[id]     = ssl; //Store ssl session for this player.
    printf("[JOIN] Player %d joined and secured — total: %d\n",
           id, world.player_count);
}
