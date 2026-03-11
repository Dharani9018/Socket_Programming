#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_PLAYERS 4
#define BUFFER_SIZE 1024
#define GRID_WIDTH 40
#define GRID_HEIGHT 20
#define TICK_RATE 30

typedef enum {
    MSG_JOIN = 1,
    MSG_LEAVE,
    MSG_INPUT,
    MSG_SNAPSHOT,
    MSG_PLAYER_LIST
} MessageType;

typedef struct {
    uint8_t id;
    uint8_t x;
    uint8_t y;
    uint8_t active;
    uint32_t last_heartbeat;
} Player;

typedef struct {
    uint8_t player_id;
    uint8_t direction;
    uint32_t sequence;
} InputCommand;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp;
    uint8_t player_count;
    Player players[MAX_PLAYERS];
} GameSnapshot;

int init_server_socket();
int init_client_socket();
void send_packet(int sock, struct sockaddr_in *addr, void *data, size_t len);
int receive_packet(int sock, void *buffer, size_t len, struct sockaddr_in *from);

#endif
