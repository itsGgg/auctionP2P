#include "../include/message_builder.h"
#include "../include/debug.h"
#include "../lib/config.h"
#include "../lib/format.h"

#include <string.h>

message_t *msg_create_validation(app_id_t id, mess_t *message, sign_t *signature) {
    LOG_INFO("Création message CODE=%d [CODE_VALIDATION]...", CODE_VALIDATION);

    message_t *msg = message_create(CODE_VALIDATION);
    if (!msg)
        return NULL;

    msg->content.validation.id = id;

    if (message->length > 0) {
        msg->content.validation.message.data = malloc(message->length);
        if (!msg->content.validation.message.data) {
            message_free(msg);
            return NULL;
        }
        memcpy(msg->content.validation.message.data, message->data, message->length);
        msg->content.validation.message.length = message->length;
    } else {
        msg->content.validation.message.data = NULL;
        msg->content.validation.message.length = 0;
    }

    if (copy_signature(&msg->content.validation.signature, signature) < 0) {
        message_free(msg);
        return NULL;
    }

    LOG_INFO("Message CODE=%d créé avec ID=%u, LMESS=%u, LSIG=%u", msg->code,
             msg->content.validation.id, msg->content.validation.message.length,
             msg->content.validation.signature.length);

    return msg;
}

message_t *msg_create_consensus(const peer_t *peer, mess_t *message, sign_t *signature,
                                nb_t peer_count, peer_signature_t *peer_signatures) {
    LOG_INFO("Création message CODE=%d [CODE_CONSENSUS]...", CODE_CONSENSUS);

    message_t *msg = message_create(CODE_CONSENSUS);
    if (!msg)
        return NULL;

    msg->content.consensus.id = peer->id;

    if (message->length > 0) {
        msg->content.consensus.message.length = message->length;
        msg->content.consensus.message.data = malloc(message->length);
        if (!msg->content.consensus.message.data) {
            message_free(msg);
            return NULL;
        }
        memcpy(msg->content.consensus.message.data, message->data, message->length);
    } else {
        msg->content.consensus.message.data = NULL;
        msg->content.consensus.message.length = 0;
    }

    if (copy_signature(&msg->content.consensus.signature, signature) < 0) {
        message_free(msg);
        return NULL;
    }

    // copier signatures des pairs
    nb_t nb_to_copy = (peer_count <= MAX_SIGNATURES) ? peer_count : MAX_SIGNATURES;
    msg->content.consensus.peer_count = nb_to_copy;

    if (nb_to_copy > 0) {
        if (copy_peer_signatures(&msg->content.consensus.peer_signatures, peer_signatures,
                                 nb_to_copy) < 0) {
            message_free(msg);
            return NULL;
        }
    } else {
        msg->content.consensus.peer_signatures = NULL;
    }

    LOG_INFO("Message CODE=%d créé avec ID=%u, LMESS=%u, LSIG=%u, NB_PEERS=%u", msg->code,
             msg->content.consensus.id, msg->content.consensus.message.length,
             msg->content.consensus.signature.length, nb_to_copy);

    return msg;
}

message_t *msg_create_consensus_part(const peer_t *peer, nb_t peer_count,
                                     peer_signature_t *peer_signatures) {
    LOG_INFO("Création message CODE=%d [CODE_CONSENSUS_PART]...", CODE_CONSENSUS_PART);

    message_t *msg = message_create(CODE_CONSENSUS_PART);
    if (!msg)
        return NULL;

    msg->content.consensus.id = peer->id;

    // copier signatures des pairs
    nb_t nb_to_copy = (peer_count <= MAX_SIGNATURES) ? peer_count : MAX_SIGNATURES;
    msg->content.consensus.peer_count = nb_to_copy;

    if (nb_to_copy > 0) {
        if (copy_peer_signatures(&msg->content.consensus.peer_signatures, peer_signatures,
                                 nb_to_copy) < 0) {
            message_free(msg);
            return NULL;
        }
    } else {
        msg->content.consensus.peer_signatures = NULL;
    }

    return msg;
}

message_t *msg_create_join_request() {
    LOG_INFO("Création message CODE=%d [CODE_JOIN_REQUEST]...", CODE_JOIN_REQUEST);

    message_t *msg = message_create(CODE_JOIN_REQUEST);
    if (!msg)
        return NULL;

    return msg;
}

message_t *msg_create_join_response(const peer_t *peer) {
    LOG_INFO("Création message CODE=%d [CODE_JOIN_RESPONSE]...", CODE_JOIN_RESPONSE);

    if (!peer) {
        LOG_ERROR("Erreur création message : peer NULL");
        return NULL;
    }

    message_t *msg = message_create(CODE_JOIN_RESPONSE);
    if (!msg)
        return NULL;

    msg->content.join_resp.id = peer->id;
    memcpy(msg->content.peer_info.info.ip.addr, peer->ip.addr, IPV6_ADDR_LEN);
    msg->content.join_resp.port = peer->port;

    return msg;
}

message_t *msg_create_peer_info(const peer_t *peer) {
    LOG_INFO("Création message CODE=%d [CODE_PEER_INFO]...", CODE_PEER_INFO);

    if (!peer) {
        LOG_ERROR("Erreur création message : peer NULL");
        return NULL;
    }

    message_t *msg = message_create(CODE_PEER_INFO);
    if (!msg)
        return NULL;

    msg->content.peer_info.info.id = peer->id;
    memcpy(msg->content.peer_info.info.ip.addr, peer->ip.addr, IPV6_ADDR_LEN);
    msg->content.join_resp.port = peer->port;
    memcpy(msg->content.peer_info.info.key.key, peer->public_key.key,
           ED25519_PUB_KEY_LEN);

    return msg;
}

message_t *msg_create_peer_info_resp_ok() {
    LOG_INFO("Création message CODE=%d [CODE_PEER_INFO_RESP_OK]...",
             CODE_PEER_INFO_RESP_OK);

    message_t *msg = message_create(CODE_PEER_INFO_RESP_OK);
    if (!msg)
        return NULL;

    return msg;
}

message_t *msg_create_peer_info_resp_alt(app_id_t id) {
    LOG_INFO("Création message CODE=%d [CODE_PEER_INFO_RESP_ALT]...",
             CODE_PEER_INFO_RESP_ALT);

    message_t *msg = message_create(CODE_PEER_INFO_RESP_ALT);
    if (!msg)
        return NULL;

    msg->content.peer_info_resp.assigned_id = id;

    return msg;
}

message_t *msg_create_new_peer_info(const info_t *new_peer) {
    LOG_INFO("Création message CODE=%d [CODE_NEW_PEER_INFO]...", CODE_NEW_PEER_INFO);

    if (!new_peer) {
        LOG_ERROR("Erreur création message : new_peer NULL");
        return NULL;
    }

    message_t *msg = message_create(CODE_NEW_PEER_INFO);
    if (!msg)
        return NULL;

    msg->content.peer_info.info.id = new_peer->id;
    memcpy(msg->content.peer_info.info.ip.addr, new_peer->ip.addr, IPV6_ADDR_LEN);
    msg->content.join_resp.port = new_peer->port;
    memcpy(msg->content.peer_info.info.key.key, new_peer->key.key, ED25519_PUB_KEY_LEN);

    return msg;
}

message_t *msg_create_system_info(const peer_t *peer) {
    LOG_INFO("Création message CODE=%d [CODE_SYSTEM_INFO]...", CODE_SYSTEM_INFO);

    if (!peer) {
        LOG_ERROR("Erreur création message : peer NULL");
        return NULL;
    }

    message_t *msg = message_create(CODE_SYSTEM_INFO);
    if (!msg)
        return NULL;

    msg->content.system_info.id = peer->id;
    memcpy(msg->content.system_info.auction_ip.addr, peer->auctions_ip.addr,
           IPV6_ADDR_LEN);
    msg->content.system_info.auction_port = peer->auctions_port;
    msg->content.system_info.peer_count = peer->peer_list.count + 1; // + soi-même

    return msg;
}

message_t *msg_create_auction_start(app_id_t supervisor_id, numv_t auction_id,
                                    prix_t price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_START]...", CODE_AUCTION_START);

    message_t *msg = message_create(CODE_AUCTION_START);
    if (!msg)
        return NULL;

    msg->content.auction.id = supervisor_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = price;

    return msg;
}

message_t *msg_create_auction_bid(app_id_t bidder_id, numv_t auction_id,
                                  prix_t new_price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_BID]...", CODE_AUCTION_BID);

    message_t *msg = message_create(CODE_AUCTION_BID);
    if (!msg)
        return NULL;

    msg->content.auction.id = bidder_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = new_price;

    return msg;
}

message_t *msg_create_auction_bid_confirm(app_id_t bidder_id, numv_t auction_id,
                                          prix_t new_price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_BID_CONFIRM]...",
             CODE_AUCTION_BID_CONFIRM);

    message_t *msg = message_create(CODE_AUCTION_BID_CONFIRM);
    if (!msg)
        return NULL;

    msg->content.auction.id = bidder_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = new_price;

    return msg;
}

message_t *msg_create_auction_almost_end(app_id_t supervisor_id, numv_t auction_id,
                                         prix_t last_price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_ALMOST_END]...",
             CODE_AUCTION_ALMOST_END);

    message_t *msg = message_create(CODE_AUCTION_ALMOST_END);
    if (!msg)
        return NULL;

    msg->content.auction.id = supervisor_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = last_price;

    return msg;
}

message_t *msg_create_auction_end(app_id_t winner_id, numv_t auction_id,
                                  prix_t last_price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_END]...", CODE_AUCTION_END);

    message_t *msg = message_create(CODE_AUCTION_END);
    if (!msg)
        return NULL;

    msg->content.auction.id = winner_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = last_price;

    return msg;
}

message_t *msg_create_system_leave(app_id_t id) {
    LOG_INFO("Création message CODE=%d [CODE_SYSTEM_LEAVE]...", CODE_SYSTEM_LEAVE);

    message_t *msg = message_create(CODE_SYSTEM_LEAVE);
    if (!msg)
        return NULL;

    msg->content.leave.id = id;

    return msg;
}

message_t *msg_create_auction_reject_concurrent(app_id_t rejected_id, numv_t auction_id,
                                                prix_t price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_REJECT_CONCURRENT]...",
             CODE_AUCTION_REJECT_CONCURRENT);

    message_t *msg = message_create(CODE_AUCTION_REJECT_CONCURRENT);
    if (!msg)
        return NULL;

    msg->content.auction.id = rejected_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = price;

    return msg;
}

message_t *msg_create_auction_reject_invalid(app_id_t rejected_id, numv_t auction_id,
                                             prix_t price) {
    LOG_INFO("Création message CODE=%d [CODE_AUCTION_REJECT_INVALID]...",
             CODE_AUCTION_REJECT_INVALID);

    message_t *msg = message_create(CODE_AUCTION_REJECT_INVALID);
    if (!msg)
        return NULL;

    msg->content.auction.id = rejected_id;
    msg->content.auction.auction_id = auction_id;
    msg->content.auction.price = price;

    return msg;
}

message_t *msg_create_supervisor_afk(const peer_t *supervisor, numv_t auction_id) {
    if (!supervisor)
        return NULL;

    message_t *msg = message_create(CODE_SUPERVISOR_AFK);
    if (!msg)
        return NULL;

    msg->content.peer_mgmt.afk.auction_id = auction_id;
    msg->content.peer_mgmt.id = supervisor->id;

    return msg;
}

message_t *msg_create_cancel_transaction(const peer_t *supervisor, numv_t auction_id) {
    if (!supervisor)
        return NULL;

    message_t *msg = message_create(CODE_CANCEL_TRANSACTION);
    if (!msg)
        return NULL;

    msg->content.peer_mgmt.afk.auction_id = auction_id;
    msg->content.peer_mgmt.id = supervisor->id;

    return msg;
}

message_t *msg_create_peers_remove(const peer_t *peer, nb_t afk_count, app_id_t *left) {
    if (!peer || !left)
        return NULL;

    message_t *msg = message_create(CODE_PEERS_REMOVE);
    if (!msg)
        return NULL;

    msg->content.peer_mgmt.id = peer->id;
    msg->content.peer_mgmt.afk.peers.peer_count = afk_count;
    msg->content.peer_mgmt.afk.peers.peer_ids = left;

    return msg;
}

// Copie une signature
int copy_signature(sign_t *dest, const sign_t *src) {
    dest->length = src->length;
    if (dest->length > 0) {
        dest->data = malloc(dest->length);
        if (!dest->data)
            return -1;
        memcpy(dest->data, src->data, dest->length);
    } else {
        dest->data = NULL;
    }
    return 0;
}

// Copie un tableau de signatures
int copy_peer_signatures(peer_signature_t **dest, const peer_signature_t *src,
                         nb_t count) {
    *dest = malloc(sizeof(peer_signature_t) * count);
    if (!*dest)
        return -1;

    for (nb_t i = 0; i < count; i++) {
        (*dest)[i].id = src[i].id;
        if (copy_signature(&(*dest)[i].signature, &src[i].signature) < 0) {
            for (nb_t j = 0; j < i; j++)
                free((*dest)[j].signature.data);
            free(*dest);
            *dest = NULL;
            return -1;
        }
    }
    return 0;
}
