#include "network.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>


uint32_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}


int init_server_socket(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return -1;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    return sock;
}

int init_client_socket(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    return sock;
}

void send_packet(int sock, struct sockaddr_in *addr, void *data, size_t len) {
    if (!addr) return;
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
    if (rand() % 100 < 20) return;
    sendto(sock, data, len, 0, (struct sockaddr *)addr, sizeof(*addr));
}

int receive_packet(int sock, void *buffer, size_t len,
                   struct sockaddr_in *from) {
    socklen_t from_len = sizeof(*from);
    return (int)recvfrom(sock, buffer, len, 0,
                         (struct sockaddr *)from, &from_len);
}


SSL_CTX *dtls_server_ctx(const char *cert_file, const char *key_file) {
    OPENSSL_init_ssl(0, NULL);

    SSL_CTX *ctx = SSL_CTX_new(DTLS_server_method());
    if (!ctx) { ERR_print_errors_fp(stderr); return NULL; }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx,  key_file,  SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

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


static SSL *make_dtls_session(SSL_CTX *ctx, int connected_sock,
                               struct sockaddr_in *peer, int is_server) {
    BIO *bio = BIO_new_dgram(connected_sock, BIO_NOCLOSE);
    if (!bio) { ERR_print_errors_fp(stderr); return NULL; }

    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, peer);

    struct timeval tv = {5, 0};
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &tv);

    SSL *ssl = SSL_new(ctx);
    if (!ssl) { BIO_free(bio); return NULL; }
    SSL_set_bio(ssl, bio, bio); 

    int r = is_server ? SSL_accept(ssl) : SSL_connect(ssl);
    if (r <= 0) {
        fprintf(stderr, "[DTLS] Handshake failed (%s side)\n",
                is_server ? "server" : "client");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);  
        return NULL;
    }

    printf("[DTLS] %s handshake complete — cipher: %s\n",
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


int secure_send(SSL *ssl, void *data, int len) {
    if (!ssl) return -1;
    int r = SSL_write(ssl, data, len);
    if (r <= 0) {
        int err = SSL_get_error(ssl, r);
        if (err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ)
            fprintf(stderr, "[DTLS] SSL_write error %d\n", err);
    }
    return r;
}

int secure_recv(SSL *ssl, void *buf, int len) {
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

void dtls_session_free(SSL *ssl) {
    if (!ssl) return;
    SSL_shutdown(ssl);
    SSL_free(ssl);
}


void init_input_queue(InputQueue *q) {
    memset(q, 0, sizeof(*q));
}

void push_input(InputQueue *q, InputCommand cmd) {
    if (q->count >= MAX_PENDING_INPUTS) return; 
    q->queue[q->tail] = cmd;
    q->tail = (q->tail + 1) % MAX_PENDING_INPUTS;
    q->count++;
}

int pop_input(InputQueue *q, InputCommand *cmd) {
    if (q->count == 0) return 0;
    *cmd    = q->queue[q->head];
    q->head = (q->head + 1) % MAX_PENDING_INPUTS;
    q->count--;
    return 1;
}

int has_inputs(InputQueue *q) {
    return q->count > 0;
}
