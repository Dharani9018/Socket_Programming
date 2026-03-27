CC = gcc
CFLAGS = -Wall -Wextra -O2 -I/usr/include/raylib -I/usr/include/openssl
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -lssl -lcrypto

# Executable names (not directories!)
SERVER_BIN = game_server
CLIENT_BIN = game_client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): server/server.o shared/game.o shared/network.o
	$(CC) server/server.o shared/game.o shared/network.o -o $(SERVER_BIN) $(LDFLAGS)

$(CLIENT_BIN): client/main.o shared/game.o shared/network.o
	$(CC) client/main.o shared/game.o shared/network.o -o $(CLIENT_BIN) $(LDFLAGS)

server/server.o: server/server.c shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c server/server.c -o server/server.o

client/main.o: client/main.c shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c client/main.c -o client/main.o

shared/game.o: shared/game.c shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c shared/game.c -o shared/game.o

shared/network.o: shared/network.c shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c shared/network.c -o shared/network.o

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -f server/*.o client/*.o shared/*.o

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN) 127.0.0.1

.PHONY: all clean run-server run-client
