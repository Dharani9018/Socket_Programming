#ifndef GAME_H
#define GAME_H

#include "network.h"
#include <ncurses.h>

#define CAT "@"
#define COIN "O"

typedef struct {
    Player players[MAX_PLAYERS];
    Coin coins[MAX_COINS];
    uint32_t sequence;
    uint32_t last_update;
    struct sockaddr_in client_addrs[MAX_PLAYERS];
    int player_count;
    NetworkStats stats;
    
    // Input queues for server
    InputQueue input_queues[MAX_PLAYERS];
} GameWorld;

typedef struct {
    InputCommand inputs[MAX_PENDING_INPUTS];
    int count;
    uint32_t last_ack_sequence;
} ClientInputBuffer;

void init_game_world(GameWorld *world);
void init_client_buffer(ClientInputBuffer *buffer);
uint32_t get_time_ms(void);

int add_player(GameWorld *world, struct sockaddr_in *addr);
void remove_player(GameWorld *world, int id);
int get_player_count(GameWorld *world);

void update_player(GameWorld *world, int id, uint8_t dir);
void spawn_coins(GameWorld *world);
void check_coin_collision(GameWorld *world, int id);

void simulate_fixed_tick(GameWorld *world);
void render_game(GameWorld *world, int local_id);
int get_input(void);
void print_network_analysis(NetworkStats stats, GameWorld w, int player_id);

extern int GRID_WIDTH;
extern int GRID_HEIGHT;

#endif
