#include "server_handlers.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>



void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}



void broadcast_snapshot(void) {
    GameSnapshot snap;
    snap.sequence     = global_sequence++;
    snap.timestamp    = get_time_ms();
    snap.player_count = (uint8_t)world.player_count;

    memcpy(snap.players, world.players, sizeof(world.players));
    memcpy(snap.coins,   world.coins,   sizeof(world.coins));

    for (int i = 0; i < MAX_PLAYERS; i++)
        snap.last_processed_input[i] = last_processed_input[i];

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!world.players[i].active || !client_ssl[i]) continue;
        int r = secure_send(client_ssl[i], &snap, (int)sizeof(snap));
        if (r > 0) packets_sent++;
    }
}


void recv_from_player(int id) {
    char    buf[BUFFER_SIZE];
    int     len = secure_recv(client_ssl[id], buf, sizeof(buf));
    if (len <= 0) return;

    packets_received++;
    uint8_t type = (uint8_t)buf[0];

    switch (type) {

        case MSG_INPUT: {
            if ((size_t)len < sizeof(InputCommand) + 1) break;
            InputCommand *cmd = (InputCommand *)(buf + 1);

            if (cmd->player_id != (uint8_t)id) break;
            if (!world.players[id].active)      break;

            push_input(&world.input_queues[id], *cmd);

            if (cmd->sequence > last_processed_input[id])
                last_processed_input[id] = cmd->sequence;
            break;
        }

        case MSG_LEAVE: {
            printf("[LEAVE] Player %d left (total: %d)\n",
                   id, world.player_count - 1);
            remove_player(&world, id);
            dtls_session_free(client_ssl[id]);
            client_ssl[id] = NULL;
            close(dedicated_sock[id]);
            dedicated_sock[id] = -1;
            break;
        }

        case MSG_PING: {
            uint8_t pong = MSG_PONG;
            int r = secure_send(client_ssl[id], &pong, 1);
            if (r > 0) packets_sent++;
            break;
        }

        default:
            break;
    }
}


void handle_new_join(struct sockaddr_in *peer) {
    int id = add_player(&world, peer);
    if (id < 0) {
        printf("[JOIN] Server full — rejecting %s\n", inet_ntoa(peer->sin_addr));
        return;
    }

    int dsock = socket(AF_INET, SOCK_DGRAM, 0);
    if (dsock < 0) {
        perror("dedicated socket");
        remove_player(&world, id);
        return;
    }

    int opt = 1;
    setsockopt(dsock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(dsock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(dsock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("dedicated bind");
        remove_player(&world, id);
        close(dsock);
        return;
    }

    if (connect(dsock, (struct sockaddr *)peer, sizeof(*peer)) < 0) {
        perror("dedicated connect");
        remove_player(&world, id);
        close(dsock);
        return;
    }

    uint8_t resp[2] = {MSG_JOIN, (uint8_t)id};
    sendto(server_sock, resp, 2, 0,
           (struct sockaddr *)peer, sizeof(*peer));

    printf("[JOIN] Player %d @ %s — starting DTLS handshake...\n",
           id, inet_ntoa(peer->sin_addr));

    SSL *ssl = dtls_server_session(dtls_ctx, dsock, peer);
    if (!ssl) {
        printf("[DTLS] Handshake failed for player %d — removing slot\n", id);
        remove_player(&world, id);
        close(dsock);
        return;
    }

    set_nonblocking(dsock);

    dedicated_sock[id] = dsock;
    client_ssl[id]     = ssl;
    printf("[JOIN] Player %d joined and secured — total: %d\n",
           id, world.player_count);
}
