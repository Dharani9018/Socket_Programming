#include "../shared/game.h"
#include "../shared/network.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <raylib.h>

#define USE_RAYLIB

// ─── PALETTE ──────────────────────────────────────────────────────────────────
#define COL_BG     (Color){10,  12,  20,  255}
#define COL_GRID   (Color){28,  34,  52,  255}
#define COL_COIN   (Color){255, 210,  50,  255}
#define COL_HUD_BG (Color){10,  12,  20, 210}
#define COL_TEXT   (Color){220, 225, 240,  255}
#define COL_ACCENT (Color){ 80, 230, 120,  255}
#define COL_DIM    (Color){100, 110, 140,  255}
#define COL_WARN   (Color){255, 140,  50,  255}
#define COL_BAD    (Color){255,  70,  70,  255}

static Color player_colors[MAX_PLAYERS] = {
    { 80, 230, 120, 255},
    {255,  80,  90, 255},
    { 80, 160, 255, 255},
    {200,  80, 255, 255},
};

// ─── DRAW HELPERS ─────────────────────────────────────────────────────────────

static void draw_rounded_rect(int x, int y, int w, int h, float r, Color c) {
    DrawRectangleRounded((Rectangle){x, y, w, h}, r, 6, c);
}

// Simple coin: plain filled circle, no animation
static void draw_coin(int cx, int cy, int size) {
    int r  = size / 2 - 1;
    int ox = cx + size / 2;
    int oy = cy + size / 2;
    DrawCircle(ox, oy, (float)r, COL_COIN);
    // small highlight dot
    int hs = r / 4 > 1 ? r / 4 : 1;
    DrawCircle(ox - r/3, oy - r/3, (float)hs, (Color){255, 255, 200, 160});
}

static void draw_player(int px, int py, int size, Color col, int local) {
    // Drop shadow
    DrawRectangle(px + 2, py + 3, size, size, (Color){0, 0, 0, 70});

    // Local player outline
    if (local)
        DrawRectangle(px - 2, py - 2, size + 4, size + 4,
                      (Color){col.r, col.g, col.b, 90});

    draw_rounded_rect(px, py, size, size, 0.3f, col);

    // Eyes
    int ey = py + size / 3;
    DrawRectangle(px + size/4,           ey, 3, 3, WHITE);
    DrawRectangle(px + size/2 + size/8,  ey, 3, 3, WHITE);
}

static void draw_grid(int gw, int gh, int cs) {
    for (int x = 0; x <= gw; x++)
        DrawLine(x * cs, 0, x * cs, gh * cs, COL_GRID);
    for (int y = 0; y <= gh; y++)
        DrawLine(0, y * cs, gw * cs, y * cs, COL_GRID);
}

// ─── HUD ──────────────────────────────────────────────────────────────────────

static Color quality_color(float loss) {
    if (loss < 5.0f)  return COL_ACCENT;
    if (loss < 15.0f) return COL_WARN;
    return COL_BAD;
}

static void draw_bar(int x, int y, int w, int h, float frac, Color fill) {
    if (frac > 1.0f) frac = 1.0f;
    DrawRectangle(x, y, w, h, (Color){40, 50, 70, 200});
    DrawRectangle(x, y, (int)(w * frac), h, fill);
    DrawRectangleLines(x, y, w, h, (Color){80, 90, 110, 160});
}

static void draw_hud(GameWorld *w, int local_id, NetworkStats *stats, int sw, int sh) {
    // ── Left panel ────────────────────────────────────────
    int px = 12, py = 12, pw = 230, ph = 194;
    draw_rounded_rect(px, py, pw, ph, 0.1f, COL_HUD_BG);
    DrawRectangleRoundedLines((Rectangle){px, py, pw, ph}, 0.1f, 6,
                              (Color){60, 70, 100, 180});

    DrawText("CAT ARENA", px + 10, py + 10, 18, COL_ACCENT);
    DrawLine(px + 10, py + 32, px + pw - 10, py + 32, (Color){60, 70, 100, 200});

    char score_buf[16];
    snprintf(score_buf, sizeof(score_buf), "%d", w->players[local_id].score);
    DrawText("SCORE",   px + 10,  py + 40, 11, COL_DIM);
    DrawText(score_buf, px + 10,  py + 53, 28, player_colors[local_id % MAX_PLAYERS]);

    DrawText("PLAYERS", px + 130, py + 40, 11, COL_DIM);
    char pl_buf[16];
    snprintf(pl_buf, sizeof(pl_buf), "%d / %d", w->player_count, MAX_PLAYERS);
    DrawText(pl_buf, px + 130, py + 53, 18, COL_TEXT);

    DrawLine(px + 10, py + 88, px + pw - 10, py + 88, (Color){40, 50, 70, 200});

    // Network rows: label + value + color
    typedef struct { const char *label; char val[32]; Color col; } Row;
    Row rows[5];
    rows[0].label = "LATENCY"; rows[0].col = COL_TEXT;
    rows[1].label = "JITTER";  rows[1].col = COL_TEXT;
    rows[2].label = "LOSS";    rows[2].col = quality_color(stats->packet_loss_rate);
    rows[3].label = "RTT";     rows[3].col = (stats->rtt_ms > 200) ? COL_WARN : COL_TEXT;
    rows[4].label = "BW";      rows[4].col = COL_TEXT;

    snprintf(rows[0].val, 32, "%.0f ms",   stats->avg_latency_ms);
    snprintf(rows[1].val, 32, "%.1f ms",   stats->jitter_ms);
    snprintf(rows[2].val, 32, "%.1f%%",    stats->packet_loss_rate);
    snprintf(rows[3].val, 32, "%u ms",     stats->rtt_ms);
    snprintf(rows[4].val, 32, "%.1f KB/s", stats->bandwidth_kbps);

    for (int i = 0; i < 5; i++) {
        int ry = py + 96 + i * 18;
        DrawText(rows[i].label, px + 10,  ry, 11, COL_DIM);
        DrawText(rows[i].val,   px + 110, ry, 13, rows[i].col);
    }

    draw_bar(px + 10, py + ph - 14, pw - 20, 6,
             stats->packet_loss_rate / 100.0f,
             quality_color(stats->packet_loss_rate));

    // ── Scoreboard (if >1 player) ─────────────────────────
    if (w->player_count > 1) {
        int active = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) if (w->players[i].active) active++;
        int sbw = 160, sbh = 26 + active * 22 + 8;
        int sbx = sw - sbw - 12, sby = sh - sbh - 36;
        draw_rounded_rect(sbx, sby, sbw, sbh, 0.1f, COL_HUD_BG);
        DrawRectangleRoundedLines((Rectangle){sbx, sby, sbw, sbh}, 0.1f, 6,
                                  (Color){60, 70, 100, 160});
        DrawText("SCOREBOARD", sbx + 10, sby + 8, 11, COL_DIM);
        int row = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!w->players[i].active) continue;
            int ry = sby + 26 + row * 22;
            Color pc = player_colors[i % MAX_PLAYERS];
            DrawRectangle(sbx + 10, ry + 3, 10, 10, pc);
            char name[24];
            snprintf(name, sizeof(name), "P%d%s", i, (i == local_id) ? " (you)" : "");
            DrawText(name, sbx + 28, ry, 13, (i == local_id) ? COL_ACCENT : COL_TEXT);
            char sc[12];
            snprintf(sc, sizeof(sc), "%u", w->players[i].score);
            DrawText(sc, sbx + sbw - 28, ry, 13, pc);
            row++;
        }
    }

    // ── Bottom hint ───────────────────────────────────────
    draw_rounded_rect(sw / 2 - 130, sh - 28, 260, 22, 0.4f, COL_HUD_BG);
    DrawText("WASD  move    ESC  quit", sw / 2 - 105, sh - 23, 13, COL_DIM);
}

// ─── MAIN ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    const int screenWidth  = GRID_WIDTH  * CELL_SIZE;
    const int screenHeight = GRID_HEIGHT * CELL_SIZE;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "Cat Arena");
    SetTargetFPS(60);

    int sock = init_client_socket();
    if (sock < 0) { CloseWindow(); return 1; }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    GameWorld        world;
    ClientInputBuffer input_buffer;
    NetworkStats      stats;
    init_game_world(&world);
    init_client_buffer(&input_buffer);
    memset(&stats, 0, sizeof(stats));

    // ── Connect ───────────────────────────────────────────
    uint8_t join_msg = MSG_JOIN;
    send_packet(sock, &server_addr, &join_msg, 1);

    char             buffer[BUFFER_SIZE];
    struct sockaddr_in from;
    uint32_t         start     = get_time_ms();
    int              player_id = -1;

    while (player_id == -1 && (get_time_ms() - start) < 5000) {
        int len = receive_packet(sock, buffer, sizeof(buffer), &from);
        if (len >= 2 && buffer[0] == MSG_JOIN) {
            player_id = buffer[1];
            printf("Connected as Player %d!\n", player_id);
        }
        usleep(10000);
    }

    if (player_id == -1) {
        printf("Failed to connect\n");
        CloseWindow();
        return 1;
    }

    // ── Loop state ────────────────────────────────────────
    uint32_t last_ping         = 0;
    uint32_t ping_sent_at      = 0;
    uint32_t input_sequence    = 0;
    uint32_t expected_seq      = 0;
    uint32_t pkts_received     = 0;
    uint32_t pkts_lost         = 0;
    int      has_snapshot      = 0;

    // Rate-limit input to server tick rate (30 Hz = one move per ~33ms)
    uint32_t last_input_time   = 0;
    const uint32_t INPUT_INTERVAL = 1000 / TICK_RATE; // 33 ms

    // Bandwidth: count bytes in a 1-second window
    uint32_t bw_bytes  = 0;
    uint32_t bw_start  = get_time_ms();

    float last_latency = -1.0f;

    while (!WindowShouldClose()) {
        uint32_t now = get_time_ms();

        // ── Ping ──────────────────────────────────────────
        if (now - last_ping >= 1000) {
            uint8_t ping = MSG_PING;
            send_packet(sock, &server_addr, &ping, 1);
            ping_sent_at = now;
            last_ping    = now;
        }

        // ── Receive ───────────────────────────────────────
        int len;
        while ((len = receive_packet(sock, buffer, sizeof(buffer), &from)) > 0) {
            bw_bytes += (uint32_t)len;

            if (buffer[0] == MSG_PONG) {
                uint32_t rtt = get_time_ms() - ping_sent_at;
                stats.rtt_ms = (stats.rtt_ms == 0)
                    ? rtt
                    : (uint32_t)(stats.rtt_ms * 0.875f + rtt * 0.125f);

            } else if ((size_t)len >= sizeof(GameSnapshot)) {
                GameSnapshot *snap = (GameSnapshot *)buffer;

                // Packet loss
                if (expected_seq > 0 && snap->sequence > expected_seq)
                    pkts_lost += snap->sequence - expected_seq;
                expected_seq = snap->sequence + 1;
                pkts_received++;
                uint32_t total = pkts_received + pkts_lost;
                if (total > 0)
                    stats.packet_loss_rate = (float)pkts_lost / total * 100.0f;

                // Latency (measured at receive time, not stale loop timestamp)
                uint32_t recv_now = get_time_ms();
                float latency = (float)(recv_now - snap->timestamp);
                if (latency > 2000.0f) latency = 2000.0f;

                if (stats.avg_latency_ms == 0.0f) {
                    stats.avg_latency_ms = latency;
                } else {
                    // Jitter = EWMA of |delta between consecutive latency samples|
                    if (last_latency >= 0.0f) {
                        float diff = latency - last_latency;
                        if (diff < 0.0f) diff = -diff;
                        stats.jitter_ms = stats.jitter_ms * 0.9f + diff * 0.1f;
                    }
                    stats.avg_latency_ms = stats.avg_latency_ms * 0.9f + latency * 0.1f;
                }
                last_latency = latency;

                // Apply server state
                memcpy(world.players, snap->players, sizeof(world.players));
                memcpy(world.coins,   snap->coins,   sizeof(world.coins));
                world.player_count = snap->player_count;

                // Reconciliation: discard ack'd inputs, replay remainder.
                // Only replay inputs from PREVIOUS frames.
                // The current frame's input was already applied via client prediction;
                // replaying it again is what caused the 5-cell jump.
                uint32_t ack          = snap->last_processed_input[player_id];
                uint32_t just_applied = input_sequence - 1;
                int      new_cnt      = 0;
                for (int i = 0; i < input_buffer.count; i++)
                    if (input_buffer.inputs[i].sequence > ack)
                        input_buffer.inputs[new_cnt++] = input_buffer.inputs[i];
                input_buffer.count = new_cnt;

                for (int i = 0; i < input_buffer.count; i++) {
                    if (input_buffer.inputs[i].sequence == just_applied) continue;
                    update_player(&world, player_id,
                                  input_buffer.inputs[i].direction);
                }

                has_snapshot = 1;
            }
        }

        // ── Bandwidth window ──────────────────────────────
        uint32_t bw_elapsed = now - bw_start;
        if (bw_elapsed >= 1000) {
            // bytes / ms = KB/s  (1 byte/ms = 1 KB/s)
            stats.bandwidth_kbps = (float)bw_bytes / (float)bw_elapsed;
            bw_bytes  = 0;
            bw_start  = now;
        }

        // ── Input (rate-limited to TICK_RATE) ─────────────
        // IsKeyDown reads hardware state every frame (60/s).
        // We gate on a 33ms timer so exactly one move per server tick.
        int dir = 0;
        if      (IsKeyDown(KEY_W)) dir = 1;
        else if (IsKeyDown(KEY_S)) dir = 2;
        else if (IsKeyDown(KEY_A)) dir = 3;
        else if (IsKeyDown(KEY_D)) dir = 4;
        if (IsKeyPressed(KEY_ESCAPE)) break;

        if (dir > 0 && (now - last_input_time) >= INPUT_INTERVAL) {
            last_input_time = now;

            InputCommand cmd;
            cmd.player_id    = player_id;
            cmd.direction    = dir;
            cmd.sequence     = input_sequence++;
            cmd.timestamp_ms = now;

            // Client prediction: apply immediately without waiting for server
            update_player(&world, player_id, dir);

            // Store AFTER applying so reconciliation replay skips this frame's move.
            // Reconciliation only replays inputs that arrived in a PREVIOUS frame
            // but haven't been ack'd yet — it must not re-apply what we just moved.
            if (input_buffer.count < MAX_PENDING_INPUTS)
                input_buffer.inputs[input_buffer.count++] = cmd;

            // Send over UDP
            uint8_t out[sizeof(cmd) + 1];
            out[0] = MSG_INPUT;
            memcpy(out + 1, &cmd, sizeof(cmd));
            send_packet(sock, &server_addr, out, sizeof(out));
        }

        // ── Render ────────────────────────────────────────
        BeginDrawing();
        ClearBackground(COL_BG);

        if (has_snapshot) {
            draw_grid(GRID_WIDTH, GRID_HEIGHT, CELL_SIZE);

            for (int i = 0; i < MAX_COINS; i++)
                if (world.coins[i].active)
                    draw_coin(world.coins[i].x * CELL_SIZE,
                              world.coins[i].y * CELL_SIZE,
                              CELL_SIZE - 2);

            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!world.players[i].active) continue;
                draw_player(world.players[i].x * CELL_SIZE,
                            world.players[i].y * CELL_SIZE,
                            CELL_SIZE - 1,
                            player_colors[i % MAX_PLAYERS],
                            (i == player_id));
            }

            draw_hud(&world, player_id, &stats, screenWidth, screenHeight);
        } else {
            DrawText("CONNECTING...",
                     screenWidth / 2 - 80, screenHeight / 2 - 12, 24, COL_ACCENT);
            DrawText(argv[1],
                     screenWidth / 2 - 30, screenHeight / 2 + 16, 14, COL_DIM);
        }

        EndDrawing();
    }

    // ── Cleanup ───────────────────────────────────────────
    uint8_t leave[2] = {MSG_LEAVE, (uint8_t)player_id};
    send_packet(sock, &server_addr, leave, 2);
    close(sock);
    CloseWindow();

    print_network_analysis(stats, world.players[player_id].score);
    return 0;
}
