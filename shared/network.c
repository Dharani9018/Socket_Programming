#include "network.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

uint32_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int init_server_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(sock);
        return -1;
    }
    
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    return sock;
}

int init_client_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    return sock;
}

void send_packet(int sock, struct sockaddr_in *addr, void *data, size_t len) {
    if (!addr) return;
    
    // Simulate 20% packet loss for testing
    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL));
        seeded = 1;
    }
    
    if (rand() % 100 < 20) {
        return; // Drop packet
    }
    
    sendto(sock, data, len, 0, (struct sockaddr *)addr, sizeof(*addr));
}

int receive_packet(int sock, void *buffer, size_t len, struct sockaddr_in *from) {
    socklen_t from_len = sizeof(*from);
    return recvfrom(sock, buffer, len, 0, (struct sockaddr *)from, &from_len);
}

// DTLS Initialization
int init_dtls_server(SSL_CTX **ctx, SSL **ssl, int sock) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    *ctx = SSL_CTX_new(DTLS_server_method());
    if (!*ctx) {
        fprintf(stderr, "SSL_CTX_new failed\n");
        return -1;
    }
    
    // Use self-signed certificate (for demo)
    SSL_CTX_use_certificate_file(*ctx, "server.crt", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(*ctx, "server.key", SSL_FILETYPE_PEM);
    
    *ssl = SSL_new(*ctx);
    if (!*ssl) {
        fprintf(stderr, "SSL_new failed\n");
        return -1;
    }
    
    SSL_set_fd(*ssl, sock);
    SSL_set_accept_state(*ssl);
    
    return 0;
}

int init_dtls_client(SSL_CTX **ctx, SSL **ssl, int sock) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    *ctx = SSL_CTX_new(DTLS_client_method());
    if (!*ctx) {
        fprintf(stderr, "SSL_CTX_new failed\n");
        return -1;
    }
    
    *ssl = SSL_new(*ctx);
    if (!*ssl) {
        fprintf(stderr, "SSL_new failed\n");
        return -1;
    }
    
    SSL_set_fd(*ssl, sock);
    SSL_set_connect_state(*ssl);
    
    return 0;
}

void cleanup_dtls(SSL_CTX *ctx, SSL *ssl) {
    if (ssl) SSL_free(ssl);
    if (ctx) SSL_CTX_free(ctx);
    EVP_cleanup();
}

void send_packet_dtls(SSL *ssl, void *data, size_t len) {
    if (ssl) {
        SSL_write(ssl, data, len);
    }
}

int receive_packet_dtls(SSL *ssl, void *buffer, size_t len) {
    if (ssl) {
        return SSL_read(ssl, buffer, len);
    }
    return -1;
}

void init_input_queue(InputQueue *q) {
    memset(q, 0, sizeof(*q));
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

void push_input(InputQueue *q, InputCommand cmd) {
    if (q->count < MAX_PENDING_INPUTS) {
        q->queue[q->tail] = cmd;
        q->tail = (q->tail + 1) % MAX_PENDING_INPUTS;
        q->count++;
    }
}

int pop_input(InputQueue *q, InputCommand *cmd) {
    if (q->count == 0) return 0;
    *cmd = q->queue[q->head];
    q->head = (q->head + 1) % MAX_PENDING_INPUTS;
    q->count--;
    return 1;
}

int has_inputs(InputQueue *q) {
    return q->count > 0;
}

void update_network_stats(NetworkStats *stats, uint32_t latency_ms, int received, uint32_t rtt, uint32_t sequence) {
    static uint32_t prev_latency = 0;
    static int sample_count = 0;
    static uint32_t last_sequence = 0;
    
    if (received) {
        if (sample_count == 0) {
            stats->avg_latency_ms = latency_ms;
        } else {
            stats->avg_latency_ms = stats->avg_latency_ms * 0.9f + latency_ms * 0.1f;
        }
        
        if (rtt > 0) {
            if (stats->rtt_ms == 0) {
                stats->rtt_ms = rtt;
            } else {
                stats->rtt_ms = stats->rtt_ms * 0.9f + rtt * 0.1f;
            }
        }
        
        if (latency_ms < stats->min_latency_ms || stats->min_latency_ms == 0)
            stats->min_latency_ms = latency_ms;
        if (latency_ms > stats->max_latency_ms)
            stats->max_latency_ms = latency_ms;
        
        if (sample_count > 0) {
            float diff = (float)abs((int)latency_ms - (int)prev_latency);
            stats->jitter_ms = stats->jitter_ms * 0.9f + diff * 0.1f;
        }
        prev_latency = latency_ms;
        
        if (last_sequence > 0 && sequence < last_sequence) {
            stats->packets_reordered++;
        }
        last_sequence = sequence;
        
        stats->packets_received++;
        sample_count++;
    } else {
        stats->packets_lost++;
    }
    
    uint32_t total = stats->packets_lost + stats->packets_received;
    if (total > 0) {
        stats->packet_loss_rate = (float)stats->packets_lost / total * 100.0f;
    }
}
