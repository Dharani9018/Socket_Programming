#include "client_render.h"
#include "../shared/game.h"
#include "../shared/network.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>


static int connect_plain(int sock, struct sockaddr_in *srv) {
    uint8_t join = MSG_JOIN;
    sendto(sock, &join, 1, 0, (struct sockaddr *)srv, sizeof(*srv));

    char     buf[8];
    uint32_t start = get_time_ms();
    while (get_time_ms() - start < 5000) {
        struct sockaddr_in tmp; socklen_t tl = sizeof(tmp);
        int n = (int)recvfrom(sock, buf, sizeof(buf), 0,
                              (struct sockaddr *)&tmp, &tl);
        if (n >= 2 && (uint8_t)buf[0] == MSG_JOIN)
            return (uint8_t)buf[1];
        usleep(5000);
    }
    return -1;
}


static SSL *connect_dtls(int sock, struct sockaddr_in *srv,
                          SSL_CTX **ctx_out) {
    // Make blocking for handshake
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);

    SSL_CTX *ctx = dtls_client_ctx();
    if (!ctx) return NULL;

    SSL *ssl = dtls_client_session(ctx, sock, srv);
    if (!ssl) { dtls_ctx_free(ctx); return NULL; }


    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    *ctx_out = ctx;
    return ssl;
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];


    const int SW = GRID_WIDTH  * CELL_SIZE;
    const int SH = GRID_HEIGHT * CELL_SIZE;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SW, SH, "Cat Arena [DTLS]");
    SetTargetFPS(60);


    int sock = init_client_socket();   // non-blocking
    if (sock < 0) { CloseWindow(); return 1; }

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(PORT);
    inet_pton(AF_INET, server_ip, &srv.sin_addr);


    int player_id = connect_plain(sock, &srv);
    if (player_id < 0) {
        printf("ERROR: No JOIN response from server\n");
        close(sock); CloseWindow(); return 1;
    }
    printf("Assigned player ID: %d — upgrading to DTLS...\n", player_id);


    if (connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("connect"); close(sock); CloseWindow(); return 1;
    }

    SSL_CTX *dtls_ctx = NULL;
    SSL     *ssl      = connect_dtls(sock, &srv, &dtls_ctx);
    if (!ssl) {
        printf("ERROR: DTLS handshake failed\n");
        close(sock); CloseWindow(); return 1;
    }
    printf("Secured as Player %d — cipher: %s\n",
           player_id, SSL_get_cipher(ssl));


    GameWorld         world;
    ClientInputBuffer ibuf;
    NetworkStats      stats;
    init_game_world(&world);
    init_client_buffer(&ibuf);
    memset(&stats, 0, sizeof(stats));

    // Timing
    uint32_t last_ping       = 0;
    uint32_t ping_sent_at    = 0;
    uint32_t input_seq       = 0;
    uint32_t expected_seq    = 0;
    uint32_t pkts_received   = 0;
    uint32_t pkts_lost       = 0;
    int      has_snapshot    = 0;
    uint32_t last_input_time = 0;


    const uint32_t INPUT_INTERVAL = 1000u / TICK_RATE;


    uint32_t bw_bytes = 0;
    uint32_t bw_start = get_time_ms();


    float last_latency = -1.0f;

    char recv_buf[BUFFER_SIZE];


    while (!WindowShouldClose()) {
        uint32_t now = get_time_ms();


        if (now - last_ping >= 1000u) {
            uint8_t ping = MSG_PING;
            secure_send(ssl, &ping, 1);
            ping_sent_at = now;
            last_ping    = now;
        }


        int len;
        while ((len = secure_recv(ssl, recv_buf, sizeof(recv_buf))) > 0) {
            bw_bytes += (uint32_t)len;

            if ((uint8_t)recv_buf[0] == MSG_PONG) {

                uint32_t rtt = get_time_ms() - ping_sent_at;
                stats.rtt_ms = (stats.rtt_ms == 0)
                    ? rtt
                    : (uint32_t)(stats.rtt_ms * 0.875f + rtt * 0.125f);

            } else if ((size_t)len >= sizeof(GameSnapshot)) {
                GameSnapshot *snap = (GameSnapshot *)recv_buf;


                if (expected_seq > 0 && snap->sequence > expected_seq)
                    pkts_lost += snap->sequence - expected_seq;
                expected_seq = snap->sequence + 1;
                pkts_received++;

                uint32_t total = pkts_received + pkts_lost;
                if (total > 0)
                    stats.packet_loss_rate =
                        (float)pkts_lost / (float)total * 100.0f;


                uint32_t recv_now = get_time_ms();
                float latency = (float)(recv_now - snap->timestamp);
                if (latency > 2000.0f) latency = 2000.0f;  // clamp

                if (stats.avg_latency_ms == 0.0f) {
                    stats.avg_latency_ms = latency;
                } else {

                    if (last_latency >= 0.0f) {
                        float diff = latency - last_latency;
                        if (diff < 0.0f) diff = -diff;
                        stats.jitter_ms =
                            stats.jitter_ms * 0.9f + diff * 0.1f;
                    }
                    stats.avg_latency_ms =
                        stats.avg_latency_ms * 0.9f + latency * 0.1f;
                }
                last_latency = latency;


                memcpy(world.players, snap->players, sizeof(world.players));
                memcpy(world.coins,   snap->coins,   sizeof(world.coins));
                world.player_count = snap->player_count;

                uint32_t ack     = snap->last_processed_input[player_id];
                int      new_cnt = 0;
                for (int i = 0; i < ibuf.count; i++)
                    if (ibuf.inputs[i].sequence > ack)
                        ibuf.inputs[new_cnt++] = ibuf.inputs[i];
                ibuf.count = new_cnt;

                uint32_t just_applied = (input_seq > 0) ? input_seq - 1
                                                         : UINT32_MAX;
                for (int i = 0; i < ibuf.count; i++) {
                    if (ibuf.inputs[i].sequence == just_applied) continue;
                    update_player(&world, player_id,
                                  ibuf.inputs[i].direction);
                }

                has_snapshot = 1;
            }
        }


        uint32_t bw_elapsed = now - bw_start;
        if (bw_elapsed >= 1000u) {

            stats.bandwidth_kbps = (float)bw_bytes / (float)bw_elapsed;
            bw_bytes = 0;
            bw_start = now;   
        }


        int dir = 0;
        if      (IsKeyDown(KEY_W)) dir = 1;
        else if (IsKeyDown(KEY_S)) dir = 2;
        else if (IsKeyDown(KEY_A)) dir = 3;
        else if (IsKeyDown(KEY_D)) dir = 4;
        if (IsKeyPressed(KEY_ESCAPE)) break;

        if (dir > 0 && (now - last_input_time) >= INPUT_INTERVAL) {
            last_input_time = now;

            InputCommand cmd;
            cmd.player_id    = (uint8_t)player_id;
            cmd.direction    = (uint8_t)dir;
            cmd.sequence     = input_seq++;
            cmd.timestamp_ms = now;


            update_player(&world, player_id, cmd.direction);


            if (ibuf.count < MAX_PENDING_INPUTS)
                ibuf.inputs[ibuf.count++] = cmd;


            uint8_t pkt[sizeof(cmd) + 1];
            pkt[0] = MSG_INPUT;
            memcpy(pkt + 1, &cmd, sizeof(cmd));
            secure_send(ssl, pkt, (int)sizeof(pkt));
        }


        render_frame(&world, player_id, &stats,
                     SW, SH, has_snapshot, server_ip);
    }


    uint8_t leave[2] = {MSG_LEAVE, (uint8_t)player_id};
    secure_send(ssl, leave, 2);
    dtls_session_free(ssl);
    dtls_ctx_free(dtls_ctx);
    close(sock);
    CloseWindow();

    print_network_analysis(stats, (int)world.players[player_id].score);
    return 0;
}
