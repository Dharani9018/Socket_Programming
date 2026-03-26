#include "network.h"
#include "game.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

int client_sock;
int player_id = -1; //no players assigned
int running = 1; //start with game running
GameWorld world;
ClientInputBuffer input_buffer; //stores INPUTS (key presses) that haven't been confirmed by the server yet.
NetworkStats stats; //stores latency, jitter, loss, bandwidth stats

struct sockaddr_in server_addr; //server's IP address and port
uint32_t bytes_sent = 0; //total bytes sent (running total)
uint32_t last_bandwidth_update = 0; //last time bandwidth was calculated
uint32_t last_bytes_sent = 0; //bytes at last checkpoint
int has_snapshot = 0; //whether we've received first game state
uint32_t ping_send_time = 0; //when last ping was sent
uint32_t last_stats_print = 0; //when stats were last printed

// Dedicated stats window
WINDOW *stats_win = NULL;

void measure_bandwidth(uint32_t now)
{
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
    
    // Connection loop - wait up to 5 seconds for JOIN ACK
    while (player_id == -1 && (get_time_ms() - start) < 5000)  
    {
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
        init_pair(1, COLOR_YELLOW, COLOR_BLACK); //coins
        init_pair(2, COLOR_GREEN, COLOR_BLACK);  //local player
        init_pair(3, COLOR_CYAN, COLOR_BLACK);   //other players
    }
    
    uint32_t last_ping = 0;
    uint32_t last_render = 0;
    uint32_t input_sequence = 0;
    
    clear();
    refresh();
    
    // Create dedicated stats window at the bottom
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    stats_win = newwin(1, max_x, max_y - 1, 0);
    wrefresh(stats_win);
    
    // Initialize stats
    stats.last_sequence = 0;
    stats.packets_lost = 0;
    stats.packets_received = 0;
    stats.avg_latency_ms = 0;
    stats.jitter_ms = 0;
    stats.rtt_ms = 0;
    
    last_stats_print = get_time_ms();
    
    while (running) {
        uint32_t now = get_time_ms();
        
        // Send ping for RTT measurement (every 1 second)
        if (now - last_ping >= 1000) {
            uint8_t ping = MSG_PING;
            ping_send_time = now;
            send_packet_with_stats(&ping, 1);
            last_ping = now;
        }
        
        // Receive all pending packets
        int len;
        while ((len = receive_packet(client_sock, buffer, sizeof(buffer), &from)) > 0) {
            if (buffer[0] == MSG_PONG) {
                uint32_t rtt = get_time_ms() - ping_send_time;
                update_network_stats(&stats, rtt / 2, 1, rtt);
            }
            else if ((size_t)len >= sizeof(GameSnapshot)) {
                GameSnapshot *snap = (GameSnapshot*)buffer;
                
                // Use global sequence number from server
                uint32_t global_seq = snap->sequence;
                
                // Track packet loss (silently - for final stats only)
                if (stats.last_sequence > 0) {
                    if (global_seq > stats.last_sequence + 1) {
                        int lost = global_seq - stats.last_sequence - 1;
                        stats.packets_lost += lost;
                    }
                }
                
                stats.last_sequence = global_seq;
                stats.packets_received++;
                
                // Calculate latency
                uint32_t latency = now - snap->timestamp;
                update_network_stats(&stats, latency, 1, 0);
                
                // Apply snapshot (full state sync)
                memcpy(world.players, snap->players, sizeof(world.players));
                memcpy(world.coins, snap->coins, sizeof(world.coins));
                world.player_count = snap->player_count;
                
                // Server correction
                world.players[player_id] = snap->players[player_id];
                
                has_snapshot = 1;
            }
        }
        
        measure_bandwidth(now);
        
        // Handle input (WASD movement)
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
            
            // CLIENT PREDICTION - move instantly (no waiting)
            update_player(&world, player_id, (uint8_t)dir);
            
            // Send to server
            uint8_t out[sizeof(cmd) + 1];
            out[0] = MSG_INPUT;
            memcpy(out + 1, &cmd, sizeof(cmd));
            send_packet_with_stats(out, sizeof(out));
        }
        
        // Update stats window - NO LOSS DISPLAYED
        if (now - last_stats_print >= 200) {
            werase(stats_win);
            wattron(stats_win, COLOR_PAIR(3));
            mvwprintw(stats_win, 0, 0,
                      "Latency: %.0f ms | Jitter: %.0f ms | RTT: %u ms | BW: %.0f KB/s",
                      stats.avg_latency_ms,
                      stats.jitter_ms,
                      stats.rtt_ms,
                      stats.bandwidth_kbps);
            wattroff(stats_win, COLOR_PAIR(3));
            wrefresh(stats_win);
            last_stats_print = now;
        }
        
        // Render game (30 FPS)
        if (has_snapshot && (now - last_render >= 33)) {
            render_game(&world, player_id);
            last_render = now;
        }
        
        usleep(10000);
    }
    
    // Cleanup
    uint8_t leave_msg[2] = {MSG_LEAVE, (uint8_t)player_id};
    send_packet_with_stats(leave_msg, 2);
    
    if (stats_win) delwin(stats_win);
    endwin();
    close(client_sock);
    
    /*
    printf("\n\n");
    printf("        FINAL NETWORK STATS             \n");
    printf("Average Latency:        %7.2f ms      \n", stats.avg_latency_ms);
    printf("Average Jitter:         %7.2f ms      \n", stats.jitter_ms);
    printf("Average RTT:            %7u ms        \n", stats.rtt_ms);
    );
    */
    print_network_analysis(stats,world,player_id); 
    
    return 0;
}
