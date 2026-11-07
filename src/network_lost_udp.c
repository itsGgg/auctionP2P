#include "../include/debug.h"
#include "../include/message_serializer.h"
#include "../include/network_consensus.h"
#include "../include/peer.h"

#include <unistd.h>

/**
 * Relance les pairs dont on n'a pas reçu de validation
 */
int relaunch_consensus_peer(peer_t *peer, consensus_context_t *ctx) {
    int sock_send = -1;
    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    memcpy(msg_buf, ctx->msg, ctx->lmess);
    size_t msg_len = ctx->lmess;
    sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    if (sock_send < 0) {
        LOG_ERROR("Échec de la liaison UDP");
        return -1;
    }

    for (size_t i = 0; i < ctx->remaining_list.count; i++) {
        info_t *destination =
            peer_list_find(&peer->peer_list, ctx->remaining_list.ids[i]);
        if (destination != NULL) {
            struct sockaddr_in6 addr =
                make_addr(&destination->ip, destination->port, NULL);

            if (sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr,
                       sizeof(addr)) < 0) {
                LOG_ERROR("Échec envoi relance");
                close(sock_send);
                return -1;
            }

            LOG_WARNING("Pair %u relancé", ctx->remaining_list.ids[i]);
        }
    }
    close(sock_send);
    return 0;
}

int resend_message(peer_t *peer, message_t *msg, int sock_send,
                   struct sockaddr_in6 addr) {
    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_INFO("Erreur sérialisation du message.");
        return -1;
    }

    if (sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr)) <
        0) {
        LOG_ERROR("Échec envoi du message");
        return -1;
    }
    return 0;
}