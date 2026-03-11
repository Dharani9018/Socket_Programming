// client.c
#include "network.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>

int client_sock;
int player_id = -1;
int running = 1;
GameWorld local_world;
struct sockaddr_in server_addr;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        printf("Example: %s 127.0.0.1\n", argv[0]);
        return 1;
    }
    
   
    client_sock = init_client_socket();
    if (client_sock < 0) return 1;
    
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        printf("Invalid IP address: %s\n", argv[1]);
        close(client_sock);
        return 1;
    }
    
    printf("Connecting to server at %s:%d...\n", argv[1], PORT);
    
    
    uint8_t join_msg = MSG_JOIN;
    send_packet(client_sock, &server_addr, &join_msg, 1);
    
   
    struct sockaddr_in from;
    char buffer[BUFFER_SIZE];
    time_t start = time(NULL);
    
    while (player_id == -1 && (time(NULL) - start) < 5) {
        int len = receive_packet(client_sock, buffer, sizeof(buffer), &from);
        if (len >= 2 && buffer[0] == MSG_JOIN) {
            player_id = buffer[1];
            printf("Connected as Cat %d!\n", player_id);
            break;
        }
        usleep(100000);
    }
    
    if (player_id == -1) {
        printf("Failed to connect to server.\n");
        close(client_sock);
        return 1;
    }
    
    
    init_game_world(&local_world);
    
    
    init_ncurses();
    
    uint32_t last_sequence = 0;
    
    
    while (running) {
        
        int len = receive_packet(client_sock, buffer, sizeof(buffer), &from);
        if (len >= (int)sizeof(GameSnapshot)) {
            GameSnapshot *snap = (GameSnapshot*)buffer;
            if (snap->sequence != last_sequence) {
                memcpy(local_world.players, snap->players, sizeof(snap->players));
                local_world.player_count = snap->player_count;
                render_game_ncurses(&local_world, player_id);
                last_sequence = snap->sequence;
            }
        }
        
        
        int dir = get_input_ncurses();
        if (dir == -1) {  
            running = 0;
        } else if (dir > 0) {
            InputCommand cmd;
            cmd.player_id = player_id;
            cmd.direction = dir;
            cmd.sequence = 0;
            
            uint8_t out_buffer[sizeof(InputCommand) + 1];
            out_buffer[0] = MSG_INPUT;
            memcpy(out_buffer + 1, &cmd, sizeof(InputCommand));
            send_packet(client_sock, &server_addr, out_buffer, sizeof(out_buffer));
        }
        
        usleep(10000); 
    }
    
    
    uint8_t leave_msg[2] = {MSG_LEAVE, player_id};
    send_packet(client_sock, &server_addr, leave_msg, 2);
    
    
    cleanup_ncurses();
    close(client_sock);
    printf("\nCat %d left. Goodbye!\n", player_id);
    
    return 0;
}
