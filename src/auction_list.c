#include "../include/auction_list.h"
#include "../include/debug.h"
#include "../include/safe_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int auction_list_init(auction_list_t *list) {
    if (!list) {
        return -1;
    }
    list->auctions = malloc(AUCTION_LIST_INITIAL_CAPACITY * sizeof(auction_t));
    if (!list->auctions) {
        LOG_ERROR("Échec de l'initialisation de la liste d'enchères");
        return -1;
    }
    list->count = 0;
    list->capacity = AUCTION_LIST_INITIAL_CAPACITY;
    return 0;
}

void auction_list_free(auction_list_t *list) {
    if (!list) {
        return;
    }
    free(list->auctions);
    list->auctions = NULL;
    list->count = 0;
    list->capacity = 0;
}

/**
 * Ajoute une enchère à la liste.
 * @return 0 si ajout réussi, 1 si l'enchère existe déjà, -1 en cas d'erreur.
 */
int auction_list_add(auction_list_t *list, const auction_t *auction) {
    if (!list || !auction) {
        LOG_ERROR("Liste ou enchère invalide");
        return -1;
    }

    // Vérifier si une enchère avec le même ID existe déjà
    for (size_t i = 0; i < list->count; ++i) {
        if (list->auctions[i].numv == auction->numv) {
            LOG_WARNING("Tentative d'ajouter une enchère déjà existante (NUMV=%u)",
                        auction->numv);
            return 1;
        }
    }

    // Redimensionner si besoin
    if (list->count >= list->capacity) {
        size_t new_capacity = list->capacity * 2;
        auction_t *new_auctions =
            realloc(list->auctions, new_capacity * sizeof(auction_t));
        if (!new_auctions) {
            LOG_ERROR("Échec de l'agrandissement de la liste d'enchères");
            return -1;
        }
        list->auctions = new_auctions;
        list->capacity = new_capacity;
    }

    list->auctions[list->count++] = *auction;

    LOG_INFO("Nouvelle enchère ajoutée (NUMV=%u), total = %zu", auction->numv,
             list->count);
    return 0; // 0 pour "ajouté avec succès"
}

/**
 * Supprime une enchère de la liste.
 * @return 0 si suppression réussie, 1 si l'enchère n'existe pas, -1 en cas d'erreur.
 */
int auction_list_remove(auction_list_t *list, numv_t auction_id) {
    if (!list) {
        return -1;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (list->auctions[i].numv == auction_id) {
            // Déplacer la dernière enchère à la place de l'enchère supprimée
            list->auctions[i] = list->auctions[list->count - 1];
            list->count--;
            return 0;
        }
    }
    LOG_WARNING("Tentative de suppression d'une enchère inexistante (NUMV=%u)",
                auction_id);
    return 1;
}

auction_t *auction_list_find(auction_list_t *list, numv_t auction_id) {
    if (!list) {
        return NULL;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (list->auctions[i].numv == auction_id) {
            return &list->auctions[i];
        }
    }
    return NULL;
}

bool auction_list_contains(auction_list_t *list, numv_t auction_id) {
    if (!list) {
        return false;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (list->auctions[i].numv == auction_id) {
            return true;
        }
    }
    return false;
}

void auction_list_print(const auction_list_t *list) {

    spf_priority_lock();

    if (!list || list->count == 0) {
        spf_priority("---------------------------------------------------------------\n");
        spf_priority("Aucune enchère disponible.\n");
        spf_priority("---------------------------------------------------------------\n");
    } else {
        spf_priority("---------------------------------------------------------------\n");
        spf_priority("Liste des enchères : (Total : %d)\n", list->count);
        spf_priority_unlock();
        for (size_t i = 0; i < list->count; ++i) {
            const auction_t *a = &list->auctions[i];
            print_auction(a);
        }
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
    }

    spf_priority_unlock();
}

bool auction_list_have_supervisor(auction_list_t *list, app_id_t id) {
    if (!list) {
        return false;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (list->auctions[i].creator_id == id) {
            return true;
        }
    }
    return false;
}
