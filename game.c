#include "game.h"
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int color_pairs[] = {1, 2, 3, 4, 5, 6};

WINDOW *game_win = NULL;
WINDOW *info_win = NULL;

void init_game_world(GameWorld *world) {
    memset(world, 0, sizeof(GameWorld));
    world->player_count = 0;
    world->sequence = 0;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        world->players[i].active = 0;
        memset(&world->client_addrs[i], 0, sizeof(struct sockaddr_in));
    }
}

int add_player(GameWorld *world, struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!world->players[i].active) {
            world->players[i].id = i;
            world->players[i].x = rand() % GRID_WIDTH;
            world->players[i].y = rand() % GRID_HEIGHT;
            world->players[i].active = 1;
            world->players[i].last_heartbeat = time(NULL);
            world->client_addrs[i] = *addr;
            world->player_count++;
            return i;
        }
    }
    return -1;
}

void remove_player(GameWorld *world, int player_id) {
    if (player_id >= 0 && player_id < MAX_PLAYERS && world->players[player_id].active) {
        world->players[player_id].active = 0;
        world->player_count--;
        memset(&world->client_addrs[player_id], 0, sizeof(struct sockaddr_in));
    }
}

void update_player_position(GameWorld *world, int player_id, uint8_t direction) {
    if (!world->players[player_id].active) return;
    
    switch(direction) {
        case 1: // up
            if (world->players[player_id].y > 0) 
                world->players[player_id].y--;
            break;
        case 2: // down
            if (world->players[player_id].y < GRID_HEIGHT - 1) 
                world->players[player_id].y++;
            break;
        case 3: // left
            if (world->players[player_id].x > 0) 
                world->players[player_id].x--;
            break;
        case 4: // right
            if (world->players[player_id].x < GRID_WIDTH - 1) 
                world->players[player_id].x++;
            break;
        default:
            break;
    }
    
    world->players[player_id].last_heartbeat = time(NULL);
}

void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // Hide cursor
    timeout(0);   // Non-blocking getch
    
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_RED, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_BLUE, COLOR_BLACK);
        init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(6, COLOR_CYAN, COLOR_BLACK);
    }
    
    game_win = newwin(GRID_HEIGHT + 2, GRID_WIDTH + 2, 1, 1);
    box(game_win, 0, 0);
    wrefresh(game_win);
    
    info_win = newwin(10, GRID_WIDTH + 2, GRID_HEIGHT + 3, 1);
    box(info_win, 0, 0);
    wrefresh(info_win);
}

void cleanup_ncurses() {
    if (game_win) delwin(game_win);
    if (info_win) delwin(info_win);
    endwin();
}

void render_game_ncurses(GameWorld *world, int local_player_id) {
    for (int y = 1; y <= GRID_HEIGHT; y++) {
        for (int x = 1; x <= GRID_WIDTH; x++) {
            mvwaddch(game_win, y, x, ' ');
        }
    }
    
    // Draw cats
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (world->players[p].active) {
            // Set color
            wattron(game_win, COLOR_PAIR((p % 6) + 1));
            mvwprintw(game_win, 
                      world->players[p].y + 1, 
                      world->players[p].x + 1, 
                      "%s", CAT);
            wattroff(game_win, COLOR_PAIR((p % 6) + 1));
        }
    }
    
    // Refresh game window
    wrefresh(game_win);
    
    // Clear info window content
    werase(info_win);
    box(info_win, 0, 0);
    
    // Draw player list
    mvwprintw(info_win, 1, 2, "Cats in the arena:");
    int line = 2;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (world->players[p].active) {
            wattron(info_win, COLOR_PAIR((p % 6) + 1));
            mvwprintw(info_win, line, 2, "Cat %d", p);
            wattroff(info_win, COLOR_PAIR((p % 6) + 1));
            
            if (p == local_player_id) {
                wprintw(info_win, " (YOU)");
            }
            wprintw(info_win, " at (%d,%d)", 
                    world->players[p].x, world->players[p].y);
            line++;
        }
    }
    
    mvwprintw(info_win, line + 1, 2, "Use WASD to move, Q to quit");
    
    // Refresh info window
    wrefresh(info_win);
}

int get_input_ncurses() {
    int ch = getch();
    switch(ch) {
        case 'w': case 'W': return 1;  // up
        case 's': case 'S': return 2;  // down
        case 'a': case 'A': return 3;  // left
        case 'd': case 'D': return 4;  // right
        case 'q': case 'Q': return -1; // quit
        default: return 0;
    }
}
