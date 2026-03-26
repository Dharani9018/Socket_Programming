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
uint32_t ping_send_time = 0;
uint32_t last_stats_print = 0;

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
        printf("Example: %s 127.0.0.1\n", argv[0]);
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
    
    clear();
    refresh();
    
    printf("Game started! Press WASD to move, Q to quit\n");
    printf("Packet Loss: 20%% simulated on server\n\n");
    
    // Initialize sequence tracking
    stats.last_sequence = 0;
    stats.packets_lost = 0;
    stats.packets_received = 0;
    stats.avg_latency_ms = 0;
    stats.jitter_ms = 0;
    stats.rtt_ms = 0;
    last_stats_print = get_time_ms();
    
    // For tracking last printed correction
    uint32_t last_correction_time = 0;
    int packet_loss_counter = 0;
    
    while (running) {
        uint32_t now = get_time_ms();
        
        // Send ping for RTT
        if (now - last_ping >= 1000) {
            uint8_t ping = MSG_PING;
            ping_send_time = now;
            send_packet_with_stats(&ping, 1);
            last_ping = now;
        }
        
        // Receive packets
        int len;
        while ((len = receive_packet(client_sock, buffer, sizeof(buffer), &from)) > 0) {
            if (buffer[0] == MSG_PONG) {
                uint32_t rtt = get_time_ms() - ping_send_time;
                update_network_stats(&stats, rtt / 2, 1, rtt);
            }
            else if ((size_t)len >= sizeof(GameSnapshot)) {
                GameSnapshot *snap = (GameSnapshot*)buffer;
                
                // PROPER PACKET LOSS DETECTION
                if (stats.last_sequence > 0) {
                    if (snap->sequence > stats.last_sequence + 1) {
                        int lost = snap->sequence - stats.last_sequence - 1;
                        stats.packets_lost += lost;
                        packet_loss_counter++;
                    }
                }
                stats.last_sequence = snap->sequence;
                stats.packets_received++;
                
                // Update loss rate
                uint32_t total = stats.packets_lost + stats.packets_received;
                if (total > 0) {
                    stats.packet_loss_rate = (float)stats.packets_lost / total * 100.0f;
                }
                
                uint32_t latency = now - snap->timestamp;
                update_network_stats(&stats, latency, 1, 0);
                
                // Apply snapshot directly
                memcpy(world.players, snap->players, sizeof(world.players));
                memcpy(world.coins, snap->coins, sizeof(world.coins));
                world.player_count = snap->player_count;
                
                // Server correction (only print if actually corrected and not too frequent)
                if (world.players[player_id].x != snap->players[player_id].x ||
                    world.players[player_id].y != snap->players[player_id].y) {
                    
                    if (now - last_correction_time > 500) {
                        // Save cursor position, print correction, restore
                        printf("\n🔄 CORRECTION: (%d,%d) -> (%d,%d)\n",
                               world.players[player_id].x,
                               world.players[player_id].y,
                               snap->players[player_id].x,
                               snap->players[player_id].y);
                        last_correction_time = now;
                    }
                }
                
                // Server correction
                world.players[player_id] = snap->players[player_id];
                
                has_snapshot = 1;
            }
        }
        
        measure_bandwidth(now);
        
        // Handle input
        int dir = 0;
        int ch = getch();
        
        if (ch != ERR) {
            switch(ch) {
                case 'w': case 'W': dir = 1; break;
                case 's': case 'S': dir = 2; break;
                case 'a': case 'A': dir = 3; break;
                case 'd': case 'D': dir = 4; break;
                case 'q': case 'Q': running = 0; break;
            }
        }
        
        if (dir > 0) {
            InputCommand cmd;
            cmd.player_id = (uint8_t)player_id;
            cmd.direction = (uint8_t)dir;
            cmd.sequence = input_sequence++;
            cmd.timestamp_ms = now;
            
            // Store for reconciliation
            if (input_buffer.count < MAX_PENDING_INPUTS) {
                input_buffer.inputs[input_buffer.count++] = cmd;
            }
            
            // CLIENT PREDICTION - move instantly
            update_player(&world, player_id, (uint8_t)dir);
            
            // Send to server
            uint8_t out[sizeof(cmd) + 1];
            out[0] = MSG_INPUT;
            memcpy(out + 1, &cmd, sizeof(cmd));
            send_packet_with_stats(out, sizeof(out));
        }
        
        // Display network stats on a single line (updates every 200ms)
        if (now - last_stats_print >= 200) {
            // Move cursor to bottom line, clear it, print stats
            int max_y, max_x;
            getmaxyx(stdscr, max_y, max_x);
            
            // Print stats line at the bottom of the screen
            attron(COLOR_PAIR(3));
            mvprintw(max_y - 1, 0, 
                     "Latency: %.0f ms | Jitter: %.0f ms | Loss: %.1f%% | RTT: %u ms | BW: %.0f KB/s     ",
                     stats.avg_latency_ms,
                     stats.jitter_ms,
                     stats.packet_loss_rate,
                     stats.rtt_ms,
                     stats.bandwidth_kbps);
            attroff(COLOR_PAIR(3));
            refresh();
            
            last_stats_print = now;
        }
        
        // Render game
        if (has_snapshot && (now - last_render >= 33)) {
            render_game(&world, player_id, &stats);
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
