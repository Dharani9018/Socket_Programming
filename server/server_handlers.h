#ifndef SERVER_HANDLERS_H
#define SERVER_HANDLERS_H

#include "../shared/game.h"
#include "../shared/network.h"

extern GameWorld  world;
extern int        server_sock;
extern int        dedicated_sock[MAX_PLAYERS];
extern SSL       *client_ssl[MAX_PLAYERS];
extern SSL_CTX   *dtls_ctx;
extern int        running;
extern uint32_t   last_processed_input[MAX_PLAYERS];
extern uint32_t   global_sequence;
extern uint32_t   packets_received;
extern uint32_t   packets_sent;

void broadcast_snapshot(void);

void recv_from_player(int id);

void handle_new_join(struct sockaddr_in *peer);

void set_nonblocking(int fd);

#endif
