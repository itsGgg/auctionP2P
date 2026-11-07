#include "../include/peer_list.h"
#include "../include/debug.h"
#include "../include/safe_output.h"

#include <stdlib.h>
#include <string.h>

int peer_list_init(peer_list_t *list) {
    list->peers = malloc(INITIAL_PEER_LIST_CAPACITY * sizeof(info_t));
    if (!list->peers) {
        LOG_ERROR("Erreur malloc");
        return -1;
    }

    list->count = 0;
    list->capacity = INITIAL_PEER_LIST_CAPACITY;
    return 0;
}

/**
 * Ajoute un pair à la liste
 * Si le pair existe déjà, met à jour ses informations
 * @return 0 si ajouté, 1 si mis à jour, -1 en cas d'erreur
 */
int peer_list_add(peer_list_t *list, const info_t *peer_info) {
    if (!list || !peer_info) {
        return -1;
    }

    // Vérifie si le pair est déjà dans la liste
    for (size_t i = 0; i < list->count; ++i) {
        if (list->peers[i].id == peer_info->id) {
            // Mettre à jour les informations existantes
            LOG_INFO("Mise à jour du pair existant | ID = %u", peer_info->id);
            list->peers[i] = *peer_info;
            return 1;
        }
    }

    // Resize si nécessaire
    if (list->count >= list->capacity) {
        size_t new_capacity =
            list->capacity > 0 ? list->capacity * 2 : 4; // capacité minimale de 4
        info_t *new_array = realloc(list->peers, new_capacity * sizeof(info_t));
        if (!new_array) {
            LOG_ERROR("Erreur lors du resize de la liste de pairs");
            return -1;
        }
        list->peers = new_array;
        list->capacity = new_capacity;
    }

    list->peers[list->count++] = *peer_info;

    LOG_INFO("Pair %u ajouté (nouveau total : %zu)", peer_info->id, list->count);
    return 0;
}

int peer_list_remove(peer_list_t *list, app_id_t peer_id) {
    if (!list) {
        return -1;
    }

    for (size_t i = 0; i < list->count; ++i) {
        if (list->peers[i].id == peer_id) {
            // Écrase l'élément à supprimer avec le dernier
            list->peers[i] = list->peers[list->count - 1];
            list->count--;
            LOG_INFO("Pair %u retiré", peer_id);
            return 0;
        }
    }
    LOG_WARNING("Pair %u non trouvé dans la liste", peer_id);
    return -1;
}

info_t *peer_list_find(peer_list_t *list, app_id_t peer_id) {
    if (!list) {
        return NULL;
    }

    for (size_t i = 0; i < list->count; ++i) {
        if (list->peers[i].id == peer_id) {
            return &list->peers[i];
        }
    }
    return NULL;
}

int peer_list_contains(peer_list_t *list, app_id_t peer_id) {
    if (!list) {
        return 0;
    }

    for (size_t i = 0; i < list->count; ++i) {
        if (list->peers[i].id == peer_id) {
            return 1; // Pair trouvé
        }
    }
    return 0; // Pair non trouvé
}

void peer_list_cleanup(peer_list_t *list) {
    if (!list) {
        return;
    }

    free(list->peers);
    list->peers = NULL;
    list->count = 0;
    list->capacity = 0;
}