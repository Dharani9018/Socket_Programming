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
uint32_t last_tick_time = 0;
uint32_t packets_received = 0;
uint32_t packets_sent = 0;
uint32_t last_stats_time = 0;
uint32_t last_processed_input[MAX_PLAYERS];
uint32_t global_sequence = 0;  // GLOBAL sequence counter for ALL snapshots
uint32_t last_full_broadcast = 0;

// Store previous positions to detect changes for TICK printing
Player previous_positions[MAX_PLAYERS];

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

void broadcast_snapshot() {
    GameSnapshot snapshot;
    
    snapshot.sequence = global_sequence++;
    snapshot.timestamp = get_time_ms();
    snapshot.player_count = world.player_count;
    
    memcpy(snapshot.players, world.players, sizeof(world.players));
    memcpy(snapshot.coins, world.coins, sizeof(world.coins));
    
    // Send to all active players (same snapshot to everyone)
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
    memset(previous_positions, 0, sizeof(previous_positions));
    last_stats_time = time(NULL);
    last_tick_time = get_time_ms();
    global_sequence = 0;
    
    printf("=== ARENA SERVER ===\n");
    printf("Port: %d | Simulation: %d Hz | Network: %d Hz\n", PORT, TICK_RATE, NETWORK_SEND_RATE);
    printf("Packet Loss Simulation: 20%%\n");
    printf("Global Sequence Numbers: ENABLED\n");
    printf("Waiting for players...\n\n");
    
    struct sockaddr_in client_addr;
    char buffer[BUFFER_SIZE];
    
    while (running) {
        uint32_t now = get_time_ms();
        
        // Process incoming packets
        int len;
        while ((len = receive_packet(server_sock, buffer, sizeof(buffer), &client_addr)) > 0) {
            packets_received++;
            uint8_t type = buffer[0];
            
            switch(type) {
                case MSG_JOIN: {
                    int id = add_player(&world, &client_addr);
                    if (id >= 0) {
                        uint8_t resp[2] = {MSG_JOIN, (uint8_t)id};
                        send_packet(server_sock, &client_addr, resp, 2);
                        packets_sent++;
                        printf("[JOIN] Player %d joined (Total: %d)\n", id, world.player_count);
                        // Initialize previous position for new player
                        previous_positions[id] = world.players[id];
                    }
                    break;
                }
                
                case MSG_INPUT: {
                    if ((size_t)len >= sizeof(InputCommand) + 1) {
                        InputCommand *cmd = (InputCommand*)(buffer + 1);
                        
                        if (cmd->player_id < MAX_PLAYERS && 
                            world.players[cmd->player_id].active) {
                            
                            // Store input in queue for tick-based processing
                            push_input(&world.input_queues[cmd->player_id], *cmd);
                        }
                    }
                    break;
                }
                
                case MSG_LEAVE: {
                    if (len >= 2) {
                        uint8_t id = buffer[1];
                        remove_player(&world, id);
                        printf("[LEAVE] Player %d left (Total: %d)\n", id, world.player_count);
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
        
        // Fixed tick simulation (30Hz)
        if (now - last_tick_time >= (1000 / TICK_RATE)) {
            // Process all queued inputs
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!world.players[i].active) continue;
                
                InputCommand cmd;
                while (pop_input(&world.input_queues[i], &cmd)) {
                    update_player(&world, i, cmd.direction);
                    // Track last processed input for ACK
                    if (cmd.sequence > last_processed_input[i]) {
                        last_processed_input[i] = cmd.sequence;
                    }
                }
            }
            last_tick_time = now;
            
            // Only print TICK when positions change (reduces spam)
            int changed = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (world.players[i].active) {
                    if (world.players[i].x != previous_positions[i].x ||
                        world.players[i].y != previous_positions[i].y) {
                        changed = 1;
                        break;
                    }
                }
            }
            
            if (changed) {
                printf("TICK: ");
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (world.players[i].active) {
                        printf("[P%d x=%d y=%d] ", i, world.players[i].x, world.players[i].y);
                        previous_positions[i] = world.players[i];
                    }
                }
                printf("\n");
            }
        }
        
        // Broadcast snapshots (15Hz)
        if (now - last_broadcast >= (1000 / NETWORK_SEND_RATE)) {
            broadcast_snapshot();
            last_broadcast = now;
        }
        
        // Print server stats every second
        uint32_t now_sec = time(NULL);
        if (now_sec != last_stats_time) {
            printf("[STATS] RX: %4u | TX: %4u | Players: %d | Global Seq: %u\n",
                   packets_received, packets_sent, world.player_count, global_sequence);
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
