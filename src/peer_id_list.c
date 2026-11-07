#include "../include/peer_id_list.h"
#include "../include/debug.h"
#include "../include/safe_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int peer_id_list_init(peer_id_list_t *list) {
    if (list == NULL) {
        return -1; // Vérification des arguments d'entrée
    }

    list->ids = malloc(PEER_ID_LIST_INITIAL_CAPACITY * sizeof(app_id_t));
    if (list->ids == NULL) {
        perror("Erreur d'allocation mémoire");
        return -1;
    }

    list->count = 0;
    list->capacity = PEER_ID_LIST_INITIAL_CAPACITY;

    return 0;
}

void peer_id_list_free(peer_id_list_t *list) {
    if (list == NULL) {
        return; // Vérification des arguments d'entrée
    }

    free(list->ids);
    list->ids = NULL;
    list->count = 0;
    list->capacity = 0;
}

int peer_id_list_add(peer_id_list_t *list, app_id_t id) {
    if (list == NULL) {
        return -1; // Vérification des arguments d'entrée
    }

    if (list->count >= list->capacity) {
        size_t new_capacity = list->capacity * 2;
        app_id_t *new_ids = realloc(list->ids, new_capacity * sizeof(app_id_t));
        if (new_ids == NULL) {
            perror("Erreur d'allocation mémoire");
            return -1;
        }
        list->ids = new_ids;
        list->capacity = new_capacity;
    }

    list->ids[list->count++] = id;
    return 0;
}

int peer_id_list_remove(peer_id_list_t *list, app_id_t id) {
    if (list == NULL || list->count == 0) {
        return -1; // Vérification des arguments d'entrée
    }

    for (size_t i = 0; i < list->count; i++) {
        if (list->ids[i] == id) {
            // Déplacer le dernier élément à la position de l'élément supprimé
            list->ids[i] = list->ids[list->count - 1];
            list->count--;
            return 0;
        }
    }
    return -1; // ID non trouvé
}

bool peer_id_list_contains(peer_id_list_t *list, app_id_t id) {
    if (list == NULL) {
        return false; // Vérification des arguments d'entrée
    }

    for (size_t i = 0; i < list->count; i++) {
        if (list->ids[i] == id) {
            return true;
        }
    }
    return false;
}

void peer_id_list_print(const peer_id_list_t *list) {
    if (list == NULL || list->count == 0) {
        safe_printf("La liste des IDs de pairs est vide.\n");
        return;
    }

    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Liste des IDs de pairs (%zu):\n", list->count);
    for (size_t i = 0; i < list->count; i++) {
        spf_priority("ID %zu: %u\n", i + 1, list->ids[i]);
    }
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}