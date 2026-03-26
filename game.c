#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

WINDOW *game_win = NULL;
WINDOW *info_win = NULL;

int GRID_WIDTH = 60;
int GRID_HEIGHT = 25;

void init_game_world(GameWorld *w) {
    memset(w, 0, sizeof(*w));
    srand(time(NULL));
    spawn_coins(w);
    w->last_update = get_time_ms();
    w->player_count = 0;
    w->buffer_count = 0;
    w->buffer_head = 0;
    w->last_snapshot_time = 0;
    w->next_snapshot_time = 0;
    memset(&w->stats, 0, sizeof(w->stats));
    
    // Initialize interpolation
    for (int i = 0; i < MAX_PLAYERS; i++) {
        w->interpolated[i].interpolation_duration = INTERPOLATION_MS;
        w->interpolated[i].interpolation_time = 0;
        memset(&w->interpolated[i].current, 0, sizeof(Player));
        memset(&w->interpolated[i].target, 0, sizeof(Player));
    }
}

void init_client_buffer(ClientInputBuffer *buffer) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->count = 0;
    buffer->last_ack_sequence = 0;
    buffer->last_sent_sequence = 0;
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
            return i;
        }
    }
    return -1;
}

void remove_player(GameWorld *w, int id) {
    if (id >= 0 && id < MAX_PLAYERS) {
        w->players[id].active = 0;
        w->player_count = get_player_count(w);
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

void add_to_jitter_buffer(GameWorld *w, GameSnapshot *snapshot) {
    if (w->buffer_count < MAX_SNAPSHOT_BUFFER) {
        w->snapshot_buffer[w->buffer_count++] = *snapshot;
    } else {
        w->snapshot_buffer[w->buffer_head] = *snapshot;
        w->buffer_head = (w->buffer_head + 1) % MAX_SNAPSHOT_BUFFER;
    }
    
    // Simple bubble sort for sequence ordering
    for (int i = 0; i < w->buffer_count - 1; i++) {
        for (int j = 0; j < w->buffer_count - i - 1; j++) {
            if (w->snapshot_buffer[j].sequence > w->snapshot_buffer[j + 1].sequence) {
                GameSnapshot temp = w->snapshot_buffer[j];
                w->snapshot_buffer[j] = w->snapshot_buffer[j + 1];
                w->snapshot_buffer[j + 1] = temp;
            }
        }
    }
}

void process_jitter_buffer(GameWorld *w) {
    if (w->buffer_count == 0) return;
    
    // Use the latest snapshot
    GameSnapshot *latest = &w->snapshot_buffer[w->buffer_count - 1];
    
    // Update world with authoritative state
    memcpy(w->players, latest->players, sizeof(w->players));
    memcpy(w->coins, latest->coins, sizeof(w->coins));
    w->player_count = latest->player_count;
    w->sequence = latest->sequence;
    
    // Setup interpolation targets
    w->last_snapshot_time = w->last_update;
    w->next_snapshot_time = get_time_ms();
    w->last_update = get_time_ms();
    
    // Clear buffer
    w->buffer_count = 0;
    w->buffer_head = 0;
}

void interpolate_positions(GameWorld *w, float alpha) {
    (void)alpha; // Suppress unused parameter warning
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (w->players[i].active) {
            w->interpolated[i].current = w->players[i];
        }
    }
}

void predict_movement(GameWorld *world, int player_id, ClientInputBuffer *buffer) {
    for (int i = 0; i < buffer->count; i++) {
        if (buffer->inputs[i].sequence > buffer->last_ack_sequence) {
            update_player(world, player_id, buffer->inputs[i].direction);
        }
    }
}

void reconcile_with_server(GameWorld *world, int player_id, 
                          ClientInputBuffer *buffer, GameSnapshot *snapshot) {
    if (player_id < 0 || player_id >= MAX_PLAYERS) return;
    
    // Apply server state
    world->players[player_id] = snapshot->players[player_id];
    
    // Update acknowledged sequence
    buffer->last_ack_sequence = snapshot->last_processed_input;
    
    // Remove acknowledged inputs
    int new_count = 0;
    for (int i = 0; i < buffer->count; i++) {
        if (buffer->inputs[i].sequence > buffer->last_ack_sequence) {
            buffer->inputs[new_count++] = buffer->inputs[i];
        }
    }
    buffer->count = new_count;
    
    // Re-apply pending inputs for prediction correction
    for (int i = 0; i < buffer->count; i++) {
        update_player(world, player_id, buffer->inputs[i].direction);
    }
}

void render_game(GameWorld *w, int local_id, NetworkStats *stats) {
    // Initialize windows ONCE
    if (!game_win) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        
        GRID_WIDTH = max_x - 4;
        GRID_HEIGHT = max_y - 12;
        
        if (GRID_WIDTH < 40) GRID_WIDTH = 40;
        if (GRID_HEIGHT < 15) GRID_HEIGHT = 15;
        
        game_win = newwin(GRID_HEIGHT + 2, GRID_WIDTH + 2, 0, 0);
        info_win = newwin(10, GRID_WIDTH + 2, GRID_HEIGHT + 2, 0);
        box(game_win, 0, 0);
        box(info_win, 0, 0);
    }
    
    // Clear and redraw game area
    werase(game_win);
    box(game_win, 0, 0);
    
    // Draw ALL coins
    for (int i = 0; i < MAX_COINS; i++) {
        if (w->coins[i].active && 
            w->coins[i].x < GRID_WIDTH && 
            w->coins[i].y < GRID_HEIGHT) {
            wattron(game_win, COLOR_PAIR(1));
            mvwprintw(game_win, w->coins[i].y + 1, w->coins[i].x + 1, COIN);
            wattroff(game_win, COLOR_PAIR(1));
        }
    }
    
    // Draw ALL active players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (w->players[i].active && 
            w->players[i].x < GRID_WIDTH && 
            w->players[i].y < GRID_HEIGHT) {
            int color = (i == local_id) ? 2 : 3;
            wattron(game_win, COLOR_PAIR(color));
            if (i == local_id) wattron(game_win, A_BOLD);
            
            mvwprintw(game_win, w->players[i].y + 1, w->players[i].x + 1, CAT);
            
            if (i == local_id) wattroff(game_win, A_BOLD);
            wattroff(game_win, COLOR_PAIR(color));
        }
    }
    
    wrefresh(game_win);
    
    // Update info panel
    werase(info_win);
    box(info_win, 0, 0);
    
    mvwprintw(info_win, 1, 2, "Player: %d  Score: %d", 
              local_id, w->players[local_id].score);
    mvwprintw(info_win, 2, 2, "Players: %d", w->player_count);
    mvwprintw(info_win, 3, 2, "Latency: %.0f ms (RTT: %u ms)", 
              stats->avg_latency_ms, stats->rtt_ms);
    mvwprintw(info_win, 4, 2, "Jitter: %.0f ms", stats->jitter_ms);
    mvwprintw(info_win, 5, 2, "Loss: %.1f%%", stats->packet_loss_rate);
    mvwprintw(info_win, 6, 2, "Bandwidth: %.0f KB/s", stats->bandwidth_kbps);
    mvwprintw(info_win, 8, 2, "WASD to move | Q to quit");
    
    wrefresh(info_win);
}

int get_input(void) {
    int ch = getch();
    switch(ch) {
        case 'w': case 'W': return 1;
        case 's': case 'S': return 2;
        case 'a': case 'A': return 3;
        case 'd': case 'D': return 4;
        case 'q': case 'Q': return -1;
        default: return 0;
    }
}

void print_network_analysis(NetworkStats *stats) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║     NETWORK PERFORMANCE ANALYSIS      ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Avg Latency:   %7.2f ms              ║\n", stats->avg_latency_ms);
    printf("║ Min Latency:   %7.2f ms              ║\n", stats->min_latency_ms);
    printf("║ Max Latency:   %7.2f ms              ║\n", stats->max_latency_ms);
    printf("║ Jitter:        %7.2f ms              ║\n", stats->jitter_ms);
    printf("║ RTT:           %7u ms                ║\n", stats->rtt_ms);
    printf("║ Loss Rate:     %7.2f%%              ║\n", stats->packet_loss_rate);
    printf("║ Packets Sent:  %7u                  ║\n", stats->packets_sent);
    printf("║ Packets Recv:  %7u                  ║\n", stats->packets_received);
    printf("║ Packets Lost:  %7u                  ║\n", stats->packets_lost);
    printf("║ Bandwidth:     %7.2f KB/s           ║\n", stats->bandwidth_kbps);
    printf("╚════════════════════════════════════════╝\n");
}
