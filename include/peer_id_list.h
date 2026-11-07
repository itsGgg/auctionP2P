#ifndef peer_ID_LIST_H
#define peer_ID_LIST_H

#include "../lib/format.h"

#include <stdbool.h>

#define PEER_ID_LIST_INITIAL_CAPACITY 8

typedef struct peer_id_list_t peer_id_list_t;

struct peer_id_list_t {
    app_id_t *ids;
    size_t count;
    size_t capacity;
};

int peer_id_list_init(peer_id_list_t *list);
void peer_id_list_free(peer_id_list_t *list);
int peer_id_list_add(peer_id_list_t *list, app_id_t id);
int peer_id_list_remove(peer_id_list_t *list, app_id_t id);
bool peer_id_list_contains(peer_id_list_t *list, app_id_t id);
void peer_id_list_print(const peer_id_list_t *list);

#endif