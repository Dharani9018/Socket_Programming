#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <netinet/in.h>
#include "common.h"

typedef enum {
    MSG_JOIN = 1,
    MSG_LEAVE,
    MSG_INPUT,
    MSG_SNAPSHOT,
    MSG_PING,
    MSG_PONG
} MessageType;

typedef struct {
    uint8_t id;
    uint8_t x, y;
    uint8_t active;
    uint32_t score;
} Player;

typedef struct {
    uint8_t x, y;
    uint8_t active;
    uint32_t respawn_timer;
} Coin;

#pragma pack(push, 1)
typedef struct {
    uint8_t player_id;
    uint8_t direction;
    uint32_t sequence;
    uint32_t timestamp_ms;
} InputCommand;
#pragma pack(pop)

typedef struct {
    uint32_t sequence;
    uint32_t last_processed_input;
    uint32_t timestamp;
    uint8_t player_count;
    Player players[MAX_PLAYERS];
    Coin coins[MAX_COINS];
} GameSnapshot;

typedef struct {
    float avg_latency_ms;
    float min_latency_ms;
    float max_latency_ms;
    float packet_loss_rate;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;
    float jitter_ms;
    float bandwidth_kbps;
    uint32_t rtt_ms;
    uint32_t last_sequence;
} NetworkStats;

typedef struct {
    InputCommand queue[MAX_PENDING_INPUTS];
    int head;
    int tail;
    int count;
} InputQueue;

int init_server_socket();
int init_client_socket();
void send_packet(int sock, struct sockaddr_in *addr, void *data, size_t len);
int receive_packet(int sock, void *buffer, size_t len, struct sockaddr_in *from);
void update_network_stats(NetworkStats *stats, uint32_t latency_ms, int received, uint32_t rtt);
uint32_t get_time_ms(void);

void init_input_queue(InputQueue *q);
void push_input(InputQueue *q, InputCommand cmd);
int pop_input(InputQueue *q, InputCommand *cmd);

#endif
