#include "../include/network_leave.h"
#include "../include/debug.h"
#include "../include/message.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_consensus.h"

#include <errno.h>
#include <unistd.h>

/**
 * Crée et envoie un message SYSTEM_LEAVE,
 * Demande un consensus
 * @return 0 succès, -1 erreur
 */
int send_system_leave(peer_t *peer) {
    int ret = 0;
    int sock_send = -1;

    LOG_INFO("Création du message SYSTEM_LEAVE...");
    message_t *final_msg = msg_create_system_leave(peer->id);
    if (!final_msg) {
        LOG_ERROR("Échec de création du message CODE_SYSTEM_LEAVE");
        ret = -1;
        goto cleanup;
    }

    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    if (msg_serialize(final_msg, msg_buf, sizeof(msg_buf), &msg_len) !=
        SERIALIZE_SUCCESS) {
        LOG_INFO("Erreur sérialisation CODE_SYSTEM_LEAVE.");
        ret = -1;
        goto cleanup;
    }

    sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    // Adresse enchère
    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);

    if (sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr)) <
        0) {
        LOG_ERROR("Échec envoi CODE_SYSTEM_LEAVE");
        ret = -1;
        goto cleanup;
    }

    LOG_INFO("Message CODE_SYSTEM_LEAVE envoyé\n");

    close(sock_send);

    if (CONSENSUS_ON) {
        if (ask_consensus(peer, msg_buf, msg_len) < 0) {
            LOG_ERROR("Echec du consensus");
            ret = -1;
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    if (final_msg != NULL) {
        message_free(final_msg);
        final_msg = NULL;
    }
    if (sock_send >= 0) {
        close(sock_send);
        sock_send = -1;
    }

    return ret;
}

int delete_peer_info(peer_t *peer, app_id_t id) {
    if (peer->id != id) {
        peer_lock(peer, PEER_MUTEX_PEER_LIST);
        int removed = peer_list_remove(&peer->peer_list, id);
        peer_unlock(peer, PEER_MUTEX_PEER_LIST);
        if (removed < 0) {
            return -1;
        }
    }
    LOG_INFO("Pair quittant le système -> supprimé de la liste des pairs\n");
    return 0;
}

void handle_system_leave(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_SYSTEM_LEAVE] reçu.\n", CODE_SYSTEM_LEAVE);

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    info_t *validation_dest = peer_list_find(&peer->peer_list, msg->content.leave.id);
    bool found = validation_dest != NULL;
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    info_t dest;
    if (found) {
        dest = *validation_dest;
    }

    if (delete_peer_info(peer, msg->content.leave.id) < 0) {
        LOG_ERROR("Échec de la suppression du pair");
        return;
    }

    u_int8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;
    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de la sérialisation du message CODE_SYSTEM_LEAVE");
        return;
    }

    if (CONSENSUS_ON) {
        if (found) {
            if (respond_consensus(peer, msg_buf, msg_len, dest) < 0) {
                LOG_ERROR(
                    "Échec de la réponse au consensus pour le message CODE_SYSTEM_LEAVE");
            } else {
                LOG_INFO(
                    "Réponse au consensus pour le message CODE_SYSTEM_LEAVE réussie");
            }
        } else {
            LOG_WARNING(
                "Aucun pair trouvé pour la validation du message CODE_SYSTEM_LEAVE");
        }
    }

    return;
}
