#include "server_handlers.h"
#include <unistd.h> //usleep() , close().
#include <signal.h> //signal for handling ctrl+c.
#include <stdio.h>
#include <string.h> //memset()
#include <time.h> //time().


GameWorld  world; //Comple game state.
int        server_sock              = -1; //main server socket (listens for new players).
int        dedicated_sock[MAX_PLAYERS]; //One upd socket per connected player.
SSL       *client_ssl[MAX_PLAYERS]; //one ssl session perplayer.
SSL_CTX   *dtls_ctx                 = NULL; //dtls context.(certificates, settings).
int        running                  = 1; //Server loop control flag.
uint32_t   last_processed_input[MAX_PLAYERS]; //Highes input sequnce processed per player.
uint32_t   global_sequence          = 0; //snapshot counter: increases each broadcast. 
uint32_t   packets_received         = 0; //total number of packets received.
uint32_t   packets_sent             = 0; //total number of packets sent.

#define CERT_FILE "cert.pem"
#define KEY_FILE  "key.pem"

static void handle_signal(int sig) { (void)sig; running = 0; } //ctrl C results in gracefull shutdown.

int main(void) 
{
    //1. signal setup
    signal(SIGINT,  handle_signal); //handle ctrl C
    signal(SIGTERM, handle_signal);//Termination signal.


    //2. DTLS setup (Encryption).
    //Loads the SSL certificate and the private key.
    //if the files doesnt exist, shows the command to generate them.
    dtls_ctx = dtls_server_ctx(CERT_FILE, KEY_FILE);
    if (!dtls_ctx) {
        fprintf(stderr,
            "ERROR: Could not load %s / %s\n"
            "Generate with:\n"
            "  openssl req -x509 -newkey rsa:2048 -keyout key.pem "
            "-out cert.pem -days 365 -nodes -subj '/CN=cat-arena'\n",
            CERT_FILE, KEY_FILE);
        return 1;
    }

    //3. Create server socket.
    //Create UDP socket bound to port
    //Makes it non-blocking.
    server_sock = init_server_socket();
    if (server_sock < 0) return 1;


    //Initialize arrays
    for (int i = 0; i < MAX_PLAYERS; i++) 
    {
        client_ssl[i]           = NULL;
        dedicated_sock[i]       = -1;
        last_processed_input[i] = 0;
    }

    //initilize game world.
    //Creates the empty player array
    //spawns coins randomly on grid.
    init_game_world(&world);

    uint32_t last_tick_time  = get_time_ms(); //last simulation tick.
    uint32_t last_broadcast  = get_time_ms();
    uint32_t last_stats_time = (uint32_t)time(NULL);

    printf("=== CAT ARENA SERVER ===\n");
    printf("Transport : DTLS 1.2 over UDP (OpenSSL)\n");
    printf("Port      : %d\n", PORT);
    printf("Sim rate  : %d Hz\n", TICK_RATE);
    printf("Net rate  : %d Hz\n", NETWORK_SEND_RATE);
    printf("Loss sim  : 20%% on plain UDP (JOIN only)\n");
    printf("Waiting for players...\n\n");

    struct sockaddr_in client_addr;
    char               buffer[BUFFER_SIZE];

    //loop control.
    while (running) 
    {
        uint32_t now = get_time_ms(); //current time.


        int len;
        while ((len = receive_packet(server_sock, buffer,
                                     sizeof(buffer), &client_addr)) > 0) {
            if ((uint8_t)buffer[0] != MSG_JOIN) continue; //only accept join messages on the main socket.


            //1. Accept new players.
            //Check if the player already exists.
            int known = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (world.players[i].active &&
                    world.client_addrs[i].sin_addr.s_addr ==
                        client_addr.sin_addr.s_addr &&
                    world.client_addrs[i].sin_port == client_addr.sin_port) {
                    known = 1; break;
                }
            }
            if (!known) handle_new_join(&client_addr); //if new player handle new join.
        }

        //2. process incoming data from existing player.
        for (int i = 0; i < MAX_PLAYERS; i++) 
        {
            if (world.players[i].active && client_ssl[i])
                recv_from_player(i); //Check if player sent any data.
        }

        //3. Game simulation (30) Hz
        //Processes all queued player inputs
        //Moves players, checks collisions, updates scores.
        if (now - last_tick_time >= (1000u / TICK_RATE))  //1000ms / 30 = 33.33ms between ticks
        {
            simulate_fixed_tick(&world); //process all queued inputs.
            last_tick_time = now;
        }


        //4. Network broadcast:
        //only send if atleast 1 player is connected.
        if (now - last_broadcast >= (1000u / NETWORK_SEND_RATE)) 
        {
            if (world.player_count > 0)
                broadcast_snapshot(); //send game state to all clients.
            last_broadcast = now;
        }

        //Statistics o/p: every second.
        uint32_t now_sec = (uint32_t)time(NULL);
        if (now_sec != last_stats_time) 
        {
            printf("[STATS] Packets received: %4u | Packets sent: %4u | Players: %d | GlobalSeq: %u\n",
                   packets_received, packets_sent,
                   world.player_count, global_sequence);
            //reset counters for next second.
            packets_received = 0;
            packets_sent     = 0;
            last_stats_time  = now_sec;
        }


        usleep(1000);  //sleep for 1ms to avoid consuming of the cpu, gives time to other processes.
    }


    printf("\n[SERVER] Shutting down...\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (client_ssl[i])        dtls_session_free(client_ssl[i]);
        if (dedicated_sock[i] >= 0) close(dedicated_sock[i]); //close encrypted sessions
    }
    close(server_sock);
    dtls_ctx_free(dtls_ctx); //free ssl socket.
    return 0;
}
