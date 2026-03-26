#include "network.h"
#include "game.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

int client_sock;
int player_id = -1;
int running = 1;
GameWorld world;
ClientInputBuffer input_buffer;
NetworkStats stats;

struct sockaddr_in server_addr;
uint32_t bytes_sent = 0;
uint32_t last_bandwidth_update = 0;
uint32_t last_bytes_sent = 0;
int has_snapshot = 0;

// For RTT tracking
uint32_t ping_send_time = 0;
uint32_t last_ping_sequence = 0;

void measure_bandwidth(uint32_t now) {
    if (last_bandwidth_update == 0) {
        last_bandwidth_update = now;
        last_bytes_sent = bytes_sent;
        return;
    }
    
    uint32_t elapsed = now - last_bandwidth_update;
    if (elapsed >= 1000) {
        uint32_t bytes_diff = bytes_sent - last_bytes_sent;
        stats.bandwidth_kbps = (bytes_diff * 8.0f) / 1024.0f;
        last_bandwidth_update = now;
        last_bytes_sent = bytes_sent;
    }
}

void send_packet_with_stats(void *data, size_t len) {
    send_packet(client_sock, &server_addr, data, len);
    bytes_sent += len;
    stats.packets_sent++;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    
    client_sock = init_client_socket();
    if (client_sock < 0) return 1;
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    
    init_game_world(&world);
    init_client_buffer(&input_buffer);
    
    // Connect
    uint8_t join_msg = MSG_JOIN;
    send_packet_with_stats(&join_msg, 1);
    
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from;
    uint32_t start = get_time_ms();
    
    printf("Connecting to server at %s:%d...\n", argv[1], PORT);
    
    while (player_id == -1 && (get_time_ms() - start) < 5000) {
        int len = receive_packet(client_sock, buffer, sizeof(buffer), &from);
        if (len >= 2 && buffer[0] == MSG_JOIN) {
            player_id = buffer[1];
            printf("Connected as Player %d!\n", player_id);
        }
        usleep(10000);
    }
    
    if (player_id == -1) {
        printf("Failed to connect to server\n");
        return 1;
    }
    
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    timeout(0);
    
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_YELLOW, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_CYAN, COLOR_BLACK);
    }
    
    uint32_t last_ping = 0;
    uint32_t last_render = 0;
    uint32_t input_sequence = 0;
    uint32_t last_sequence_received = 0;
    
    // Clear screen initially
    clear();
    refresh();
    
    while (running) {
        uint32_t now = get_time_ms();
        
        // Send ping for RTT measurement
        if (now - last_ping >= 1000) {
            uint8_t ping[5];
            ping[0] = MSG_PING;
            ping_send_time = now;
            last_ping_sequence++;
            memcpy(ping + 1, &last_ping_sequence, 4);
            send_packet_with_stats(ping, 5);
            last_ping = now;
        }
        
        // Receive packets
        int len = receive_packet(client_sock, buffer, sizeof(buffer), &from);
        if (len > 0) {
            if (buffer[0] == MSG_PONG && len >= 5) {
                uint32_t rtt = get_time_ms() - ping_send_time;
                update_network_stats(&stats, rtt / 2, 1, rtt);
            }
            else if ((size_t)len >= sizeof(GameSnapshot)) {
                GameSnapshot *snapshot = (GameSnapshot*)buffer;
                
                // Check for packet loss
                if (last_sequence_received > 0 && 
                    snapshot->sequence > last_sequence_received + 1) {
                    // Lost packets detected
                    int lost = snapshot->sequence - last_sequence_received - 1;
                    for (int i = 0; i < lost; i++) {
                        update_network_stats(&stats, 0, 0, 0);
                    }
                }
                last_sequence_received = snapshot->sequence;
                
                uint32_t latency = now - snapshot->timestamp;
                update_network_stats(&stats, latency, 1, 0);
                
                // Add to jitter buffer
                add_to_jitter_buffer(&world, snapshot);
                process_jitter_buffer(&world);
                
                // Full sync with server
                memcpy(world.players, snapshot->players, sizeof(world.players));
                memcpy(world.coins, snapshot->coins, sizeof(world.coins));
                world.player_count = snapshot->player_count;
                
                // Reconcile with prediction buffer
                input_buffer.last_ack_sequence = snapshot->last_processed_input;
                
                // Remove acknowledged inputs
                int new_count = 0;
                for (int i = 0; i < input_buffer.count; i++) {
                    if (input_buffer.inputs[i].sequence > input_buffer.last_ack_sequence) {
                        input_buffer.inputs[new_count++] = input_buffer.inputs[i];
                    }
                }
                input_buffer.count = new_count;
                
                // Re-apply pending inputs (prediction correction)
                for (int i = 0; i < input_buffer.count; i++) {
                    update_player(&world, player_id, input_buffer.inputs[i].direction);
                }
                
                has_snapshot = 1;
            }
        }
        
        measure_bandwidth(now);
        
        // Handle input
        int dir = get_input();
        if (dir == -1) {
            running = 0;
            break;
        }
        
        if (dir > 0) {
            InputCommand cmd;
            cmd.player_id = (uint8_t)player_id;
            cmd.direction = (uint8_t)dir;
            cmd.sequence = input_sequence++;
            cmd.timestamp_ms = now;
            
            if (input_buffer.count < MAX_PENDING_INPUTS) {
                input_buffer.inputs[input_buffer.count++] = cmd;
            }
            
            // Predict movement locally
            update_player(&world, player_id, (uint8_t)dir);
            
            uint8_t out[sizeof(cmd) + 1];
            out[0] = MSG_INPUT;
            memcpy(out + 1, &cmd, sizeof(cmd));
            send_packet_with_stats(out, sizeof(out));
        }
        
        // Render with interpolation
        if (has_snapshot && (now - last_render >= 33)) {
            // Interpolate positions for smooth movement
            interpolate_positions(&world, 0.5f);
            render_game(&world, player_id, &stats);
            last_render = now;
        } else if (!has_snapshot && (now - last_render >= 1000)) {
            clear();
            mvprintw(GRID_HEIGHT/2, GRID_WIDTH/2 - 10, "Waiting for server...");
            refresh();
            last_render = now;
        }
        
        usleep(10000);
    }
    
    // Cleanup
    uint8_t leave_msg[2] = {MSG_LEAVE, (uint8_t)player_id};
    send_packet_with_stats(leave_msg, 2);
    
    endwin();
    close(client_sock);
    print_network_analysis(&stats);
    
    return 0;
}
