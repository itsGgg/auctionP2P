#include "../include/network_missing_peer.h"
#include "../include/debug.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_consensus.h"

#include <unistd.h>

int handle_missing_peer(peer_t *peer, nb_t count, app_id_t *ids) {
    message_t *msg = NULL;
    int sock_send = -1;
    int ret = 0;

    if (delete_missing_peers(peer, ids, count) < 0) {
        LOG_ERROR("Échec de la suppression des pairs manquants");
        ret = -1;
        goto cleanup;
    }

    msg = msg_create_peers_remove(peer, count, ids);
    if (!msg) {
        LOG_ERROR("erreur creation message peers remove");
        ret = -1;
        goto cleanup;
    }

    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_INFO("Erreur sérialisation CODE_PEERS_REMOVE.");
        ret = -1;
        goto cleanup;
    }

    // LOG_INFO("Buffer à envoyer (%zu octets):", msg_len);
    // for (size_t i = 0; i < msg_len && i < 32; i++) {
    //     LOG_INFO("%02X ", msg_buf[i]);
    // }

    sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    // Adresse enchère
    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);

    LOG_INFO("Envoi du message CODE_PEERS_REMOVE (%zu octets)...", msg_len);
    if (sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr)) <
        0) {
        LOG_ERROR("Échec envoi CODE_SYSTEM_LEAVE");
        ret = -1;
        goto cleanup;
    }
    close(sock_send);
    if (CONSENSUS_ON) {
        if (ask_consensus(peer, msg_buf, msg_len) < 0) {
            LOG_ERROR("Echec du consensus");
            LOG_ERROR("Je n'ai pas pu prévenir des pairs manquants");
            ret = -1;
            goto cleanup;
        } else {
            LOG_INFO("J'ai prévenu des pairs manquants");
        }
    }

cleanup:
    if (msg != NULL) {
        message_free(msg);
        msg = NULL;
    }
    if (sock_send >= 0) {
        close(sock_send);
        sock_send = -1;
    }
    return ret;
}

int delete_missing_peers(peer_t *peer, app_id_t *ids, nb_t count) {
    for (size_t i = 0; i < count; i++) {
        LOG_INFO("Je supprime ID: %u", ids[i]);
        if (peer_list_remove(&peer->peer_list, ids[i]) < 0) {
            return -1;
        }
    }

    return 0;
}

void handle_peers_remove(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_PEERS_REMOVE] reçu.\n", CODE_PEERS_REMOVE);

    if (delete_missing_peers(peer, msg->content.peer_mgmt.afk.peers.peer_ids,
                             msg->content.peer_mgmt.afk.peers.peer_count) < 0) {
        LOG_ERROR("Échec de la suppression des pairs");
        return;
    }

    u_int8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;
    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de la sérialisation du message CODE_AUCTION_END");
        return;
    }

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    info_t *validation_dest = peer_list_find(&peer->peer_list, msg->content.leave.id);
    bool found = validation_dest != NULL;
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    info_t dest;
    if (found) {
        dest = *validation_dest;
    }

    if (CONSENSUS_ON) {
        if (found) {
            if (respond_consensus(peer, msg_buf, msg_len, dest) < 0) {
                LOG_ERROR(
                    "Échec de la réponse au consensus pour le message CODE_PEERS_REMOVE");
            } else {
                LOG_INFO(
                    "Réponse au consensus pour le message CODE_PEERS_REMOVE réussie");
            }
        } else {
            LOG_WARNING(
                "Aucun pair trouvé pour la validation du message CODE_PEERS_REMOVE");
        }
    }

    return;
}

int handle_missing_supervisor(peer_t *peer, info_t supervisor) {
    int ret = 0;
    numv_t aid = 0;
    message_t *msg = NULL;
    int sock_send = -1;

    // on cherche le numero de l'enchère supervisée par le superviseur
    for (size_t i = 0; i < peer->auction_list.count; i++) {
        if (peer->auction_list.auctions[i].creator_id == supervisor.id) {
            LOG_INFO("Superviseur trouvé pour l'enchère %u", supervisor.id,
                     peer->auction_list.auctions[i].numv);
            aid = peer->auction_list.auctions[i].numv;
            break;
        }
    }

    // PROBLEME: COMMENT OBTENIR L'ID D'UN AUTRE PAIR AVEC CES INFOS
    // Contournement: au lieu d'envoyer l'id de S on s'envoie le notre
    // Les autres pairs ont du remarquer la disparition de S
    msg = msg_create_supervisor_afk(peer, aid);
    if (!msg) {
        LOG_ERROR("Échec de la création du message CODE_SUPERVISOR_AFK");
        ret = -1;
        goto cleanup;
    }

    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de la sérialisation du message CODE_SUPERVISOR_AFK");
        ret = -1;
        goto cleanup;
    }

    sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);
    if (sock_send < 0) {
        LOG_ERROR("Échec de la liaison UDP pour CODE_SUPERVISOR_AFK");
        ret = -1;
        goto cleanup;
    }

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);

    if (sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr)) <
        0) {
        LOG_ERROR("Échec de l'envoi du message CODE_SUPERVISOR_AFK");
        ret = -1;
        goto cleanup;
    }

    LOG_INFO("Message CODE_MISSING_SUPERVISOR envoyé");
    if (CONSENSUS_ON) {
        if (ask_consensus(peer, msg_buf, msg_len) < 0) {
            LOG_ERROR("Échec du consensus pour le message CODE_SUPERVISOR_AFK");
            ret = -1;
            goto cleanup;
        } else {
            LOG_INFO("Consensus réussi pour le message CODE_SUPERVISOR_AFK");
        }
    }

cleanup:
    if (msg != NULL) {
        message_free(msg);
        msg = NULL;
    }
    if (sock_send >= 0) {
        close(sock_send);
        sock_send = -1;
    }
    return ret;
}