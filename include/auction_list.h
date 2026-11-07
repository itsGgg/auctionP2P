#ifndef AUCTION_LIST_H
#define AUCTION_LIST_H

#include "../include/auction.h"

#include <stdbool.h>

#define AUCTION_LIST_INITIAL_CAPACITY 8

typedef struct auction_list_t auction_list_t;

struct auction_list_t {
    auction_t *auctions; // Tableau dynamique d'enchères
    size_t count;        // Nombre d'enchères présentes
    size_t capacity;     // Capacité maximale actuelle
};

int auction_list_init(auction_list_t *list);
void auction_list_free(auction_list_t *list);
int auction_list_add(auction_list_t *list, const auction_t *auction);
int auction_list_remove(auction_list_t *list, numv_t auction_id);
auction_t *auction_list_find(auction_list_t *list, numv_t auction_id);
bool auction_list_contains(auction_list_t *list, numv_t auction_id);
void auction_list_print(const auction_list_t *list);
bool auction_list_have_supervisor(auction_list_t *list, app_id_t id);

#endif
