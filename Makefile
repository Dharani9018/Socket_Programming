CC=gcc
CFLAGS=-Wall -Wextra -O2
LDFLAGS=-lncurses -lm

all: server client

server: server.o game.o network.o
	$(CC) server.o game.o network.o -o server $(LDFLAGS)

client: client.o game.o network.o
	$(CC) client.o game.o network.o -o client $(LDFLAGS)

server.o: server.c game.h network.h common.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c game.h network.h common.h
	$(CC) $(CFLAGS) -c client.c

game.o: game.c game.h network.h common.h
	$(CC) $(CFLAGS) -c game.c

network.o: network.c network.h common.h
	$(CC) $(CFLAGS) -c network.c

clean:
	rm -f *.o server client

run-server: server
	./server

run-client: client
	./client 127.0.0.1

.PHONY: all clean run-server run-client
