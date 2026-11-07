#ifndef PEER_H
#define PEER_H

#include "../include/auction_id_list.h"
#include "../include/auction_list.h"
#include "../include/console.h"
#include "../include/network_sockets.h"
#include "../include/network_tls.h"
#include "../include/peer_id.h"
#include "../include/peer_list.h"
#include "../include/peer_mutex.h"
#include "../lib/config.h"
#include "../lib/format.h"
#include <openssl/x509.h>

#include <stdint.h>

typedef struct peer_t peer_t;

// Structure d'un pair
struct peer_t {
    app_id_t id;
    ip_t ip;
    port_t port;
    cle_pub_t public_key;
    tls_config_t tls;
    char *interface_name; // Vaut NULL si pas d'interface spécifique
    network_sockets_t sockets;
    user_location_t location;
    peer_list_t peer_list;
    ip_t auctions_ip;
    port_t auctions_port;
    uint16_t sale_counter;
    auction_list_t auction_list;
    auc_id_list_t join_list;
    numv_t supervising_auction_id;
    peer_mutex_t mutex;
};

// Fonctions de gestion du pair
int peer_init(peer_t *peer, const ip_t *ip, port_t port, const char *interface_name);
void peer_free(peer_t *peer);
void print_peer(const peer_t *peer);
void print_auctions_system(const peer_t *peer);

app_id_t generate_valid_id(peer_t *peer);
numv_t generate_valid_numv(peer_t *peer);

#endif