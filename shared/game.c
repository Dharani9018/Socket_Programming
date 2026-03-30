#include "game.h"
#include <stdlib.h> //rand(), srand() -> generating pseudo random numbers
#include <string.h>//memset()-> memory set, fills a block of memory with a specific value:memset(pointer, value, number_of_bytes)
#include <time.h> //time()-> returns current time
#include <stdio.h>

void init_game_world(GameWorld *w) //set up the entire game.
{
    memset(w->players,      0, sizeof(w->players)); //clear all players
    memset(w->coins,        0, sizeof(w->coins));//clear all coins
    memset(w->client_addrs, 0, sizeof(w->client_addrs));//Clear all addresses
    w->player_count = 0; //no players yet
    w->sequence     = 0; //snapshot counter starts at zero.
    w->last_update  = get_time_ms(); //record current time
    memset(&w->stats, 0, sizeof(w->stats));//clear network stats.
    for (int i = 0; i < MAX_PLAYERS; i++) //create an empty input queue for each potential player
        init_input_queue(&w->input_queues[i]);
    srand((unsigned)time(NULL)); //seeding with current time.
    //time(NULL) -> gets current time in seconds.
    spawn_coins(w); //place coin randomly on the map.
}

void init_client_buffer(ClientInputBuffer *buffer)
{
    memset(buffer, 0, sizeof(*buffer));
}

int get_player_count(GameWorld *w) 
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (w->players[i].active) n++;
    return n;
}

int add_player(GameWorld *w, struct sockaddr_in *addr) 
{
    for (int i = 0; i < MAX_PLAYERS; i++) 
    {
        //find an empty player slot
        if (!w->players[i].active) 
        {
            w->players[i].active = 1; //mark active.
            w->players[i].id     = (uint8_t)i; //Assign id
            w->players[i].x      = (uint8_t)(rand() % GRID_WIDTH); //x coordinate
            w->players[i].y      = (uint8_t)(rand() % GRID_HEIGHT);//y coordinate
            w->players[i].score  = 0; //initial score.
            w->client_addrs[i]   = *addr; //Store client address (IP,PORT).
            w->player_count      = get_player_count(w); //Update player count
            init_input_queue(&w->input_queues[i]); //Cleat input queue
            printf("[ADD] Player %d spawned at (%d,%d)\n",
                   i, w->players[i].x, w->players[i].y);
            return i; //return player id
        }
    }
    return -1;  //No free slots.
}

void remove_player(GameWorld *w, int id) 
{
    if (id < 0 || id >= MAX_PLAYERS) return;
    w->players[id].active = 0; //make them inactive
    w->player_count       = get_player_count(w); //update player count
    init_input_queue(&w->input_queues[id]); // 
}

void spawn_coins(GameWorld *w) //places coins on the map.
{
    for (int i = 0; i < MAX_COINS; i++) 
    {
        w->coins[i].active        = 1;
        w->coins[i].x             = (uint8_t)(rand() % GRID_WIDTH);
        w->coins[i].y             = (uint8_t)(rand() % GRID_HEIGHT);
        w->coins[i].respawn_timer = 0;
    }
}

void check_coin_collision(GameWorld *w, int id) //what happens when the player picks up a coin.
{
    if (id < 0 || id >= MAX_PLAYERS || !w->players[id].active) return;

    uint32_t now = get_time_ms();  //get time in milliseconds.

    for (int i = 0; i < MAX_COINS; i++) 
    {
        if (w->coins[i].active) 
        {
            // Collection
            if (w->coins[i].x == w->players[id].x &&
                w->coins[i].y == w->players[id].y)  //check if coin's posn and player's posn is same.
            {
                w->coins[i].active        = 0; //remove the coin
                w->coins[i].respawn_timer = now + 1500; //generate a coin in next 1.5seconds
                w->players[id].score++; //increase the payer's score.
            }
        } 
        else 
        {
            //coin is inactive -> check if it should respawn.
            if (w->coins[i].respawn_timer > 0 && now >= w->coins[i].respawn_timer) 
            {
                w->coins[i].active        = 1;
                w->coins[i].x             = (uint8_t)(rand() % GRID_WIDTH);
                w->coins[i].y             = (uint8_t)(rand() % GRID_HEIGHT);
                w->coins[i].respawn_timer = 0;
            }
        }
    }
}

void update_player(GameWorld *w, int id, uint8_t d) 
{
    //d-> direction, 1-up, 2-down, 3=left, 4-right.
    if (id < 0 || id >= MAX_PLAYERS || !w->players[id].active) return;
    if (d == 1 && w->players[id].y > 0)               w->players[id].y--; //decrementing y goes up
    if (d == 2 && w->players[id].y < GRID_HEIGHT - 1) w->players[id].y++;
    if (d == 3 && w->players[id].x > 0)               w->players[id].x--;
    if (d == 4 && w->players[id].x < GRID_WIDTH  - 1) w->players[id].x++;
    check_coin_collision(w, id);
}

void simulate_fixed_tick(GameWorld *world) 
{
    //process all active players
    for (int i = 0; i < MAX_PLAYERS; i++) 
    {
        if (!world->players[i].active) continue; //skip inactive players.
        InputCommand cmd; 
        if (pop_input(&world->input_queues[i], &cmd)) //get the next input from the player.
            update_player(world, i, cmd.direction);
    }
}

void print_network_analysis(NetworkStats stats, int total_score) 
{
    printf("           GAME STATISTICS             \n");
    printf(" Total Score:           %7d          \n", total_score);
    printf("           NETWORK STATS               \n");
    printf(" Avg Latency:           %7.2f ms      \n", stats.avg_latency_ms);
    printf(" Jitter:                %7.2f ms      \n", stats.jitter_ms);
    printf(" RTT:                   %7u ms        \n", stats.rtt_ms);
    printf(" Packet Loss:           %7.2f%%       \n", stats.packet_loss_rate);
    printf(" Bandwidth:             %7.2f KB/s     \n", stats.bandwidth_kbps);
}
