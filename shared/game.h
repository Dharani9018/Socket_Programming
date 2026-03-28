#ifndef GAME_H
#define GAME_H

#include "network.h"

typedef struct {
    Player              players[MAX_PLAYERS];
    Coin                coins[MAX_COINS];
    uint32_t            sequence;
    uint32_t            last_update;
    struct sockaddr_in  client_addrs[MAX_PLAYERS];
    int                 player_count;
    NetworkStats        stats;
    InputQueue          input_queues[MAX_PLAYERS];
} GameWorld;

void init_game_world(GameWorld *world);
void init_client_buffer(ClientInputBuffer *buffer);

int  add_player(GameWorld *world, struct sockaddr_in *addr);
void remove_player(GameWorld *world, int id);
int  get_player_count(GameWorld *world);

void update_player(GameWorld *world, int id, uint8_t direction);
void spawn_coins(GameWorld *world);
void check_coin_collision(GameWorld *world, int id);

void simulate_fixed_tick(GameWorld *world);

void print_network_analysis(NetworkStats stats, int total_score);

#endif
