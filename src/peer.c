#include "../include/peer.h"
#include "../include/debug.h"
#include "../include/network.h"
#include "../include/safe_output.h"
#include "../include/utils.h"

#include <stdlib.h>
#include <string.h>

int peer_init(peer_t *peer, const ip_t *ip, port_t port, const char *interface_name) {
    if (!peer || !ip)
        return -1;

    memset(peer, 0, sizeof(peer_t));

    memcpy(&peer->ip, ip, sizeof(ip_t));

    peer->port = port;

    if (initialise_keys(peer) < 0) {
        LOG_ERROR("Erreur lors de l'initialisation des clés");
        return -1;
    }
    if (create_certificate(peer) < 0) {
        LOG_ERROR("Erreur lors de la création du certificat TLS");
        return -1;
    }

    char *cert_str = x509_to_human_readable(peer->tls.certificate);
    if (cert_str) {
        DEBUG_CALL(spf_priority_lock();
                   spf_priority("=== Certificat TLS ===\n%s\n", cert_str);
                   spf_priority_unlock());
        free(cert_str);
    } else {
        LOG_INFO("Échec du formatage du certificat");
    }

    if (interface_name) {
        peer->interface_name = strdup(interface_name);
    } else {
        peer->interface_name = NULL; // Pas d'interface spécifique
    }

    if (network_sockets_init(&peer->sockets, &peer->ip, peer->port,
                             peer->interface_name) < 0) {
        LOG_ERROR("Erreur lors de l'initialisation des sockets");
        peer_free(peer);
        return -1;
    }

    if (peer_list_init(&peer->peer_list) < 0) {
        LOG_ERROR("Erreur peer_list_init");
        peer_free(peer);
        return -1;
    }

    if (auction_list_init(&peer->auction_list) < 0) {
        LOG_ERROR("Erreur auction_list_init : auction_list");
        peer_free(peer);
        return -1;
    }

    if (auc_id_list_init(&peer->join_list) < 0) {
        LOG_ERROR("Erreur auc_id_list_init : join_list");
        peer_free(peer);
        return -1;
    }

    if (peer_mutex_init(&peer->mutex) < 0) {
        LOG_ERROR("Erreur lors de l'initialisation des mutex");
        peer_free(peer);
        return -1;
    }

    peer->id = generate_valid_id(peer); // Techniquement ID == 1
    if (peer->id == 0) {
        LOG_ERROR("Erreur lors de la génération de l'ID du pair");
        peer_free(peer);
        return -1;
    }

    return 0;
}

void peer_free(peer_t *peer) {
    if (peer) {
        tls_config_free(&peer->tls);
        if (peer->interface_name) {
            free(peer->interface_name);
        }
        network_sockets_close(&peer->sockets);
        peer_list_cleanup(&peer->peer_list);
        auction_list_free(&peer->auction_list);
        auc_id_list_free(&peer->join_list);
        peer_mutex_destroy(&peer->mutex);

        free(peer);
        peer = NULL;
    }
}

void print_peer(const peer_t *peer) {
    if (!peer) {
        safe_printf("Peer: (null)\n");
        return;
    }
    peer_lock((peer_t *)peer, PEER_MUTEX_LOCATION);
    peer_lock((peer_t *)peer, PEER_MUTEX_PEER_LIST);
    peer_lock((peer_t *)peer, PEER_MUTEX_SALE_COUNTER);
    peer_lock((peer_t *)peer, PEER_MUTEX_AUCTION_LIST);
    peer_lock((peer_t *)peer, PEER_MUTEX_JOIN_LIST);
    peer_lock((peer_t *)peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);

    spf_priority_lock();
    spf_priority("\n============== Peer ==============\n");
    spf_priority("ID                 : %u\n", peer->id);
    spf_priority("Adresse IP         : %s\n", ip_to_str_static(&peer->ip));
    spf_priority("Port               : %u\n", peer->port);
    spf_priority("Clé publique       : %s\n",
                 key_without_newline(peer->public_key.key, 0));
    spf_priority("Interface spé.     : %s\n",
                 peer->interface_name ? peer->interface_name : "Aucune");
    spf_priority("Position           : %s\n", location_to_str(peer->location));
    spf_priority("Pairs connus       : %zu\n", peer->peer_list.count);
    spf_priority("Compteur vente     : %u\n", peer->sale_counter);
    char buffer[12]; // assez pour un uint32_t (max 10 chiffres + \0)
    spf_priority(
        "Supervision NUMV   : %s\n",
        peer->supervising_auction_id == 0
            ? "Aucune"
            : (snprintf(buffer, sizeof(buffer), "%u", peer->supervising_auction_id),
               buffer));
    spf_priority("==================================\n\n");
    spf_priority_unlock();

    peer_unlock((peer_t *)peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    peer_unlock((peer_t *)peer, PEER_MUTEX_JOIN_LIST);
    peer_unlock((peer_t *)peer, PEER_MUTEX_AUCTION_LIST);
    peer_unlock((peer_t *)peer, PEER_MUTEX_SALE_COUNTER);
    peer_unlock((peer_t *)peer, PEER_MUTEX_PEER_LIST);
    peer_unlock((peer_t *)peer, PEER_MUTEX_LOCATION);
}

void print_auctions_system(const peer_t *peer) {
    if (!peer) {
        safe_printf("Peer: (null)\n");
        return;
    }

    peer_lock((peer_t *)peer, PEER_MUTEX_PEER_LIST);
    safe_printf("\n======================= Auctions System =======================\n");
    safe_printf("Adresse IP          : %s\n", ip_to_str_static(&peer->auctions_ip));
    safe_printf("Port                : %u\n", peer->auctions_port);
    safe_printf("Membres connectés   : %zu\n", peer->peer_list.count + 1);
    safe_printf("===============================================================\n\n");
    peer_unlock((peer_t *)peer, PEER_MUTEX_PEER_LIST);
}

/**
 * Génère un nouvel ID valide
 * Cherche le max+1 dans la liste des pairs connus et soi même
 * @warning return 0 si peer == NULL, ne pas utiliser 0 comme ID
 */
app_id_t generate_valid_id(peer_t *peer) {
    if (!peer)
        return 0;

    peer_lock(peer, PEER_MUTEX_PEER_LIST);

    app_id_t max_id = peer->id;
    for (size_t i = 0; i < peer->peer_list.count; ++i) {
        app_id_t other_id = peer->peer_list.peers[i].id;
        if (other_id > max_id) {
            max_id = other_id;
        }
    }

    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    return max_id + 1;
}

/**
 * Génère un nouvel ID d'enchère valide
 * @return Concaténation de [l’identifiant du pair] et de [sale_counter + 1]
 * @warning Autoincrémentation de sale_counter dans la fonction !
 * @warning return 0 si peer == NULL, ne pas utiliser 0 comme ID d'enchère
 */
numv_t generate_valid_numv(peer_t *peer) {
    if (!peer)
        return 0;

    char buffer[12]; // suffisamment grand pour contenir "65535" + null
    peer_lock(peer, PEER_MUTEX_SALE_COUNTER);
    snprintf(buffer, sizeof(buffer), "%u%u", peer->id, peer->sale_counter + 1);
    numv_t new_numv = (uint32_t)atoi(buffer);
    peer->sale_counter++;
    peer_unlock(peer, PEER_MUTEX_SALE_COUNTER);

    return new_numv;
}