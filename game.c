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

void render_game(GameWorld *w, int local_id, NetworkStats *stats) {
    if (!game_win) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        
        GRID_WIDTH = max_x - 4;
        GRID_HEIGHT = max_y - 10;
        
        if (GRID_WIDTH < 40) GRID_WIDTH = 40;
        if (GRID_HEIGHT < 15) GRID_HEIGHT = 15;
        
        game_win = newwin(GRID_HEIGHT + 2, GRID_WIDTH + 2, 0, 0);
        info_win = newwin(8, GRID_WIDTH + 2, GRID_HEIGHT + 2, 0);
        box(game_win, 0, 0);
        box(info_win, 0, 0);
    }
    
    werase(game_win);
    box(game_win, 0, 0);
    
    // Draw coins
    for (int i = 0; i < MAX_COINS; i++) {
        if (w->coins[i].active && 
            w->coins[i].x < GRID_WIDTH && 
            w->coins[i].y < GRID_HEIGHT) {
            wattron(game_win, COLOR_PAIR(1));
            mvwprintw(game_win, w->coins[i].y + 1, w->coins[i].x + 1, COIN);
            wattroff(game_win, COLOR_PAIR(1));
        }
    }
    
    // Draw players
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
    
    // Info panel
    werase(info_win);
    box(info_win, 0, 0);
    
    mvwprintw(info_win, 1, 2, "Player: %d  Score: %d", 
              local_id, w->players[local_id].score);
    mvwprintw(info_win, 2, 2, "Players: %d", w->player_count);
    mvwprintw(info_win, 3, 2, "Latency: %.0f ms", stats->avg_latency_ms);
    mvwprintw(info_win, 4, 2, "Jitter: %.0f ms", stats->jitter_ms);
    mvwprintw(info_win, 5, 2, "Loss: %.1f%%", stats->packet_loss_rate);
    mvwprintw(info_win, 6, 2, "RTT: %u ms", stats->rtt_ms);
    mvwprintw(info_win, 7, 2, "WASD to move | Q to quit");
    
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
    printf("\n=== NETWORK STATS ===\n");
    printf("Avg Latency: %.2f ms\n", stats->avg_latency_ms);
    printf("Jitter: %.2f ms\n", stats->jitter_ms);
    printf("Loss Rate: %.2f%%\n", stats->packet_loss_rate);
    printf("RTT: %u ms\n", stats->rtt_ms);
    printf("===================\n");
}
