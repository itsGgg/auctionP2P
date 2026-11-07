#ifndef CRYPTO_H
#define CRYPTO_H

#include "../lib/format.h"

#include "../include/peer.h"
#include <openssl/evp.h>
#include <openssl/pem.h>

// ---------------Gestion des clés et certificats--------------------
int initialise_keys(peer_t *peer);
int generate_ED25519_keys(uint8_t **public_key, uint8_t **private_key, size_t *pub_len,
                          size_t *priv_len);
int init_peer_pkey(peer_t *peer, uint8_t *priv_key, size_t key_len);
int create_certificate(peer_t *peer);
int char_to_evp_pkey(uint8_t **key, size_t key_len, int is_private, EVP_PKEY **pkey);
int evp_pkey_to_char(EVP_PKEY *pkey, int is_private, uint8_t **buffer);

// --------- Gestion TLS et vérification ---------
int init_tls_context(peer_t *peer, int is_server);
int verification_callback(int preverify_ok, X509_STORE_CTX *ctx);
int verify_certificate(SSL *ssl, cle_pub_t *pub_key);

// --------- Opérations de signature ---------
int sign_message_with_key(EVP_PKEY *private_key, mess_t *message, sign_t *signature);
int verify_signature_with_key(EVP_PKEY *public_key, const mess_t *message,
                              const sign_t *signature);

// --------- Utilitaires d'affichage/formatage ---------
void print_signature_hex(const uint8_t *data, size_t length);
int print_x509_certificate(X509 *certificate);
char *x509_to_human_readable(X509 *cert);

#endif
