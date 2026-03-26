#include "game.h"
#include "network.h"
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

GameWorld world;
int server_sock;
int running = 1;
uint32_t last_broadcast = 0;

// Server stats
uint32_t packets_received = 0;
uint32_t packets_sent = 0;
uint32_t last_stats_time = 0;

// Track last processed input per player
uint32_t last_processed_input[MAX_PLAYERS];

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

void broadcast_snapshot() {
    GameSnapshot snapshot;
    snapshot.sequence = world.sequence++;
    snapshot.timestamp = get_time_ms();
    snapshot.player_count = world.player_count;
    
    memcpy(snapshot.players, world.players, sizeof(world.players));
    memcpy(snapshot.coins, world.coins, sizeof(world.coins));
    
    // Send to all active players with their last processed input
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (world.players[i].active) {
            snapshot.last_processed_input = last_processed_input[i];
            send_packet(server_sock, &world.client_addrs[i], &snapshot, sizeof(snapshot));
            packets_sent++;
        }
    }
}

int main() {
    signal(SIGINT, handle_signal);
    
    server_sock = init_server_socket();
    if (server_sock < 0) return 1;
    
    init_game_world(&world);
    memset(last_processed_input, 0, sizeof(last_processed_input));
    last_stats_time = time(NULL);
    
    printf("=== CAT ARENA SERVER ===\n");
    printf("Port: %d | Tick Rate: %d Hz | Max Players: %d\n", PORT, TICK_RATE, MAX_PLAYERS);
    printf("Server running (headless mode)\n");
    printf("Waiting for players...\n\n");
    
    struct sockaddr_in client_addr;
    char buffer[BUFFER_SIZE];
    
    while (running) {
        uint32_t now = get_time_ms();
        
        // Receive packets
        int len = receive_packet(server_sock, buffer, sizeof(buffer), &client_addr);
        if (len > 0) {
            packets_received++;
            uint8_t type = buffer[0];
            
            switch(type) {
                case MSG_JOIN: {
                    int id = add_player(&world, &client_addr);
                    if (id >= 0) {
                        uint8_t resp[2] = {MSG_JOIN, (uint8_t)id};
                        send_packet(server_sock, &client_addr, resp, 2);
                        packets_sent++;
                        printf("[JOIN] Player %d joined (Total: %d)\n", 
                               id, world.player_count);
                    }
                    break;
                }
                
                case MSG_INPUT: {
                    if (len > 0 && (size_t)len >= sizeof(InputCommand) + 1) {
                        InputCommand *cmd = (InputCommand*)(buffer + 1);
                        if (cmd->player_id < MAX_PLAYERS && 
                            world.players[cmd->player_id].active) {
                            
                            // Update last processed input for this player
                            if (cmd->sequence > last_processed_input[cmd->player_id]) {
                                last_processed_input[cmd->player_id] = cmd->sequence;
                            }
                            
                            // Apply movement
                            update_player(&world, cmd->player_id, cmd->direction);
                        }
                    }
                    break;
                }
                
                case MSG_LEAVE: {
                    if (len >= 2) {
                        uint8_t id = buffer[1];
                        remove_player(&world, id);
                        last_processed_input[id] = 0;
                        printf("[LEAVE] Player %d left (Total: %d)\n", 
                               id, world.player_count);
                    }
                    break;
                }
                
                case MSG_PING: {
                    uint8_t pong = MSG_PONG;
                    send_packet(server_sock, &client_addr, &pong, 1);
                    packets_sent++;
                    break;
                }
            }
        }
        
        // Broadcast at fixed rate
        if (now - last_broadcast >= (1000 / TICK_RATE)) {
            broadcast_snapshot();
            last_broadcast = now;
        }
        
        // Print server stats every second
        uint32_t now_sec = time(NULL);
        if (now_sec != last_stats_time) {
            printf("[STATS] RX: %4u | TX: %4u | Players: %d | Coins: %d\n",
                   packets_received, packets_sent, world.player_count, MAX_COINS);
            packets_received = 0;
            packets_sent = 0;
            last_stats_time = now_sec;
        }
        
        usleep(1000);
    }
    
    close(server_sock);
    printf("\n[SERVER] Shutting down...\n");
    return 0;
}
