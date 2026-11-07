#ifndef NETWORK_TLS_H
#define NETWORK_TLS_H

#include "../lib/format.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/types.h>

struct peer_t;
// Configuration TLS
typedef struct tls_config_t tls_config_t;

struct tls_config_t {
    EVP_PKEY *pkey;
    X509 *certificate;
    SSL_CTX *ssl_ctx_client;
    SSL_CTX *ssl_ctx_server;
};

int upgrade_to_tls_client(struct peer_t *peer, int sock, SSL **tls_socket);
int upgrade_to_tls_server(struct peer_t *peer, int sock, SSL **tls_socket);
ssize_t tls_send_all(SSL *ssl, const uint8_t *buf, size_t len, const struct timeval *tv);
ssize_t tls_recv_all(SSL *ssl, uint8_t *buf, size_t len, const struct timeval *tv);
void tls_close(SSL *ssl);

void tls_config_free(tls_config_t *tls_config);

#endif
