#ifndef FORMAT_H
#define FORMAT_H

#include "../lib/config.h"
#include <openssl/types.h>

#include <stdint.h>

// Types de base avec taille fixe
typedef uint8_t code_t;    // 1 octet
typedef uint16_t app_id_t; // 2 octets
typedef uint8_t lmess_t;   // 1 octet
typedef uint8_t lsign_t;   // 1 octet
typedef uint16_t port_t;   // 2 octets
typedef uint32_t numv_t;   // 4 octets
typedef uint32_t prix_t;   // 4 octets
typedef uint16_t nb_t;     // 2 octets

// Structures pour les composants des messages
typedef struct ip_t ip_t;
typedef struct cle_pub_t cle_pub_t;
typedef struct sign_t sign_t;
typedef struct mess_t mess_t;
typedef struct peer_signature_t peer_signature_t;
typedef struct info_t info_t;

typedef enum message_codes_t message_codes_t;

/**
 * Déclaration par structures pour copie par valeur facile,
 * Ne jamais ajouter de champs à ces structures (ip_t, cle_pub_t)
 * car elles sont utilisées avec sizeof(struct)
 */
struct ip_t {
    uint8_t addr[IPV6_ADDR_LEN];
};

/**
 * Déclaration par structures pour copie par valeur facile,
 * Ne jamais ajouter de champs à ces structures (ip_t, cle_pub_t)
 * car elles sont utilisées avec sizeof(struct)
 * @warning key Clé publique ED25519 brut sans \0 à la fin, utiliser memcpy pour copier
 */
struct cle_pub_t {
    uint8_t key[ED25519_PUB_KEY_LEN];
};
struct sign_t {
    uint8_t *data;  // Données de la signature
    lsign_t length; // Longueur de la signature
};

struct mess_t {
    uint8_t *data;  // Contenu du message
    lmess_t length; // Longueur du message
};

struct peer_signature_t {
    app_id_t id;
    sign_t signature;
};

struct info_t {
    app_id_t id;   // Identifiant du pair
    ip_t ip;       // Adresse IP
    port_t port;   // Port
    cle_pub_t key; // Clé publique
};

enum message_codes_t {
    CODE_VALIDATION = 1,
    CODE_CONSENSUS = 2,
    CODE_CONSENSUS_PART = 20,
    CODE_JOIN_REQUEST = 3,
    CODE_JOIN_RESPONSE = 4,
    CODE_PEER_INFO = 5,
    CODE_PEER_INFO_RESP_OK = 50,
    CODE_PEER_INFO_RESP_ALT = 51,
    CODE_NEW_PEER_INFO = 6,
    CODE_SYSTEM_INFO = 7,
    CODE_AUCTION_START = 8,
    CODE_AUCTION_BID = 9,
    CODE_AUCTION_BID_CONFIRM = 10,
    CODE_AUCTION_ALMOST_END = 11,
    CODE_AUCTION_END = 12,
    CODE_SYSTEM_LEAVE = 13,
    CODE_AUCTION_REJECT_CONCURRENT = 14,
    CODE_AUCTION_REJECT_INVALID = 15,
    CODE_SUPERVISOR_AFK = 16,
    CODE_CANCEL_TRANSACTION = 17,
    CODE_PEERS_REMOVE = 18,
};

#endif