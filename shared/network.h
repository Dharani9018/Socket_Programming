#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "common.h"

typedef enum {
    MSG_JOIN     = 1,
    MSG_LEAVE    = 2,
    MSG_INPUT    = 3,
    MSG_SNAPSHOT = 4,
    MSG_PING     = 5,
    MSG_PONG     = 6
} MessageType;

typedef struct {
    uint8_t  id;
    uint8_t  x, y;
    uint8_t  active;
    uint32_t score;
} Player;

typedef struct {
    uint8_t  x, y;
    uint8_t  active;
    uint32_t respawn_timer;   
} Coin;

#pragma pack(push, 1)
typedef struct {
    uint8_t  player_id;
    uint8_t  direction;      
    uint32_t sequence;        
    uint32_t timestamp_ms;    
} InputCommand;
#pragma pack(pop)

typedef struct {
    uint32_t sequence;                          // server-side snapshot counter
    uint32_t last_processed_input[MAX_PLAYERS]; // per-player ack for reconciliation
    uint32_t timestamp;                         // server send time (ms)
    uint8_t  player_count;
    Player   players[MAX_PLAYERS];
    Coin     coins[MAX_COINS];
} GameSnapshot;

typedef struct {
    float    avg_latency_ms;
    float    min_latency_ms;
    float    max_latency_ms;
    float    packet_loss_rate;   // 0–100 %
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;
    float    jitter_ms;          // EWMA of |consecutive latency delta|
    float    bandwidth_kbps;     // incoming KB/s (1-second window)
    uint32_t rtt_ms;             // round-trip time via ping/pong
    uint32_t last_sequence;
    uint32_t packets_reordered;
} NetworkStats;

typedef struct {
    InputCommand queue[MAX_PENDING_INPUTS];
    int head, tail, count;
} InputQueue;

typedef struct {
    InputCommand inputs[MAX_PENDING_INPUTS];
    int          count;
    uint32_t     last_ack_sequence;
} ClientInputBuffer;

int  init_server_socket(void);
int  init_client_socket(void);

void send_packet(int sock, struct sockaddr_in *addr, void *data, size_t len);
int  receive_packet(int sock, void *buffer, size_t len, struct sockaddr_in *from);

SSL_CTX *dtls_server_ctx(const char *cert_file, const char *key_file);
SSL_CTX *dtls_client_ctx(void);
void     dtls_ctx_free(SSL_CTX *ctx);

SSL *dtls_server_session(SSL_CTX *ctx, int connected_sock,
                         struct sockaddr_in *peer);
SSL *dtls_client_session(SSL_CTX *ctx, int connected_sock,
                         struct sockaddr_in *server);

int  secure_send(SSL *ssl, void *data, int len);
int  secure_recv(SSL *ssl, void *buf,  int len);
void dtls_session_free(SSL *ssl);

void init_input_queue(InputQueue *q);
void push_input(InputQueue *q, InputCommand cmd);
int  pop_input(InputQueue *q, InputCommand *cmd);
int  has_inputs(InputQueue *q);

uint32_t get_time_ms(void);

#endif

