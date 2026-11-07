#include "../include/auction_id_list.h"
#include "../include/debug.h"
#include "../include/safe_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int auc_id_list_init(auc_id_list_t *list) {
    if (!list) {
        return -1;
    }
    list->ids = malloc(AUC_ID_LIST_INITIAL_CAPACITY * sizeof(numv_t));
    if (!list->ids) {
        LOG_ERROR("Échec de l'initialisation de la liste d'IDs d'enchères");
        return -1;
    }
    list->count = 0;
    list->capacity = AUC_ID_LIST_INITIAL_CAPACITY;
    return 0;
}

void auc_id_list_free(auc_id_list_t *list) {
    if (!list) {
        return;
    }
    free(list->ids);
    list->ids = NULL;
    list->count = 0;
    list->capacity = 0;
}

int auc_id_list_add(auc_id_list_t *list, numv_t auction) {
    if (!list) {
        LOG_ERROR("Liste invalide");
        return -1;
    }

    // Vérifier si l'ID d'enchère existe déjà
    for (size_t i = 0; i < list->count; ++i) {
        if (list->ids[i] == auction) {
            LOG_WARNING("Tentative d'ajouter un ID d'enchère déjà existant (NUMV=%u)",
                        auction);
            return 1;
        }
    }

    // Redimensionner si besoin
    if (list->count >= list->capacity) {
        size_t new_capacity = list->capacity * 2;
        numv_t *new_ids = realloc(list->ids, new_capacity * sizeof(numv_t));
        if (!new_ids) {
            LOG_ERROR("Échec de l'agrandissement de la liste d'IDs d'enchères");
            return -1;
        }
        list->ids = new_ids;
        list->capacity = new_capacity;
    }

    list->ids[list->count++] = auction;

    LOG_INFO("Ajout de l'ID d'enchère (NUMV=%u) à la liste", auction);
    return 0;
}

int auc_id_list_remove(auc_id_list_t *list, numv_t auction_id) {
    if (!list) {
        return -1;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (list->ids[i] == auction_id) {
            // Déplacer le dernier ID à la place de l'ID supprimé
            list->ids[i] = list->ids[list->count - 1];
            list->count--;
            return 0;
        }
    }
    LOG_WARNING("Tentative de suppression d'un ID d'enchère inexistant (NUMV=%u)",
                auction_id);
    return 1;
}

bool auc_id_list_contains(auc_id_list_t *list, numv_t auction_id) {
    if (!list) {
        return false;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (list->ids[i] == auction_id) {
            return true;
        }
    }
    return false;
}

void auc_id_list_print(const auc_id_list_t *list) {
    if (!list) {
        return;
    }

    spf_priority_lock();

    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Liste d'IDs d'enchères : (Total : %d)\n", list->count);
    for (size_t i = 0; i < list->count; ++i) {
        spf_priority("  - %u\n", list->ids[i]);
    }
    spf_priority("---------------------------------------------------------------\n");

    spf_priority_unlock();
}