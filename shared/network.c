#include "network.h"
#include <sys/socket.h> //socket(), sendto(), recvfrom().
#include <fcntl.h> //File control - for non-blocking mode.
#include <unistd.h> //system calls (close())
#include <stdio.h>
#include <string.h> //memset()
#include <stdlib.h>//rand(),srand()
#include <sys/time.h>//gettimeofday 
#include <time.h> //time().


uint32_t get_time_ms(void) 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000); //seconds to millseconds + microseconds to milliseconds.
}


int init_server_socket(void)  //Creates server UDP sockets.
{
    //1. Create UDP socket.
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) 
    { 
        perror("socket"); 
        return -1; 
    }
    //Allow reusing the port (so we can start server immediately).
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //setup server address structure.
    struct sockaddr_in addr = {0};
    //sin- Socket INternet.
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT); //host byte order to network byte order(follows big endian).
    addr.sin_addr.s_addr = INADDR_ANY; //Listen on all network interfaces(like wifi, ethernet, loopback).

    //bind the socket to the address. attach the socket to the port and ip address.

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        perror("bind"); 
        close(sock); 
        return -1;
    }

    int flags = fcntl(sock, F_GETFL, 0); //Check current socket flags.
    fcntl(sock, F_SETFL, flags | O_NONBLOCK); //Take the current flags, add the NONBLOCK flag, and set it back
    return sock; //return the file descriptor.
}

int init_client_socket(void) //Creating client UDP socket.
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    //client doesnt need to bind, the OS assigns the port automatically.
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    return sock;
}

void send_packet(int sock, struct sockaddr_in *addr, void *data, size_t len) 
{
    if (!addr) return; //can't send if invalid address.
    static int seeded = 0; //keeping the value between function calls.
    if (!seeded) 
    { 
        srand((unsigned)time(NULL)); //generate seed acc. to current time.
        seeded = 1; //always 1 until you come out of the program.
    }
    if (rand() % 100 < 20) //simulate network loss, if less than 20, drop the packet.
        return;
    //>20 send the packet.
    sendto(sock, data, len, 0, (struct sockaddr *)addr, sizeof(*addr));
}

//Receiving UDP packet:
int receive_packet(int sock, void *buffer, size_t len,
                   struct sockaddr_in *from) 
{
    socklen_t from_len = sizeof(*from);
    return (int)recvfrom(sock, buffer, len, 0,
                         (struct sockaddr *)from, &from_len);
}

//DTLS: datagram transport layer security (like SSL/TLS) but for UDP. (ssl + UDP)
//Creates DTLS context for server.
SSL_CTX *dtls_server_ctx(const char *cert_file, const char *key_file)
{
    OPENSSL_init_ssl(0, NULL); //initialize open ssl.

    SSL_CTX *ctx = SSL_CTX_new(DTLS_server_method()); //Create DTLS server context.
    if (!ctx) { ERR_print_errors_fp(stderr); return NULL; } 

    //use certificate and private key for encryption.
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx,  key_file,  SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }
   //Dont verify client certificates for simplicity. 
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

//Creates DTLS context for client.
SSL_CTX *dtls_client_ctx(void) {
    OPENSSL_init_ssl(0, NULL);

    SSL_CTX *ctx = SSL_CTX_new(DTLS_client_method());
    if (!ctx) { ERR_print_errors_fp(stderr); return NULL; }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

void dtls_ctx_free(SSL_CTX *ctx) {
    if (ctx) SSL_CTX_free(ctx);
}

//Establishing secure connection.
static SSL *make_dtls_session(SSL_CTX *ctx, int connected_sock,
                               struct sockaddr_in *peer, int is_server)
{//CREAte BIO, (open ssl's bio abstaction) for UDP.
    BIO *bio = BIO_new_dgram(connected_sock, BIO_NOCLOSE);
    if (!bio) { ERR_print_errors_fp(stderr); return NULL; }
    
    //tell openssl which peer to talk to.
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, peer);

    //set 5 second timeout for handshake.
    struct timeval tv = {5, 0};
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &tv);

    //Create SSL object.
    SSL *ssl = SSL_new(ctx);
    if (!ssl) { BIO_free(bio); return NULL; }
    SSL_set_bio(ssl, bio, bio); 

    //Perform handshake, (server:accept, client:connect).
    int r = is_server ? SSL_accept(ssl) : SSL_connect(ssl);
    if (r <= 0) {
        fprintf(stderr, "[DTLS] Handshake failed (%s side)\n",
                is_server ? "server" : "client");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);  
        return NULL;
    }

    printf("[DTLS] %s handshake complete - cipher: %s\n",
           is_server ? "Server" : "Client",
           SSL_get_cipher(ssl));
    return ssl;
}

SSL *dtls_server_session(SSL_CTX *ctx, int connected_sock,
                          struct sockaddr_in *peer) {
    return make_dtls_session(ctx, connected_sock, peer, 1);
}

SSL *dtls_client_session(SSL_CTX *ctx, int connected_sock,
                          struct sockaddr_in *server) {
    return make_dtls_session(ctx, connected_sock, server, 0);
}

//send encrypted data:
int secure_send(SSL *ssl, void *data, int len) 
{
    if (!ssl) return -1;
    int r = SSL_write(ssl, data, len); //Encrypt and send.
    if (r <= 0) {
        int err = SSL_get_error(ssl, r);
        if (err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ)
            fprintf(stderr, "[DTLS] SSL_write error %d\n", err);
    }
    return r;
}

//Encrypt and receive.
int secure_recv(SSL *ssl, void *buf, int len) 
{
    if (!ssl) return -1;
    int r = SSL_read(ssl, buf, len);
    if (r <= 0) {
        int err = SSL_get_error(ssl, r);
        if (err != SSL_ERROR_WANT_READ  &&
            err != SSL_ERROR_WANT_WRITE &&
            err != SSL_ERROR_ZERO_RETURN)
            fprintf(stderr, "[DTLS] SSL_read error %d\n", err);
    }
    return r;
}

void dtls_session_free(SSL *ssl) 
{
    if (!ssl) return;
    SSL_shutdown(ssl);
    SSL_free(ssl);
}

//Initialize empty queue.
void init_input_queue(InputQueue *q) {
    memset(q, 0, sizeof(*q));
}

//Add input to the queue.
void push_input(InputQueue *q, InputCommand cmd)
{
    if (q->count >= MAX_PENDING_INPUTS) return; 
    q->queue[q->tail] = cmd;
    q->tail = (q->tail + 1) % MAX_PENDING_INPUTS;
    q->count++;
}

//pop input from the queue.
int pop_input(InputQueue *q, InputCommand *cmd) {
    if (q->count == 0) return 0;
    *cmd    = q->queue[q->head];
    q->head = (q->head + 1) % MAX_PENDING_INPUTS;
    q->count--;
    return 1;
}

//check if the queue is empty.
int has_inputs(InputQueue *q) 
{
    return q->count > 0;
}
