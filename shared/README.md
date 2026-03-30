## Common.h
### `TICK_RATE 30` : Simulation speed
- The server updates the gameworld 30 times per second
-  meaning every 33ms the gameworld is being updated
### `NETWORK_SEND_RATE 15` : how often client get updates
- Server sends snapshots(player positions, coins, Game state) to clients 15 times per second.
#### In most real games:
| System     | Typical Rate |
| ---------- | ------------ |
| Simulation | 30–128 Hz    |
| Network    | 10–30 Hz     |
##### `Avoids congestion, packet loss and Bandwidth waste` 

#### Server:
- High frequency → accuracy

#### Network:
- Lower frequency → efficiency

#### Client:
-  Fills gaps(prediction and reconcilliation) → smoothness
---
## game.h and game.c
### GameWorld = entire game state stored on the server, gets updated without the packet being sent.
- It contains:
  - All players
  - All coins
  - Network-related data
  - Input queues
### game.c:
- creates the world
- updates the world every tick
- handles gameplay logic (movement, scoring, respawn)
### GameWorld structure:
```C
typedef struct {
    Player players[MAX_PLAYERS]; //All players in the game
    Coin coins[MAX_COINS]; //All coins, each has position and respawn timer
    uint32_t sequence; //Global counter for snapshot: packet ordering and loss detection
    uint32_t last_update; //Time stamp of the last update: Helps track timing
    struct sockaddr_in client_addrs[MAX_PLAYERS]; //Store client's IP and Port to send the packet back
    int player_count; //Number of active players
    NetworkStats stats;
    InputQueue input_queues[MAX_PLAYERS]; //1 queue per player and stores input received from the network: critical for lag handling.
} GameWorld;
```


