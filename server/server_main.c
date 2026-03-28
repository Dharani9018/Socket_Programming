#include "server_handlers.h"
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


GameWorld  world;
int        server_sock              = -1;
int        dedicated_sock[MAX_PLAYERS];
SSL       *client_ssl[MAX_PLAYERS];
SSL_CTX   *dtls_ctx                 = NULL;
int        running                  = 1;
uint32_t   last_processed_input[MAX_PLAYERS];
uint32_t   global_sequence          = 0;
uint32_t   packets_received         = 0;
uint32_t   packets_sent             = 0;

#define CERT_FILE "cert.pem"
#define KEY_FILE  "key.pem"

static void handle_signal(int sig) { (void)sig; running = 0; }

int main(void) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);


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


    server_sock = init_server_socket();
    if (server_sock < 0) return 1;


    for (int i = 0; i < MAX_PLAYERS; i++) {
        client_ssl[i]           = NULL;
        dedicated_sock[i]       = -1;
        last_processed_input[i] = 0;
    }

    init_game_world(&world);

    uint32_t last_tick_time  = get_time_ms();
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

    while (running) {
        uint32_t now = get_time_ms();


        int len;
        while ((len = receive_packet(server_sock, buffer,
                                     sizeof(buffer), &client_addr)) > 0) {
            if ((uint8_t)buffer[0] != MSG_JOIN) continue;


            int known = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (world.players[i].active &&
                    world.client_addrs[i].sin_addr.s_addr ==
                        client_addr.sin_addr.s_addr &&
                    world.client_addrs[i].sin_port == client_addr.sin_port) {
                    known = 1; break;
                }
            }
            if (!known) handle_new_join(&client_addr);
        }


        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (world.players[i].active && client_ssl[i])
                recv_from_player(i);
        }


        if (now - last_tick_time >= (1000u / TICK_RATE)) {
            simulate_fixed_tick(&world);
            last_tick_time = now;
        }


        if (now - last_broadcast >= (1000u / NETWORK_SEND_RATE)) {
            if (world.player_count > 0)
                broadcast_snapshot();
            last_broadcast = now;
        }


        uint32_t now_sec = (uint32_t)time(NULL);
        if (now_sec != last_stats_time) {
            printf("[STATS] RX: %4u | TX: %4u | Players: %d | GlobalSeq: %u\n",
                   packets_received, packets_sent,
                   world.player_count, global_sequence);
            packets_received = 0;
            packets_sent     = 0;
            last_stats_time  = now_sec;
        }

        usleep(1000);  
    }


    printf("\n[SERVER] Shutting down...\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (client_ssl[i])        dtls_session_free(client_ssl[i]);
        if (dedicated_sock[i] >= 0) close(dedicated_sock[i]);
    }
    close(server_sock);
    dtls_ctx_free(dtls_ctx);
    return 0;
}
