#include "../include/auction.h"
#include "../include/debug.h"
#include "../include/message.h"
#include "../include/peer.h"
#include "../include/safe_output.h"

auction_t *create_auction(peer_t *creator, prix_t starting_price) {
    auction_t *auction = malloc(sizeof(auction_t));
    if (auction == NULL) {
        perror("Erreur d'allocation mémoire");
        return NULL;
    }

    auction->numv = generate_valid_numv(creator);
    if (auction->numv == 0) {
        LOG_ERROR("Erreur lors de la génération de l'ID d'enchère");
        free(auction);
        return NULL;
    }
    LOG_INFO("NUMV généré : %u\n", auction->numv);
    auction->creator_id = creator->id;
    auction->starting_price = starting_price;
    auction->bid_price = starting_price;
    auction->bidder_id = 0; // Aucun enchérisseur au départ
    auction->creation_time = time(NULL);
    auction->last_bid_time = auction->creation_time;
    auction->state = AUCTION_OPEN;

    return auction;
}

int update_auction(auction_t *old_auction, auction_t *new_auction) {
    if (old_auction == NULL || new_auction == NULL) {
        LOG_ERROR("Échec de la mise à jour de l'enchère");
        return -1;
    }

    if (old_auction->creator_id == 0 && new_auction->creator_id != 0) {
        old_auction->creator_id = new_auction->creator_id;
    }
    if (old_auction->starting_price == 0 && new_auction->starting_price != 0) {
        old_auction->starting_price = new_auction->starting_price;
    }
    old_auction->bid_price = new_auction->bid_price;
    old_auction->bidder_id = new_auction->bidder_id;
    if (old_auction->creation_time == 0 && new_auction->creation_time != 0) {
        old_auction->creation_time = new_auction->creation_time;
    }
    old_auction->last_bid_time = new_auction->last_bid_time;
    old_auction->state = new_auction->state;

    return 0;
}

void print_auction(const auction_t *auction) {
    if (auction == NULL) {
        return;
    }

    spf_priority_lock();

    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Enchère NUMV: %u\n", auction->numv);

    if (auction->creator_id == 0) {
        spf_priority("Créateur ID: Inconnu\n");
    } else {
        spf_priority("Créateur ID: %u\n", auction->creator_id);
    }

    if (auction->starting_price == 0) {
        spf_priority("Prix de départ: Inconnu\n");
    } else {
        spf_priority("Prix de départ: %u\n", auction->starting_price);
    }

    if (auction->bid_price == 0) {
        spf_priority("Prix courant: Inconnu\n");
    } else {
        spf_priority("Prix courant: %u\n", auction->bid_price);
    }
    if (auction->bidder_id == 0) {
        spf_priority("Meilleur enchérisseur: Inconnu\n");
    } else {
        spf_priority("Meilleur enchérisseur: %u\n", auction->bidder_id);
    }

    if (auction->creation_time == 0) {
        spf_priority("Date de création: Inconnue\n");
    } else {
        spf_priority("Date de création: %s", ctime(&auction->creation_time));
    }

    if (auction->last_bid_time == 0) {
        spf_priority("Dernière enchère: Inconnue\n");
    } else {
        spf_priority("Dernière enchérissement: %s", ctime(&auction->last_bid_time));
    }

    spf_priority("État de l'enchère: %s\n", get_auction_state_str(auction->state));
    spf_priority("---------------------------------------------------------------\n");

    spf_priority_unlock();
}

void print_auction_end(const auction_t *auction) {
    if (auction == NULL) {
        return;
    }

    spf_priority_lock();

    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Enchère NUMV: %u\n", auction->numv);
    if (auction->creator_id == 0) {
        spf_priority("Créateur ID: Inconnu\n");
    } else {
        spf_priority("Créateur ID: %u\n", auction->creator_id);
    }

    if (auction->starting_price == 0) {
        spf_priority("Prix de départ: Inconnu\n");
    } else {
        spf_priority("Prix de départ: %u\n", auction->starting_price);
    }
    if (auction->bidder_id == 0) {
        spf_priority("Gagnant ID: %s\n", "Pas d'enchérisseur");
    } else {
        spf_priority("Gagnant ID: %u\n", auction->bidder_id);
        spf_priority("Prix final: %u\n", auction->bid_price);
        spf_priority("Date du dernier enchérissement: %s",
                     ctime(&auction->last_bid_time));
    }
    spf_priority("Date de création: %s", ctime(&auction->creation_time));
    time_t now = time(NULL);
    char *date_str = ctime(&now);
    spf_priority("Date de fermeture: %s", date_str);
    spf_priority("État de l'enchère: %s\n", get_auction_state_str(auction->state));
    spf_priority("---------------------------------------------------------------\n");

    spf_priority_unlock();
}

char *get_auction_state_str(auction_state_t state) {
    switch (state) {
        case AUCTION_OPEN:
            return "Ouverte";
        case AUCTION_CLOSED:
            return "Fermée";
        default:
            return "État inconnu";
    }
}

void print_compare_auction(const auction_t *old_auction, const auction_t *new_auction) {
    if (old_auction == NULL || new_auction == NULL) {
        return;
    }

    spf_priority_lock();

    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Détails de l'enchérissement NUMV: %u\n", old_auction->numv);

    if (old_auction->bid_price != new_auction->bid_price) {
        spf_priority("Prix courant: %u -> %u\n", old_auction->bid_price,
                     new_auction->bid_price);
    }
    if (old_auction->bidder_id != new_auction->bidder_id) {
        spf_priority("Meilleur enchérisseur: %u -> %u\n", old_auction->bidder_id,
                     new_auction->bidder_id);
    }
    if (old_auction->last_bid_time != new_auction->last_bid_time) {
        spf_priority("Dernière enchérissement: %s -> %s",
                     ctime(&old_auction->last_bid_time),
                     ctime(&new_auction->last_bid_time));
    }
    spf_priority("---------------------------------------------------------------\n");

    spf_priority_unlock();
}

/**
 * Convertit un message infos d'enchère en une structure d'enchère.
 * @param msg Le message d'enchère à convertir
 * @param auction A allouer au préalable
 * @param code Le code du message reçu
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int msg_auction_to_auction(msg_auction_t *msg, auction_t *auction, message_codes_t code) {
    if (!msg || !auction) {
        LOG_ERROR("Erreur: msg ou auction est NULL");
        return -1;
    }
    memset(auction, 0, sizeof(auction_t));

    switch (code) {
        case CODE_AUCTION_START:
            // Seul le bloc CODE_AUCTION_START aura une déclaration explicite
            auction->numv = msg->auction_id;
            auction->creator_id = msg->id;
            auction->starting_price = msg->price;
            auction->bid_price = msg->price;
            auction->bidder_id = 0; // Explicite, sans enchérisseur au départ
            auction->creation_time = time(NULL);
            auction->last_bid_time = auction->creation_time;
            auction->state = AUCTION_OPEN; // Explicite, mais safe
            break;

        case CODE_AUCTION_BID:
            auction->numv = msg->auction_id;
            auction->bid_price = msg->price;
            auction->bidder_id = msg->id;
            auction->last_bid_time = time(NULL); // Destiné au superviseur
            auction->state = AUCTION_OPEN;
            break;

        case CODE_AUCTION_BID_CONFIRM:
            auction->numv = msg->auction_id;
            auction->bid_price = msg->price;
            auction->bidder_id = msg->id;
            auction->last_bid_time = time(NULL); // Destiné aux pairs
            auction->state = AUCTION_OPEN;
            break;

        case CODE_AUCTION_ALMOST_END:
            auction->numv = msg->auction_id;
            auction->bid_price = msg->price;
            auction->creator_id = msg->id;
            auction->state = AUCTION_OPEN;
            break;

        case CODE_AUCTION_END:
            auction->numv = msg->auction_id;
            auction->bid_price = msg->price;
            auction->bidder_id = msg->id;
            auction->state = AUCTION_CLOSED;
            break;

        case CODE_AUCTION_REJECT_CONCURRENT:
            auction->numv = msg->auction_id;
            auction->bid_price = msg->price;
            auction->bidder_id = msg->id;
            break;

        case CODE_AUCTION_REJECT_INVALID:
            auction->numv = msg->auction_id;
            auction->bid_price = msg->price;
            auction->bidder_id = msg->id;
            break;

        default:
            LOG_WARNING("Erreur: code de message inconnu ou non pris en charge");
            break;
    }

    return 0;
}