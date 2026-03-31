#include "client_render.h"
#include "../shared/game.h"
#include "../shared/network.h"
#include <arpa/inet.h> //
#include <unistd.h>//close(), usleep()
#include <stdio.h>
#include <string.h> //memcpy()
#include <fcntl.h>//socket control.


//initial connection to the server: no encryption.
//sends initial join request with player ID.
//Waits for the server to respond with player ID.
//This happens before encryption is established
//Return player id or -1 on failure.
static int connect_plain(int sock, struct sockaddr_in *srv) 
{
    uint8_t join = MSG_JOIN;
    sendto(sock, &join, 1, 0, (struct sockaddr *)srv, sizeof(*srv)); //send join request.

    char     buf[8];
    uint32_t start = get_time_ms();
    //wait 5seconds for the response.
    while (get_time_ms() - start < 5000) {
        struct sockaddr_in tmp; socklen_t tl = sizeof(tmp);
        int n = (int)recvfrom(sock, buf, sizeof(buf), 0,
                              (struct sockaddr *)&tmp, &tl);
        //check if we got join response.
        if (n >= 2 && (uint8_t)buf[0] == MSG_JOIN)
            return (uint8_t)buf[1]; //return assigned player id.
        usleep(5000); //Wait 5ms before retry.
    }
    return -1; //Time out.
}

//Establish encrypted connection.
//upgrades connection from plain UDP to encrypted DTLS.
//Temporarily makes socket blocking because handshake requires reliable connection
//restores non blocking.
static SSL *connect_dtls(int sock, struct sockaddr_in *srv,
                          SSL_CTX **ctx_out) 
{
    // temporarily Make blocking for handshake.
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK); //remove non blocking flag.

    SSL_CTX *ctx = dtls_client_ctx(); //create DTLS client context.
    if (!ctx) return NULL;

    //perform DTLS handshake with server.
    SSL *ssl = dtls_client_session(ctx, sock, srv);
    if (!ssl) { dtls_ctx_free(ctx); return NULL; }


    fcntl(sock, F_SETFL, flags | O_NONBLOCK); //change it back to non-blocking

    *ctx_out = ctx;
    return ssl;
}



int main(int argc, char *argv[])
{
    if (argc != 2) 
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];


    //1. window setup
    const int SW = GRID_WIDTH  * CELL_SIZE;
    const int SH = GRID_HEIGHT * CELL_SIZE;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SW, SH, "CoinBox [DTLS]");
    SetTargetFPS(60); //60 frames per second.


    //2. socket setup:
    int sock = init_client_socket();   // non-blocking
    if (sock < 0) 
    { 
        CloseWindow(); 
        return 1; 
    }
    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(PORT);
    inet_pton(AF_INET, server_ip, &srv.sin_addr);

    //3. Plain connection: get playerid.
    int player_id = connect_plain(sock, &srv);
    if (player_id < 0) {
        printf("ERROR: No JOIN response from server\n");
        close(sock); CloseWindow(); return 1;
    }
    printf("Assigned player ID: %d — upgrading to DTLS...\n", player_id);


    if (connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("connect"); close(sock); CloseWindow(); return 1;
    }

    //4. DTLS UPgrade: establish encryption.
    SSL_CTX *dtls_ctx = NULL;
    SSL     *ssl      = connect_dtls(sock, &srv, &dtls_ctx);
    if (!ssl) {
        printf("ERROR: DTLS handshake failed\n");
        close(sock); CloseWindow(); return 1;
    }
    printf("Secured as Player %d — cipher: %s\n",
           player_id, SSL_get_cipher(ssl));


    //5. Client state initialization.
    GameWorld         world; //local copy of the game state.
    ClientInputBuffer ibuf; //Buffer for unacknowleged input.
    NetworkStats      stats; 
    init_game_world(&world);
    init_client_buffer(&ibuf);
    memset(&stats, 0, sizeof(stats));

    // Timing variables.
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


    while (!WindowShouldClose()) //until user closes the window - esc.
    {
        uint32_t now = get_time_ms(); //current time.


        //measuring rtt.
        if (now - last_ping >= 1000u) //Send ping every second.
        {
            uint8_t ping = MSG_PING;
            secure_send(ssl, &ping, 1);
            ping_sent_at = now;
            last_ping    = now;
        }
    

        int len;
        while ((len = secure_recv(ssl, recv_buf, sizeof(recv_buf))) > 0)  
        {
            bw_bytes += (uint32_t)len; //track bandwidth.

            if ((uint8_t)recv_buf[0] == MSG_PONG) //calculate latency.
            {

                uint32_t rtt = get_time_ms() - ping_sent_at;
                stats.rtt_ms = (stats.rtt_ms == 0)
                    ? rtt
                    : (uint32_t)(stats.rtt_ms * 0.875f + rtt * 0.125f); //EMA, for smoothing.

            } 
            else if ((size_t)len >= sizeof(GameSnapshot)) //check if received size is equal to gamesnpshot.
            {
                GameSnapshot *snap = (GameSnapshot *)recv_buf; //


                //Packet loss detection.
                if (expected_seq > 0 && snap->sequence > expected_seq)
                    pkts_lost += snap->sequence - expected_seq;
                expected_seq = snap->sequence + 1;
                pkts_received++;

                //Calculate packet loss percentage.
                uint32_t total = pkts_received + pkts_lost;
                if (total > 0)
                    stats.packet_loss_rate =
                        (float)pkts_lost / (float)total * 100.0f;


                //calculate latency.
                uint32_t recv_now = get_time_ms();
                float latency = (float)(recv_now - snap->timestamp);
                if (latency > 2000.0f) latency = 2000.0f;  // clamp

                //Update avg latency(EMA)
                if (stats.avg_latency_ms == 0.0f) 
                {
                    stats.avg_latency_ms = latency;
                } 
                else 
                {
                    //Calculate jitter: (variation in latency).
                    if (last_latency >= 0.0f) {
                        float diff = latency - last_latency;
                        if (diff < 0.0f) diff = -diff;
                        stats.jitter_ms =
                            stats.jitter_ms * 0.9f + diff * 0.1f; //EMA.
                    }
                    stats.avg_latency_ms =
                        stats.avg_latency_ms * 0.9f + latency * 0.1f;
                }
                last_latency = latency;

                //Update local game state with server snapshot:
                memcpy(world.players, snap->players, sizeof(world.players));
                memcpy(world.coins,   snap->coins,   sizeof(world.coins));
                world.player_count = snap->player_count;

                //Client Prediction reconcile with server.
                //1. SERVER acknowlegedes inputs.
                /*
                 * Before filtering:
                 ibuf.inputs = [seq 1, seq 2, seq 3, seq 4, seq 5]
                 ack = 3
                 After filtering: 
                ibuf.inputs = [seq 4, seq 5]  (only inputs server hasn't seen)
                ibuf.count = 2                 */

                uint32_t ack     = snap->last_processed_input[player_id]; //last processed input from the server.
                int      new_cnt = 0;
                for (int i = 0; i < ibuf.count; i++) //"2". remove acknowleged input from the buffer.
                    //This filters the input buffer to keep ONLY unacknowledged inputs.
                    if (ibuf.inputs[i].sequence > ack)
                        ibuf.inputs[new_cnt++] = ibuf.inputs[i];
                ibuf.count = new_cnt;

                //3. Determine which input was just applied.
                //input_seq = next sequence number to use(like a counter.)
                /*Ex: We've sent inputs with seq: 0, 1, 2, 3, 4
                input_seq = 5 (next one to use)
                just_applied = 4 (the most recent input we sent)*/
                uint32_t just_applied = (input_seq > 0) ? (input_seq - 1) : UINT32_MAX;
                //4. Re-apply Unacknowledged Inputs (Reconciliation)
                for (int i = 0; i < ibuf.count; i++) 
                {
                    if (ibuf.inputs[i].sequence == just_applied) continue;
                    update_player(&world, player_id,
                                  ibuf.inputs[i].direction);
                }

                has_snapshot = 1; //we have game data to render.
            }
        }

        //bandwidth calc:
        uint32_t bw_elapsed = now - bw_start; 
        if (bw_elapsed >= 1000u) //only calculate every 1 second.
        {

            stats.bandwidth_kbps = (float)bw_bytes / (float)bw_elapsed;
            bw_bytes = 0; //reset for next measurement.
            bw_start = now;   
        }


        int dir = 0;
        if      (IsKeyDown(KEY_W)) dir = 1;
        else if (IsKeyDown(KEY_S)) dir = 2;
        else if (IsKeyDown(KEY_A)) dir = 3;
        else if (IsKeyDown(KEY_D)) dir = 4;
        if (IsKeyPressed(KEY_ESCAPE)) break;

        //Send input at the same rate as the server.
        if (dir > 0 && (now - last_input_time) >= INPUT_INTERVAL) {
            last_input_time = now;

            InputCommand cmd;
            cmd.player_id    = (uint8_t)player_id;
            cmd.direction    = (uint8_t)dir;
            cmd.sequence     = input_seq++;
            cmd.timestamp_ms = now;


            //Update player immeadiately: Client prediction. 
            update_player(&world, player_id, cmd.direction);


            //Store in buffer for reconcilliation.
            if (ibuf.count < MAX_PENDING_INPUTS)
                ibuf.inputs[ibuf.count++] = cmd;


            //Reconciliation
            uint8_t pkt[sizeof(cmd) + 1];
            pkt[0] = MSG_INPUT;
            memcpy(pkt + 1, &cmd, sizeof(cmd));
            secure_send(ssl, pkt, (int)sizeof(pkt));
        }

        //Render frame.

        render_frame(&world, player_id, &stats,
                     SW, SH, has_snapshot, server_ip);
    }

    //Shutdown phase 
    //send leave msg to server.
    uint8_t leave[2] = {MSG_LEAVE, (uint8_t)player_id};
    //Clean up:
    secure_send(ssl, leave, 2);
    dtls_session_free(ssl);
    dtls_ctx_free(dtls_ctx);
    close(sock);
    CloseWindow();

    //print final statistics.
    print_network_analysis(stats, (int)world.players[player_id].score);
    return 0;
}
/*Communication flow
* 1. Client sends JOIN (plain UDP)
2. Server responds with player ID (plain UDP)
3. DTLS handshake (encrypted)
4. Client sends inputs (every 33ms)
5. Server sends snapshots (every 66ms)
6. Client reconciles and renders
7. Client sends periodic pings (every 1s)
8. Server responds with pongs
9. Client sends LEAVE on exit
*/
