CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lncurses

all: server client

server: server.c network.c game.c
	$(CC) $(CFLAGS) -o server server.c network.c game.c $(LDFLAGS)

client: client.c network.c game.c
	$(CC) $(CFLAGS) -o client client.c network.c game.c $(LDFLAGS)

clean:
	rm -f server client

.PHONY: all clean
