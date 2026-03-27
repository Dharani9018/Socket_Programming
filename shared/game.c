#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <raylib.h>

void init_game_world(GameWorld *w) {
    memset(w->players, 0, sizeof(w->players));
    memset(w->coins, 0, sizeof(w->coins));
    memset(w->client_addrs, 0, sizeof(w->client_addrs));
    
    w->player_count = 0;
    w->sequence = 0;
    w->last_update = get_time_ms();
    memset(&w->stats, 0, sizeof(w->stats));
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        init_input_queue(&w->input_queues[i]);
    }
    
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
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (w->players[i].active) count++;
    }
    return count;
}

int add_player(GameWorld *w, struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!w->players[i].active) {
            w->players[i].active = 1;
            w->players[i].id = i;
            w->players[i].x = rand() % GRID_WIDTH;
            w->players[i].y = rand() % GRID_HEIGHT;
            w->players[i].score = 0;
            w->client_addrs[i] = *addr;
            w->player_count = get_player_count(w);
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
        w->coins[i].active = 1;
        w->coins[i].x = rand() % GRID_WIDTH;
        w->coins[i].y = rand() % GRID_HEIGHT;
        w->coins[i].respawn_timer = 0;
    }
}

void check_coin_collision(GameWorld *w, int id) {
    if (id < 0 || id >= MAX_PLAYERS || !w->players[id].active) return;
    
    for (int i = 0; i < MAX_COINS; i++) {
        if (w->coins[i].active &&
            w->coins[i].x == w->players[id].x &&
            w->coins[i].y == w->players[id].y) {
            
            w->coins[i].active = 0;
            w->players[id].score++;
            w->coins[i].respawn_timer = get_time_ms() + 1500;
        }
        
        if (!w->coins[i].active && w->coins[i].respawn_timer > 0 && 
            get_time_ms() > w->coins[i].respawn_timer) {
            w->coins[i].active = 1;
            w->coins[i].x = rand() % GRID_WIDTH;
            w->coins[i].y = rand() % GRID_HEIGHT;
            w->coins[i].respawn_timer = 0;
        }
    }
}

void update_player(GameWorld *w, int id, uint8_t d) {
    if (id < 0 || id >= MAX_PLAYERS || !w->players[id].active) return;
    
    if (d == 1 && w->players[id].y > 0) w->players[id].y--;
    if (d == 2 && w->players[id].y < GRID_HEIGHT - 1) w->players[id].y++;
    if (d == 3 && w->players[id].x > 0) w->players[id].x--;
    if (d == 4 && w->players[id].x < GRID_WIDTH - 1) w->players[id].x++;
    
    check_coin_collision(w, id);
}

void simulate_fixed_tick(GameWorld *world) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!world->players[i].active) continue;
        
        InputCommand cmd;
        while (pop_input(&world->input_queues[i], &cmd)) {
            update_player(world, i, cmd.direction);
        }
    }
}

void draw_game(GameWorld *w, int local_id, NetworkStats *stats) {
    // Draw grid
    for (int x = 0; x <= GRID_WIDTH; x++) {
        DrawLine(x * CELL_SIZE, 0, x * CELL_SIZE, GRID_HEIGHT * CELL_SIZE, GRAY);
    }
    for (int y = 0; y <= GRID_HEIGHT; y++) {
        DrawLine(0, y * CELL_SIZE, GRID_WIDTH * CELL_SIZE, y * CELL_SIZE, GRAY);
    }
    
    // Draw coins
    for (int i = 0; i < MAX_COINS; i++) {
        if (w->coins[i].active) {
            DrawRectangle(w->coins[i].x * CELL_SIZE, w->coins[i].y * CELL_SIZE,
                         CELL_SIZE, CELL_SIZE, YELLOW);
        }
    }
    
    // Draw players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (w->players[i].active) {
            Color color = (i == local_id) ? GREEN : RED;
            DrawRectangle(w->players[i].x * CELL_SIZE, w->players[i].y * CELL_SIZE,
                         CELL_SIZE, CELL_SIZE, color);
        }
    }
    
    // Draw stats overlay
    DrawRectangle(10, 10, 300, 120, Fade(BLACK, 0.7f));
    DrawText(TextFormat("Score: %d", w->players[local_id].score), 20, 20, 20, WHITE);
    DrawText(TextFormat("Players: %d", w->player_count), 20, 45, 20, WHITE);
    DrawText(TextFormat("Latency: %.0f ms", stats->avg_latency_ms), 20, 70, 20, WHITE);
    DrawText(TextFormat("Jitter: %.0f ms", stats->jitter_ms), 20, 95, 20, WHITE);
    DrawText(TextFormat("Loss: %.1f%%", stats->packet_loss_rate), 20, 120, 20, WHITE);
    DrawText(TextFormat("RTT: %u ms", stats->rtt_ms), 20, 145, 20, WHITE);
    DrawText("WASD to move | ESC to quit", 20, GRID_HEIGHT * CELL_SIZE - 30, 20, WHITE);
}

int get_input_raylib(void) {
    if (IsKeyPressed(KEY_W)) return 1;
    if (IsKeyPressed(KEY_S)) return 2;
    if (IsKeyPressed(KEY_A)) return 3;
    if (IsKeyPressed(KEY_D)) return 4;
    if (IsKeyPressed(KEY_ESCAPE)) return -1;
    return 0;
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
