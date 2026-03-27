#include "../shared/game.h"
#include "../shared/network.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <raylib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    
    // Initialize window
    const int screenWidth = GRID_WIDTH * CELL_SIZE;
    const int screenHeight = GRID_HEIGHT * CELL_SIZE;
    InitWindow(screenWidth, screenHeight, "Cat Arena - Multiplayer Game");
    SetTargetFPS(60);
    
    // Network setup
    int sock = init_client_socket();
    if (sock < 0) {
        printf("Failed to create socket\n");
        return 1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    
    // Game state
    GameWorld world;
    ClientInputBuffer input_buffer;
    NetworkStats stats;
    init_game_world(&world);
    init_client_buffer(&input_buffer);
    
    // Connect to server
    uint8_t join_msg = MSG_JOIN;
    send_packet(sock, &server_addr, &join_msg, 1);
    
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from;
    uint32_t start = get_time_ms();
    int player_id = -1;
    
    while (player_id == -1 && (get_time_ms() - start) < 5000) {
        int len = receive_packet(sock, buffer, sizeof(buffer), &from);
        if (len >= 2 && buffer[0] == MSG_JOIN) {
            player_id = buffer[1];
            printf("Connected as Player %d!\n", player_id);
        }
        usleep(10000);
    }
    
    if (player_id == -1) {
        printf("Failed to connect\n");
        CloseWindow();
        return 1;
    }
    
    // Game loop variables
    uint32_t last_ping = 0;
    uint32_t input_sequence = 0;
    uint32_t expected_sequence = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    int has_snapshot = 0;
    
    // Stats for display
    stats.avg_latency_ms = 0;
    stats.jitter_ms = 0;
    stats.rtt_ms = 0;
    stats.bandwidth_kbps = 0;
    stats.packet_loss_rate = 0;
    
    while (!WindowShouldClose()) {
        uint32_t now = get_time_ms();
        
        // Send ping for RTT
        if (now - last_ping >= 1000) {
            uint8_t ping = MSG_PING;
            send_packet(sock, &server_addr, &ping, 1);
            last_ping = now;
        }
        
        // Receive packets
        int len;
        while ((len = receive_packet(sock, buffer, sizeof(buffer), &from)) > 0) {
            if (buffer[0] == MSG_PONG) {
                // RTT measurement - calculate latency
                uint32_t rtt = get_time_ms() - (now - 1000); // Approximate
                stats.rtt_ms = rtt;
                stats.avg_latency_ms = rtt / 2;
            }
            else if ((size_t)len >= sizeof(GameSnapshot)) {
                GameSnapshot *snap = (GameSnapshot*)buffer;
                
                // Loss detection
                if (expected_sequence > 0 && snap->sequence > expected_sequence) {
                    packets_lost += snap->sequence - expected_sequence;
                }
                expected_sequence = snap->sequence + 1;
                packets_received++;
                
                // Update loss rate
                float total = packets_received + packets_lost;
                if (total > 0) {
                    stats.packet_loss_rate = (packets_lost / total) * 100.0f;
                }
                
                // Update latency
                uint32_t latency = now - snap->timestamp;
                stats.avg_latency_ms = stats.avg_latency_ms * 0.9f + latency * 0.1f;
                
                // Apply snapshot
                memcpy(world.players, snap->players, sizeof(world.players));
                memcpy(world.coins, snap->coins, sizeof(world.coins));
                world.player_count = snap->player_count;
                
                // Server correction
                world.players[player_id] = snap->players[player_id];
                has_snapshot = 1;
            }
        }
        
        // Handle input
        int dir = 0;
        if (IsKeyPressed(KEY_W)) dir = 1;
        else if (IsKeyPressed(KEY_S)) dir = 2;
        else if (IsKeyPressed(KEY_A)) dir = 3;
        else if (IsKeyPressed(KEY_D)) dir = 4;
        else if (IsKeyPressed(KEY_ESCAPE)) break;
        
        if (dir > 0) {
            InputCommand cmd;
            cmd.player_id = player_id;
            cmd.direction = dir;
            cmd.sequence = input_sequence++;
            cmd.timestamp_ms = now;
            
            // Store for reconciliation
            if (input_buffer.count < MAX_PENDING_INPUTS) {
                input_buffer.inputs[input_buffer.count++] = cmd;
            }
            
            // Client prediction - move instantly
            update_player(&world, player_id, dir);
            
            // Send to server
            uint8_t out[sizeof(cmd) + 1];
            out[0] = MSG_INPUT;
            memcpy(out + 1, &cmd, sizeof(cmd));
            send_packet(sock, &server_addr, out, sizeof(out));
        }
        
        // Render
        BeginDrawing();
        ClearBackground(DARKGRAY);
        
        if (has_snapshot) {
            draw_game(&world, player_id, &stats);
        } else {
            DrawText("Connecting to server...", screenWidth/2 - 100, screenHeight/2, 20, WHITE);
        }
        
        EndDrawing();
        
        usleep(10000); // 10ms sleep
    }
    
    // Cleanup
    uint8_t leave_msg[2] = {MSG_LEAVE, player_id};
    send_packet(sock, &server_addr, leave_msg, 2);
    close(sock);
    CloseWindow();
    
    print_network_analysis(stats, world.players[player_id].score);
    
    return 0;
}
