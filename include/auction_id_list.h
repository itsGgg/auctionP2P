#ifndef AUCTION_ID_LIST_H
#define AUCTION_ID_LIST_H

#include "../lib/format.h"

#include <stdbool.h>

#define AUC_ID_LIST_INITIAL_CAPACITY 8

typedef struct auc_id_list_t auc_id_list_t;

struct auc_id_list_t {
    numv_t *ids;
    size_t count;
    size_t capacity;
};

int auc_id_list_init(auc_id_list_t *list);
void auc_id_list_free(auc_id_list_t *list);
int auc_id_list_add(auc_id_list_t *list, numv_t auction);
int auc_id_list_remove(auc_id_list_t *list, numv_t auction_id);
bool auc_id_list_contains(auc_id_list_t *list, numv_t auction_id);
void auc_id_list_print(const auc_id_list_t *list);

#endif
