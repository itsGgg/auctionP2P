#ifndef AUCTION_H
#define AUCTION_H

#include "../lib/format.h"

#include <stdint.h>
#include <time.h>

// Forward declaration
typedef struct peer_t peer_t;
typedef struct msg_auction_t msg_auction_t;

typedef enum auction_state_t auction_state_t;
typedef struct auction_t auction_t;

// État d'une enchère
enum auction_state_t {
    // AUCTION_NEW,           // Nouvelle enchère
    AUCTION_OPEN, // Enchère ouverte
    // AUCTION_PENDING_CLOSE, // En attente de fermeture
    AUCTION_CLOSED // Enchère fermée
};

// Structure d'une enchère
struct auction_t {
    numv_t numv;           // Identifiant unique
    app_id_t creator_id;   // ID du créateur
    prix_t starting_price; // Prix de départ
    prix_t bid_price;      // Prix courant ou proposé
    app_id_t bidder_id;    // ID du enchérisseur courant
    time_t creation_time;  // Quand l'enchère a été créée
    time_t last_bid_time;  // Quand la dernière enchère a été faite
    auction_state_t state; // État courant
};

// Fonctions pour gérer les enchères

auction_t *create_auction(peer_t *creator, prix_t starting_price);

int update_auction(auction_t *old_auction, auction_t *new_auction);

void print_auction(const auction_t *auction);
void print_auction_end(const auction_t *auction);
char *get_auction_state_str(auction_state_t state);
void print_compare_auction(const auction_t *old_auction, const auction_t *new_auction);

int msg_auction_to_auction(msg_auction_t *msg, auction_t *auction, message_codes_t code);

#endif