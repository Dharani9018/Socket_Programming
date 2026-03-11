#ifndef GAME_H
#define GAME_H

#include "network.h"
#include <ncurses.h>


#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define RESET   "\x1b[0m"

// Cat icon (nerd font)
#define CAT "^..^"

typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;
    uint32_t sequence;
    struct sockaddr_in client_addrs[MAX_PLAYERS];
} GameWorld;


extern WINDOW *game_win;
extern WINDOW *info_win;

void init_game_world(GameWorld *world);
int add_player(GameWorld *world, struct sockaddr_in *addr);
void remove_player(GameWorld *world, int player_id);
void update_player_position(GameWorld *world, int player_id, uint8_t direction);

// Ncurses rendering functions
void init_ncurses();
void cleanup_ncurses();
void render_game_ncurses(GameWorld *world, int local_player_id);
int get_input_ncurses();

#endif
