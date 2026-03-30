#include "client_render.h"
#include <stdio.h>
#include <string.h>


Color player_colors[MAX_PLAYERS] = {
    { 80, 230, 120, 255},   // P0 — mint green  (local player default)
    {255,  80,  90, 255},   // P1 — coral red
    { 80, 160, 255, 255},   // P2 — sky blue
    {200,  80, 255, 255},   // P3 — purple
};



static void draw_rounded_rect(int x, int y, int w, int h,
                               float roundness, Color c) {
    DrawRectangleRounded((Rectangle){(float)x, (float)y,
                                     (float)w, (float)h},
                         roundness, 6, c);
}
//Draw grid.
void render_grid(int gw, int gh, int cs) {
    for (int x = 0; x <= gw; x++)
        DrawLine(x * cs, 0, x * cs, gh * cs, COL_GRID);
    for (int y = 0; y <= gh; y++)
        DrawLine(0, y * cs, gw * cs, y * cs, COL_GRID);
}

//Draws coin.
void render_coin(int cx, int cy, int size) 
{
    int r  = size / 2 - 1;
    int ox = cx + size / 2;
    int oy = cy + size / 2;
    DrawCircle(ox, oy, (float)r, COL_COIN);

    int hs = (r / 4 > 1) ? r / 4 : 1;
    DrawCircle(ox - r / 3, oy - r / 3, (float)hs,
               (Color){255, 255, 200, 160});
}

//Draws player character.
void render_player(int px, int py, int size, Color col, int is_local) {

    DrawRectangle(px + 2, py + 3, size, size, (Color){0, 0, 0, 70});


    if (is_local)
        DrawRectangle(px - 2, py - 2, size + 4, size + 4,
                      (Color){col.r, col.g, col.b, 90});

    draw_rounded_rect(px, py, size, size, 0.3f, col);

    // Eyes
    int ey = py + size / 3;
    DrawRectangle(px + size / 4,           ey, 3, 3, WHITE);
    DrawRectangle(px + size / 2 + size / 8, ey, 3, 3, WHITE);
}



static Color quality_color(float loss_pct) //return colors based on packt loss.
{
    if (loss_pct < 5.0f)  return COL_ACCENT;//good (green)
    if (loss_pct < 15.0f) return COL_WARN;//Warning (yellow)
    return COL_BAD; //Bad (red).
}

static void draw_loss_bar(int x, int y, int w, int h,
                           float frac, Color fill) 
{
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    DrawRectangle(x, y, w, h, (Color){40, 50, 70, 200});
    DrawRectangle(x, y, (int)(w * frac), h, fill);
    DrawRectangleLines(x, y, w, h, (Color){80, 90, 110, 160});
}


//Score board network stats.
void render_hud(GameWorld *w, int local_id,
                NetworkStats *stats, int sw, int sh) 
{


    const int px = 12, py = 12, pw = 230, ph = 194;

    draw_rounded_rect(px, py, pw, ph, 0.1f, COL_HUD_BG);
    DrawRectangleRoundedLines(
        (Rectangle){(float)px, (float)py, (float)pw, (float)ph},
        0.1f, 6, (Color){60, 70, 100, 180});

    // Title + DTLS badge
    DrawText("CAT ARENA", px + 10, py + 10, 18, COL_ACCENT);
    DrawText("DTLS", px + pw - 44, py + 13, 11, (Color){80, 200, 255, 220});
    DrawLine(px + 10, py + 32, px + pw - 10, py + 32,
             (Color){60, 70, 100, 200});


    char score_str[16];
    snprintf(score_str, sizeof(score_str), "%u",
             w->players[local_id].score);
    DrawText("SCORE",    px + 10,  py + 40, 11, COL_DIM);
    DrawText(score_str,  px + 10,  py + 53, 28,
             player_colors[local_id % MAX_PLAYERS]);


    DrawText("PLAYERS",  px + 130, py + 40, 11, COL_DIM);
    char pl_str[16];
    snprintf(pl_str, sizeof(pl_str), "%d / %d",
             w->player_count, MAX_PLAYERS);
    DrawText(pl_str, px + 130, py + 53, 18, COL_TEXT);

    DrawLine(px + 10, py + 88, px + pw - 10, py + 88,
             (Color){40, 50, 70, 200});


    struct { const char *label; char val[32]; Color col; } rows[5] = {
        {"LATENCY", {0}, COL_TEXT},
        {"JITTER",  {0}, COL_TEXT},
        {"LOSS",    {0}, COL_TEXT},
        {"RTT",     {0}, COL_TEXT},
        {"BW",      {0}, COL_TEXT},
    };
    rows[2].col = quality_color(stats->packet_loss_rate);
    rows[3].col = (stats->rtt_ms > 200) ? COL_WARN : COL_TEXT;

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


    draw_loss_bar(px + 10, py + ph - 14, pw - 20, 6,
                  stats->packet_loss_rate / 100.0f,
                  quality_color(stats->packet_loss_rate));


    if (w->player_count > 1) {
        int active = 0;
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (w->players[i].active) active++;

        const int sbw = 160;
        const int sbh = 26 + active * 22 + 8;
        const int sbx = sw - sbw - 12;
        const int sby = sh - sbh - 36;

        draw_rounded_rect(sbx, sby, sbw, sbh, 0.1f, COL_HUD_BG);
        DrawRectangleRoundedLines(
            (Rectangle){(float)sbx, (float)sby, (float)sbw, (float)sbh},
            0.1f, 6, (Color){60, 70, 100, 160});
        DrawText("SCOREBOARD", sbx + 10, sby + 8, 11, COL_DIM);

        int row = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!w->players[i].active) continue;
            int    ry = sby + 26 + row * 22;
            Color  pc = player_colors[i % MAX_PLAYERS];
            DrawRectangle(sbx + 10, ry + 3, 10, 10, pc);

            char name[24];
            snprintf(name, sizeof(name), "P%d%s",
                     i, (i == local_id) ? " (you)" : "");
            DrawText(name, sbx + 28, ry, 13,
                     (i == local_id) ? COL_ACCENT : COL_TEXT);

            char sc[12];
            snprintf(sc, sizeof(sc), "%u", w->players[i].score);
            DrawText(sc, sbx + sbw - 28, ry, 13, pc);
            row++;
        }
    }


    draw_rounded_rect(sw / 2 - 130, sh - 28, 260, 22, 0.4f, COL_HUD_BG);
    DrawText("WASD  move    ESC  quit",
             sw / 2 - 105, sh - 23, 13, COL_DIM);
}



void render_connecting(const char *server_ip, int sw, int sh) {
    DrawText("CONNECTING (DTLS)...",
             sw / 2 - 100, sh / 2 - 12, 24, COL_ACCENT);
    DrawText(server_ip,
             sw / 2 - 30,  sh / 2 + 16, 14, COL_DIM);
}



void render_frame(GameWorld *world, int local_id,
                  NetworkStats *stats, int sw, int sh,
                  int has_snapshot, const char *server_ip) {
    BeginDrawing();
    ClearBackground(COL_BG);

    if (has_snapshot) {
        render_grid(GRID_WIDTH, GRID_HEIGHT, CELL_SIZE);

        for (int i = 0; i < MAX_COINS; i++)
            if (world->coins[i].active)
                render_coin(world->coins[i].x * CELL_SIZE,
                            world->coins[i].y * CELL_SIZE,
                            CELL_SIZE - 2);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!world->players[i].active) continue;
            render_player(world->players[i].x * CELL_SIZE,
                          world->players[i].y * CELL_SIZE,
                          CELL_SIZE - 1,
                          player_colors[i % MAX_PLAYERS],
                          (i == local_id));
        }

        render_hud(world, local_id, stats, sw, sh);
    } else {
        render_connecting(server_ip, sw, sh);
    }

    EndDrawing();
}
