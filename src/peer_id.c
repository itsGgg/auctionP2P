#include "../include/peer_id.h"
#include "../include/debug.h"

#include <stdlib.h>

#define ID_RETRY_MAX 10

bool is_id_in_use(const peer_list_t *list, app_id_t id) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->peers[i].id == id) {
            return true;
        }
    }
    return false;
}

app_id_t generate_id(const peer_list_t *peer_list) {
    LOG_INFO("Génération d'un ID pour un pair...");

    // 1. Essayer max+1 d'abord
    LOG_INFO("Génération par max+1");
    if (peer_list->count > 0) {
        app_id_t max_id = 0;
        for (size_t i = 0; i < peer_list->count; i++) {
            if (peer_list->peers[i].id > max_id) {
                max_id = peer_list->peers[i].id;
            }
        }

        if (max_id < ID_MAX && !is_id_in_use(peer_list, max_id + 1)) {
            LOG_INFO("Génération de l'id avec succès");
            return max_id + 1;
        }
    }

    // 2. Fallback aléatoire avec vérification
    LOG_INFO("Echec. Génération par fallback aléatoire");
    for (int i = 0; i < ID_RETRY_MAX; i++) {
        app_id_t candidate = (rand() % (ID_MAX - 1)) + 1; // Évite 0

        if (!is_id_in_use(peer_list, candidate)) {
            LOG_INFO("Génération de l'id avec succès");
            return candidate;
        }
    }

    // 3. Fallback extrême (très rare)
    LOG_INFO("Echec. Génération par fallback extrême");
    for (app_id_t i = 1; i < ID_MAX; i++) {
        if (!is_id_in_use(peer_list, i)) {
            LOG_INFO("Génération de l'id avec succès");
            return i;
        }
    }

    LOG_ERROR("Echec de la génération de l'id du pair.");
    return 0; // Échec (devrait jamais arriver avec ID_MAX = 65535)
}