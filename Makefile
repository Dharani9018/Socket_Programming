CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -lssl -lcrypto

SERVER_BIN = game_server
CLIENT_BIN = game_client

SHARED_SRC = shared/network.c shared/game.c
SHARED_OBJ = shared/network.o shared/game.o

SERVER_SRC = server/server_main.c server/server_handlers.c
SERVER_OBJ = server/server_main.o server/server_handlers.o

CLIENT_SRC = client/client_main.c client/client_render.c
CLIENT_OBJ = client/client_main.o client/client_render.o



all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJ) $(SHARED_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJ) $(SHARED_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)



shared/network.o: shared/network.c shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@

shared/game.o: shared/game.c shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@



server/server_main.o: server/server_main.c server/server_handlers.h \
                      shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@

server/server_handlers.o: server/server_handlers.c server/server_handlers.h \
                           shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@


client/client_main.o: client/client_main.c client/client_render.h \
                      shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@

client/client_render.o: client/client_render.c client/client_render.h \
                        shared/game.h shared/network.h shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@



certs:
	openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
	  -days 365 -nodes -subj "/CN=cat-arena"
	@echo ""
	@echo "cert.pem and key.pem generated — run ./game_server from this directory"

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -f shared/*.o server/*.o client/*.o

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN) 127.0.0.1

.PHONY: all clean certs run-server run-client
