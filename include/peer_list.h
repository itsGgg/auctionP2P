#ifndef PEER_LIST_H
#define PEER_LIST_H

#include "../lib/format.h"

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#define INITIAL_PEER_LIST_CAPACITY 4

typedef struct peer_list_t peer_list_t;
struct peer_list_t { // Pour la liste que détiennent chaque pair
    info_t *peers;   // Tableau dynamique de infos pairs
    size_t count;    // Nombre actuel de infos pairs
    size_t capacity; // Capacité actuelle du tableau
};

int peer_list_init(peer_list_t *list);
int peer_list_add(peer_list_t *list, const info_t *peer);
int peer_list_remove(peer_list_t *list, app_id_t peer_id);
info_t *peer_list_find(peer_list_t *list, app_id_t peer_id);
int peer_list_contains(peer_list_t *list, app_id_t peer_id);
void peer_list_cleanup(peer_list_t *list);

#endif