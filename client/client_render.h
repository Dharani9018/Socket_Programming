#ifndef CLIENT_RENDER_H
#define CLIENT_RENDER_H

#include "../shared/game.h"
#include "../shared/network.h"
#include <raylib.h>


#define COL_BG     (Color){10,  12,  20,  255}
#define COL_GRID   (Color){28,  34,  52,  255}
#define COL_COIN   (Color){255, 210,  50,  255}
#define COL_HUD_BG (Color){10,  12,  20,  210}
#define COL_TEXT   (Color){220, 225, 240,  255}
#define COL_ACCENT (Color){ 80, 230, 120,  255}
#define COL_DIM    (Color){100, 110, 140,  255}
#define COL_WARN   (Color){255, 140,  50,  255}
#define COL_BAD    (Color){255,  70,  70,  255}


extern Color player_colors[MAX_PLAYERS];


void render_grid(int grid_w, int grid_h, int cell_size);
void render_coin(int cell_x, int cell_y, int cell_size);
void render_player(int cell_x, int cell_y, int cell_size,
                   Color col, int is_local);


void render_hud(GameWorld *world, int local_id,
                NetworkStats *stats, int screen_w, int screen_h);


void render_connecting(const char *server_ip, int screen_w, int screen_h);


void render_frame(GameWorld *world, int local_id,
                  NetworkStats *stats, int screen_w, int screen_h,
                  int has_snapshot, const char *server_ip);

#endif
