#ifndef GAME_H
#define GAME_H

#include "network.h"
#include <ncurses.h>

#define CAT "A"
#define COIN "O"
#define JITTER_BUFFER_MS 100
#define INTERPOLATION_MS 100
#define MAX_SNAPSHOT_BUFFER 10

// Interpolation structure for smooth movement (only defined here)
typedef struct {
    Player current;
    Player target;
    float interpolation_time;
    float interpolation_duration;
} InterpolatedPlayer;

typedef struct {
    Player players[MAX_PLAYERS];
    Coin coins[MAX_COINS];
    uint32_t sequence;
    uint32_t last_update;
    struct sockaddr_in client_addrs[MAX_PLAYERS];
    int player_count;
    NetworkStats stats;
    
    // Jitter buffer for packet loss tolerance
    GameSnapshot snapshot_buffer[MAX_SNAPSHOT_BUFFER];
    int buffer_count;
    int buffer_head;
    
    // Interpolation data for smooth movement
    InterpolatedPlayer interpolated[MAX_PLAYERS];
    uint32_t last_snapshot_time;
    uint32_t next_snapshot_time;
} GameWorld;

typedef struct {
    InputCommand inputs[MAX_PENDING_INPUTS];
    int count;
    uint32_t last_ack_sequence;
    uint32_t last_sent_sequence;
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

// Network features
void add_to_jitter_buffer(GameWorld *world, GameSnapshot *snapshot);
void process_jitter_buffer(GameWorld *world);
void interpolate_positions(GameWorld *world, float alpha);
void predict_movement(GameWorld *world, int player_id, ClientInputBuffer *buffer);
void reconcile_with_server(GameWorld *world, int player_id, 
                          ClientInputBuffer *buffer, GameSnapshot *snapshot);

void render_game(GameWorld *world, int local_id, NetworkStats *stats);
int get_input(void);
void print_network_analysis(NetworkStats *stats);

extern int GRID_WIDTH;
extern int GRID_HEIGHT;

#endif
