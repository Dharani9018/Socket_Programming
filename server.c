#include "network.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>

GameWorld world;
int server_sock;
int running = 1;

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

void broadcast_snapshot() {
    GameSnapshot snapshot;
    snapshot.sequence = world.sequence++;
    snapshot.timestamp = time(NULL);
    snapshot.player_count = world.player_count;
    memcpy(snapshot.players, world.players, sizeof(world.players));
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (world.players[i].active) {
            send_packet(server_sock, &world.client_addrs[i], &snapshot, sizeof(snapshot));
        }
    }
}

int main() 
{
    signal(SIGINT, handle_signal);
    
    srand(time(NULL));
    
    server_sock = init_server_socket();
    if (server_sock < 0) return 1;
    
    init_game_world(&world);
    printf("Cat Arena Server started on port %d\n", PORT);
    printf("Waiting for cats to join...\n");
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    char ip_str[INET_ADDRSTRLEN];
    
    while (running) {
        memset(&client_addr, 0, sizeof(client_addr));
        int len = recvfrom(server_sock, buffer, sizeof(buffer), 0, 
                          (struct sockaddr *)&client_addr, &addr_len);
        
        if (len > 0) {
            uint8_t msg_type = buffer[0];
            
            switch(msg_type) {
                case MSG_JOIN: {
                    int player_id = add_player(&world, &client_addr);
                    if (player_id >= 0) {
                        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
                        printf("Cat %d joined from %s:%d\n", 
                               player_id, ip_str, ntohs(client_addr.sin_port));
                        
                        uint8_t response[2] = {MSG_JOIN, player_id};
                        send_packet(server_sock, &client_addr, response, 2);
                    }
                    break;
                }
                
                case MSG_INPUT: {
                    if (len > 0 && (size_t)len >= sizeof(InputCommand) + 1) {
                        InputCommand *cmd = (InputCommand*)(buffer + 1);
                        if (cmd->player_id < MAX_PLAYERS) {
                            update_player_position(&world, cmd->player_id, cmd->direction);
                        }
                    }
                    break;
                }
                
                case MSG_LEAVE: {
                    if (len >= 2) {
                        uint8_t player_id = buffer[1];
                        if (player_id < MAX_PLAYERS && world.players[player_id].active) {
                            remove_player(&world, player_id);
                            printf("Cat %d left\n", player_id);
                        }
                    }
                    break;
                }
            }
        }
        
        broadcast_snapshot();
        usleep(1000000 / TICK_RATE);
    }
    
    close(server_sock);
    printf("\nServer shutting down...\n");
    return 0;
}
