## Structures
### Player Structure
```C
typedef struct
{
  uint8_t id; //player id
  uint8_t x,y; //player position
  uint8_t active; //is the player in the game
} Player;
```
### GameWorld structure
```C
typedef struct {
    Player players[4];           // All players in game
    int player_count;             // Number of active players
    struct sockaddr_in client_addrs[4];  // Network addresses of clients
} GameWorld;
```
---
## Networking utility module `network.h` and `network.c`
### `sys/socket.h`
#### `socket()`
```C
socket(AF_INET,SOCK_DGRAM,0);
```
##### `AF_INET`: IPv4 Internet Protocol
##### `SOCK_DGRAM`: Socket type
##### `0`=> Specific protocol implementation, 0-> default protocol
##### Returns a file descriptor
##### After this the socket is created but not yet attached to a Port
---
#### `setsockopt()`
##### Configure socket options
```C
setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
```
##### `sock`: file descriptor
##### `SOL_SOCKET`: Specifies the Layer where the operations happen.
SOL_SOCKET → socket layer
IPPROTO_TCP → TCP layer
IPPROTO_IP  → IP layer
##### `SO_REUSEADDR`:
##### `opt=1`=> Enable options
#### bind()
```C
bind(sock,(struct sockaddr *)&addr, sizeof(addr));
```
##### Assigns a local address and port to the socket.
##### `sock`: Socket descriptor returned by `socket()`
##### bind() expects: `struct sockaddr *` so cast it `(struct sockaddr *)&addr`
##### `struct sockaddr`: a generic structure
```C
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
}
```
##### `struct sockaddr_in`: specific IPv4 version
struct sockaddr_in {
    short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
}
##### Kernel only reads:
```
family
port
addresses
```
```C
addr.sin_family = AF_INET; //Use IPv4
addr.sin_port = htons(PORT); //Use this Port
addr.sin_addr.s_addr = INADDR_ANY; //Accept packets from any interface.
```
##### `addr.sin_port = htons(PORT);`: Convert Host byte-order to network-byte order
##### The network uses Big-Endian.

#### `fcntl():` File control: Modify file descriptor behaviour.
```C
int flags = fcntl(sock, F_GETFL, 0);
fcntl(sock, F_SETFL, flags | O_NONBLOCK);
```
##### `fcntl(sock, F_GETFL, 0)`: Get current flags.
```
O_RDWR
O_NONBLOCK
O_APPEND
```
##### `fcntl(sock, F_SETFL, flags | O_NONBLOCK)`: Set flags + enable non Blocking

##### Non blocking: recvfrom() (no packet → return immediately)
---
#### `sendto`: sendto(sock, data, len, 0, (struct sockaddr *)addr, sizeof(*addr));
##### sock: file descriptor
##### data: pointer to the data
##### flags: Usually 0
##### `addr`: destination address
---
#### `recvfrom:` recvfrom(sock, buffer, len, 0, (struct sockaddr *)from, &from_len);
##### buffer: where data will be stored.
##### len: Max bytes to recieve.
##### from: sender's address.
#### `close(sock)`: closes the socket and releases the port.

## Server:
### Maintains the game world
### Receives player messages
### Updates State
### Brocasts snapshot of the world to all the players

## Program flow:
### Server 
```C
socket()
setsockopt()
bind()
fcntl(nonblocking)

loop:
    recvfrom()
    sendto()
```

### Client:
```C
socket()
fcntl(nonblocking)

sendto()
recvfrom()
```



