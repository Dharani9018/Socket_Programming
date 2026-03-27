#ifndef COMMON_H
#define COMMON_H

#define MAX_PLAYERS 4
#define MAX_COINS 15
#define TICK_RATE 30
#define NETWORK_SEND_RATE 15
#define PORT 8080
#define BUFFER_SIZE 2048
#define MAX_PENDING_INPUTS 256

// Game world dimensions
#define GRID_WIDTH 40
#define GRID_HEIGHT 30
#define CELL_SIZE 20

extern int GRID_WIDTH_PX;
extern int GRID_HEIGHT_PX;

#endif
