#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "common.h"


void init_game_world(GameWorld *w) {
    memset(w->players, 0, sizeof(w->players));
    memset(w->coins,   0, sizeof(w->coins));
    memset(w->client_addrs, 0, sizeof(w->client_addrs));
    w->player_count = 0;
    w->sequence     = 0;
    w->last_update  = get_time_ms();
    memset(&w->stats, 0, sizeof(w->stats));
    for (int i = 0; i < MAX_PLAYERS; i++)
        init_input_queue(&w->input_queues[i]);
    srand(time(NULL));
    spawn_coins(w);
}

void init_client_buffer(ClientInputBuffer *buffer) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->count = 0;
    buffer->last_ack_sequence = 0;
}

int get_player_count(GameWorld *w) {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (w->players[i].active) count++;
    return count;
}

int add_player(GameWorld *w, struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!w->players[i].active) {
            w->players[i].active = 1;
            w->players[i].id     = i;
            w->players[i].x      = rand() % GRID_WIDTH;
            w->players[i].y      = rand() % GRID_HEIGHT;
            w->players[i].score  = 0;
            w->client_addrs[i]   = *addr;
            w->player_count      = get_player_count(w);
            init_input_queue(&w->input_queues[i]);
            printf("[ADD] Player %d added\n", i);
            return i;
        }
    }
    return -1;
}

void remove_player(GameWorld *w, int id) {
    if (id >= 0 && id < MAX_PLAYERS) {
        w->players[id].active = 0;
        w->player_count = get_player_count(w);
        init_input_queue(&w->input_queues[id]);
    }
}

void spawn_coins(GameWorld *w) {
    for (int i = 0; i < MAX_COINS; i++) {
        w->coins[i].active        = 1;
        w->coins[i].x             = rand() % GRID_WIDTH;
        w->coins[i].y             = rand() % GRID_HEIGHT;
        w->coins[i].respawn_timer = 0;
    }
}

void check_coin_collision(GameWorld *w, int id) {
    if (id < 0 || id >= MAX_PLAYERS || !w->players[id].active) return;
    for (int i = 0; i < MAX_COINS; i++) {
        if (w->coins[i].active &&
            w->coins[i].x == w->players[id].x &&
            w->coins[i].y == w->players[id].y) {
            w->coins[i].active        = 0;
            w->players[id].score++;
            w->coins[i].respawn_timer = get_time_ms() + 1500;
        }
        if (!w->coins[i].active && w->coins[i].respawn_timer > 0 &&
            get_time_ms() > w->coins[i].respawn_timer) {
            w->coins[i].active        = 1;
            w->coins[i].x             = rand() % GRID_WIDTH;
            w->coins[i].y             = rand() % GRID_HEIGHT;
            w->coins[i].respawn_timer = 0;
        }
    }
}

void update_player(GameWorld *w, int id, uint8_t d) {
    if (id < 0 || id >= MAX_PLAYERS || !w->players[id].active) return;
    if (d == 1 && w->players[id].y > 0)              w->players[id].y--;
    if (d == 2 && w->players[id].y < GRID_HEIGHT - 1) w->players[id].y++;
    if (d == 3 && w->players[id].x > 0)              w->players[id].x--;
    if (d == 4 && w->players[id].x < GRID_WIDTH - 1)  w->players[id].x++;
    check_coin_collision(w, id);
}

// FIX: process exactly ONE input per player per tick.
// Before this was draining the whole queue, so if 3 inputs arrived before
// the next 33ms tick they'd all fire at once = multi-cell jumps on server.
void simulate_fixed_tick(GameWorld *world) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!world->players[i].active) continue;
        InputCommand cmd;
        if (pop_input(&world->input_queues[i], &cmd))
            update_player(world, i, cmd.direction);
    }
}

void print_network_analysis(NetworkStats stats, int total_score) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║           GAME STATISTICS             ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Total Score:           %7d          ║\n", total_score);
    printf("╠════════════════════════════════════════╣\n");
    printf("║           NETWORK STATS               ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Avg Latency:           %7.2f ms      ║\n", stats.avg_latency_ms);
    printf("║ Jitter:                %7.2f ms      ║\n", stats.jitter_ms);
    printf("║ RTT:                   %7u ms        ║\n", stats.rtt_ms);
    printf("║ Packet Loss:           %7.2f%%       ║\n", stats.packet_loss_rate);
    printf("║ Bandwidth:             %7.2f KB/s     ║\n", stats.bandwidth_kbps);
    printf("╚════════════════════════════════════════╝\n");
}
