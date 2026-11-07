#include "../include/message_serializer.h"
#include "../include/debug.h"
#include "../include/network.h"
#include "../include/utils.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

static void msg_serialize_print_success(size_t *out_length) {
    LOG_INFO("Sérialisation réussie. Taille totale : %zu octets.", *out_length);
}

// CODE 1
static serialize_result_t msg_serialize_validation(const message_t *msg, uint8_t *buffer,
                                                   size_t buffer_size,
                                                   size_t *out_length) {

    const lmess_t lmess = msg->content.validation.message.length;
    const lsign_t lsig = msg->content.validation.signature.length;

    if (*out_length + sizeof(app_id_t) + sizeof(lmess_t) + lmess + sizeof(lsign_t) +
            lsig >
        buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.validation.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    buffer[*out_length] = lmess;
    *out_length += sizeof(lmess_t);

    memcpy(buffer + *out_length, msg->content.validation.message.data, lmess);
    *out_length += lmess;

    buffer[*out_length] = lsig;
    *out_length += sizeof(lsign_t);

    memcpy(buffer + *out_length, msg->content.validation.signature.data, lsig);
    *out_length += lsig;

    msg_serialize_print_success(out_length);

    return SERIALIZE_SUCCESS;
}

// CODE 2
static serialize_result_t msg_serialize_consensus(const message_t *msg, uint8_t *buffer,
                                                  size_t buffer_size,
                                                  size_t *out_length) {

    const lmess_t lmess = msg->content.consensus.message.length;
    const lsign_t lsig = msg->content.validation.signature.length;
    const nb_t peer_count = msg->content.consensus.peer_count;

    if (*out_length + sizeof(app_id_t) + sizeof(lmess_t) + lmess + sizeof(lsign_t) +
            lsig + sizeof(nb_t) >
        buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.consensus.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    buffer[*out_length] = lmess;
    *out_length += sizeof(lmess_t);

    memcpy(buffer + *out_length, msg->content.consensus.message.data, lmess);
    *out_length += lmess;

    buffer[*out_length] = lsig;
    *out_length += sizeof(lsign_t);

    memcpy(buffer + *out_length, msg->content.consensus.signature.data, lsig);
    *out_length += lsig;

    nb_t net_peer_count = htons(peer_count);
    memcpy(buffer + *out_length, &net_peer_count, sizeof(nb_t));
    *out_length += sizeof(nb_t);

    int add_count = peer_count > 3 ? 3 : peer_count;
    for (int i = 0; i < add_count; i++) {
        if (*out_length + sizeof(app_id_t) + sizeof(lsign_t) +
                msg->content.consensus.peer_signatures[i].signature.length >
            buffer_size) {
            return SERIALIZE_BUFFER_TOO_SMALL;
        }

        lsign_t peer_sig_len = msg->content.consensus.peer_signatures[i].signature.length;

        app_id_t net_peer_id = htons(msg->content.consensus.peer_signatures[i].id);
        memcpy(buffer + *out_length, &net_peer_id, sizeof(app_id_t));
        *out_length += sizeof(app_id_t);

        lsign_t net_peer_sig_len = htons(peer_sig_len);
        memcpy(buffer + *out_length, &net_peer_sig_len, sizeof(lsign_t));
        *out_length += sizeof(lsign_t);

        memcpy(buffer + *out_length,
               msg->content.consensus.peer_signatures[i].signature.data, peer_sig_len);
        *out_length += peer_sig_len;
    }

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 20
static serialize_result_t msg_serialize_consensus_part(const message_t *msg,
                                                       uint8_t *buffer,
                                                       size_t buffer_size,
                                                       size_t *out_length) {

    const nb_t peer_count = msg->content.consensus.peer_count;

    for (nb_t i = 0; i < peer_count; i++) {
        if (*out_length + sizeof(app_id_t) + sizeof(lsign_t) +
                msg->content.consensus.peer_signatures[i].signature.length >
            buffer_size)
            return SERIALIZE_BUFFER_TOO_SMALL;
        app_id_t peer_id = msg->content.consensus.peer_signatures[i].id;
        lsign_t peer_sig_len = msg->content.consensus.peer_signatures[i].signature.length;
        const uint8_t *peer_sig_data =
            msg->content.consensus.peer_signatures[i].signature.data;

        app_id_t net_peer_id = htons(peer_id);
        memcpy(buffer + *out_length, &net_peer_id, sizeof(app_id_t));
        *out_length += sizeof(app_id_t);

        memcpy(buffer + *out_length, &peer_sig_len, sizeof(lsign_t));
        *out_length += sizeof(lsign_t);

        memcpy(buffer + *out_length, &peer_sig_data, peer_sig_len);
        *out_length += peer_sig_len;
    }

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 3
static serialize_result_t msg_serialize_join_request(const message_t *msg,
                                                     uint8_t *buffer, size_t buffer_size,
                                                     size_t *out_length) {
    // Pas de données supplémentaires
    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 4
static serialize_result_t msg_serialize_join_response(const message_t *msg,
                                                      uint8_t *buffer, size_t buffer_size,
                                                      size_t *out_length) {

    if (*out_length + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) > buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.join_resp.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    memcpy(buffer + *out_length, &msg->content.join_resp.ip, sizeof(ip_t));
    *out_length += sizeof(ip_t);

    port_t net_port = htons(msg->content.join_resp.port);
    memcpy(buffer + *out_length, &net_port, sizeof(port_t));
    *out_length += sizeof(port_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 5
static serialize_result_t msg_serialize_peer_info(const message_t *msg, uint8_t *buffer,
                                                  size_t buffer_size,
                                                  size_t *out_length) {

    if (*out_length + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) +
            sizeof(cle_pub_t) >
        buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.peer_info.info.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    memcpy(buffer + *out_length, &msg->content.peer_info.info.ip, sizeof(ip_t));
    *out_length += sizeof(ip_t);

    port_t net_port = htons(msg->content.peer_info.info.port);
    memcpy(buffer + *out_length, &net_port, sizeof(port_t));
    *out_length += sizeof(port_t);

    memcpy(buffer + *out_length, &msg->content.peer_info.info.key, sizeof(cle_pub_t));
    *out_length += sizeof(cle_pub_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 50
static serialize_result_t msg_serialize_peer_info_resp_ok(const message_t *msg,
                                                          uint8_t *buffer,
                                                          size_t buffer_size,
                                                          size_t *out_length) {
    // Pas de données supplémentaires
    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 51
static serialize_result_t msg_serialize_peer_info_resp_alt(const message_t *msg,
                                                           uint8_t *buffer,
                                                           size_t buffer_size,
                                                           size_t *out_length) {

    if (*out_length + sizeof(app_id_t) > buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.peer_info_resp.assigned_id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 6
static serialize_result_t msg_serialize_new_peer_info(const message_t *msg,
                                                      uint8_t *buffer, size_t buffer_size,
                                                      size_t *out_length) {

    if (*out_length + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) +
            sizeof(cle_pub_t) >
        buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.peer_info.info.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    memcpy(buffer + *out_length, &msg->content.peer_info.info.ip, sizeof(ip_t));
    *out_length += sizeof(ip_t);

    port_t net_port = htons(msg->content.peer_info.info.port);
    memcpy(buffer + *out_length, &net_port, sizeof(port_t));
    *out_length += sizeof(port_t);

    memcpy(buffer + *out_length, &msg->content.peer_info.info.key, sizeof(cle_pub_t));
    *out_length += sizeof(cle_pub_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 7 Partie préfixe
static serialize_result_t msg_serialize_system_info(const message_t *msg, uint8_t *buffer,
                                                    size_t buffer_size,
                                                    size_t *out_length) {

    if (*out_length + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) + sizeof(nb_t) >
        buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.system_info.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    memcpy(buffer + *out_length, &msg->content.system_info.auction_ip, sizeof(ip_t));
    *out_length += sizeof(ip_t);

    port_t net_port = htons(msg->content.system_info.auction_port);
    memcpy(buffer + *out_length, &net_port, sizeof(port_t));
    *out_length += sizeof(port_t);

    nb_t net_peer_count = htons(msg->content.system_info.peer_count);
    memcpy(buffer + *out_length, &net_peer_count, sizeof(nb_t));
    *out_length += sizeof(nb_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

static serialize_result_t msg_serialize_auction_start(const message_t *msg,
                                                      uint8_t *buffer, size_t buffer_size,
                                                      size_t *out_length) {

    if (*out_length + sizeof(app_id_t) + sizeof(numv_t) + sizeof(prix_t) > buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.auction.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    numv_t net_auction_id = htonl(msg->content.auction.auction_id);
    memcpy(buffer + *out_length, &net_auction_id, sizeof(numv_t));
    *out_length += sizeof(numv_t);

    prix_t net_price = htonl(msg->content.auction.price);
    memcpy(buffer + *out_length, &net_price, sizeof(prix_t));
    *out_length += sizeof(prix_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

static serialize_result_t msg_serialize_auction_bid(const message_t *msg, uint8_t *buffer,
                                                    size_t buffer_size,
                                                    size_t *out_length) {
    return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
}

static serialize_result_t msg_serialize_auction_bid_confirm(const message_t *msg,
                                                            uint8_t *buffer,
                                                            size_t buffer_size,
                                                            size_t *out_length) {
    return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
}

static serialize_result_t msg_serialize_auction_almost_end(const message_t *msg,
                                                           uint8_t *buffer,
                                                           size_t buffer_size,
                                                           size_t *out_length) {
    return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
}

static serialize_result_t msg_serialize_auction_end(const message_t *msg, uint8_t *buffer,
                                                    size_t buffer_size,
                                                    size_t *out_length) {
    return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
}

static serialize_result_t msg_serialize_system_leave(const message_t *msg,
                                                     uint8_t *buffer, size_t buffer_size,
                                                     size_t *out_length) {

    if (*out_length + sizeof(app_id_t) > buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.leave.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

static serialize_result_t msg_serialize_auction_reject_concurrent(const message_t *msg,
                                                                  uint8_t *buffer,
                                                                  size_t buffer_size,
                                                                  size_t *out_length) {
    return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
}

static serialize_result_t msg_serialize_auction_reject_invalid(const message_t *msg,
                                                               uint8_t *buffer,
                                                               size_t buffer_size,
                                                               size_t *out_length) {
    return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
}

static serialize_result_t msg_serialize_afk_message(const message_t *msg, uint8_t *buffer,
                                                    size_t buffer_size,
                                                    size_t *out_length) {

    if (*out_length + sizeof(app_id_t) + sizeof(numv_t) > buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.peer_mgmt.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    numv_t net_auction_id = htonl(msg->content.peer_mgmt.afk.auction_id);
    memcpy(buffer + *out_length, &net_auction_id, sizeof(numv_t));
    *out_length += sizeof(numv_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

static serialize_result_t msg_serialize_peers_remove(const message_t *msg,
                                                     uint8_t *buffer, size_t buffer_size,
                                                     size_t *out_length) {

    nb_t peer_count = msg->content.peer_mgmt.afk.peers.peer_count;
    const app_id_t *peer_ids = msg->content.peer_mgmt.afk.peers.peer_ids;

    if (*out_length + sizeof(app_id_t) + sizeof(nb_t) + (peer_count * sizeof(app_id_t)) >
        buffer_size) {
        return SERIALIZE_BUFFER_TOO_SMALL;
    }

    app_id_t net_id = htons(msg->content.peer_mgmt.id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    nb_t net_peer_count = htons(peer_count);
    memcpy(buffer + *out_length, &net_peer_count, sizeof(nb_t));
    *out_length += sizeof(nb_t);

    for (nb_t i = 0; i < peer_count; i++) {
        app_id_t net_id_peer = htons(peer_ids[i]);
        memcpy(buffer + *out_length, &net_id_peer, sizeof(app_id_t));
        *out_length += sizeof(app_id_t);
    }

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// Sérialisation générique
serialize_result_t msg_serialize(const message_t *msg, uint8_t *buffer,
                                 size_t buffer_size, size_t *out_length) {

    LOG_INFO("Début de la sérialisation du message...");
    if (!msg || !buffer) {
        LOG_ERROR("Echec de la sérialisation : message ou buffer NULL.");
        return SERIALIZE_INVALID_ARGS;
    }

    *out_length = 0;

    // Écriture de l'en-tête fixe
    if (*out_length + 1 > buffer_size)
        return SERIALIZE_BUFFER_TOO_SMALL;
    buffer[*out_length] = msg->code;
    *out_length += 1;

    // Sérialisation selon le type de message
    switch (msg->code) {

        case CODE_VALIDATION: {
            return msg_serialize_validation(msg, buffer, buffer_size, out_length);
        }

        case CODE_CONSENSUS: {
            return msg_serialize_consensus(msg, buffer, buffer_size, out_length);
        }

        case CODE_CONSENSUS_PART: {
            return msg_serialize_consensus_part(msg, buffer, buffer_size, out_length);
        }

        case CODE_JOIN_REQUEST: {
            return msg_serialize_join_request(msg, buffer, buffer_size, out_length);
        }

        case CODE_JOIN_RESPONSE: {
            return msg_serialize_join_response(msg, buffer, buffer_size, out_length);
        }

        case CODE_PEER_INFO: {
            return msg_serialize_peer_info(msg, buffer, buffer_size, out_length);
        }

        case CODE_PEER_INFO_RESP_OK: {
            return msg_serialize_peer_info_resp_ok(msg, buffer, buffer_size, out_length);
        }

        case CODE_PEER_INFO_RESP_ALT: {
            return msg_serialize_peer_info_resp_alt(msg, buffer, buffer_size, out_length);
        }

        case CODE_NEW_PEER_INFO: {
            return msg_serialize_new_peer_info(msg, buffer, buffer_size, out_length);
        }

        case CODE_SYSTEM_INFO: {
            return msg_serialize_system_info(msg, buffer, buffer_size, out_length);
        }

        case CODE_AUCTION_START: {
            return msg_serialize_auction_start(msg, buffer, buffer_size, out_length);
        }

        case CODE_AUCTION_BID: {
            return msg_serialize_auction_bid(msg, buffer, buffer_size, out_length);
        }

        case CODE_AUCTION_BID_CONFIRM: {
            return msg_serialize_auction_bid_confirm(msg, buffer, buffer_size,
                                                     out_length);
        }

        case CODE_AUCTION_ALMOST_END: {
            return msg_serialize_auction_almost_end(msg, buffer, buffer_size, out_length);
        }

        case CODE_AUCTION_END: {
            return msg_serialize_auction_end(msg, buffer, buffer_size, out_length);
        }

        case CODE_SYSTEM_LEAVE: {
            return msg_serialize_system_leave(msg, buffer, buffer_size, out_length);
        }

        case CODE_AUCTION_REJECT_CONCURRENT: {
            return msg_serialize_auction_reject_concurrent(msg, buffer, buffer_size,
                                                           out_length);
        }

        case CODE_AUCTION_REJECT_INVALID: {
            return msg_serialize_auction_reject_invalid(msg, buffer, buffer_size,
                                                        out_length);
        }

        case CODE_SUPERVISOR_AFK: {
            LOG_ERROR("Echec de la sérialisation. Implémentation requise.");
            return SERIALIZE_INVALID_ARGS;
        }

        case CODE_CANCEL_TRANSACTION: {
            return msg_serialize_afk_message(msg, buffer, buffer_size, out_length);
        }

        case CODE_PEERS_REMOVE: {
            return msg_serialize_peers_remove(msg, buffer, buffer_size, out_length);
        }

        default:
            LOG_ERROR("Echec de la sérialisation. Code non reconnu.");
            return SERIALIZE_INVALID_ARGS;
    }
}

static void msg_deserialize_print_success(size_t out_length) {
    LOG_INFO("Désérialisation réussie. Taille totale désérialisée : %zu octets.",
             out_length);
}

static deserialize_result_t msg_deserialize_validation(const uint8_t *buffer,
                                                       size_t buffer_size,
                                                       message_t *out_msg,
                                                       size_t offset) {

    LOG_INFO("offset: %zu", offset);

    if (offset + sizeof(app_id_t) + sizeof(lmess_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    // ID
    memcpy(&out_msg->content.validation.id, buffer + offset, sizeof(app_id_t));
    out_msg->content.validation.id = ntohs(out_msg->content.validation.id);
    offset += sizeof(app_id_t);

    LOG_INFO("ID: %u, offset: %zu", out_msg->content.validation.id, offset);

    // LMESS
    out_msg->content.validation.message.length = buffer[offset++];

    LOG_INFO("LMESS: %u, offset: %zu", out_msg->content.validation.message.length,
             offset);

    // MESS
    if (offset + out_msg->content.validation.message.length > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }
    out_msg->content.validation.message.data =
        malloc(out_msg->content.validation.message.length);
    if (!out_msg->content.validation.message.data) {
        LOG_ERROR("malloc message");
        return DESERIALIZE_INVALID_DATA;
    }
    memcpy(out_msg->content.validation.message.data, buffer + offset,
           out_msg->content.validation.message.length);
    offset += out_msg->content.validation.message.length;

    // LSIG
    out_msg->content.validation.signature.length = buffer[offset++];

    // SIG
    if (offset + out_msg->content.validation.signature.length > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }
    if (out_msg->content.validation.signature.length > 0) {
        out_msg->content.validation.signature.data =
            malloc(out_msg->content.validation.signature.length);
        if (!out_msg->content.validation.signature.data) {
            LOG_ERROR("malloc signature");
            return DESERIALIZE_INVALID_DATA;
        }
        memcpy(out_msg->content.validation.signature.data, buffer + offset,
               out_msg->content.validation.signature.length);
        offset += out_msg->content.validation.signature.length;
    }

    LOG_INFO("CODE:%u ID: %u, LMESS: %u, LSIG: %u", out_msg->code,
             out_msg->content.validation.id, out_msg->content.validation.message.length,
             out_msg->content.validation.signature.length);

    msg_deserialize_print_success(offset);

    return DESERIALIZE_SUCCESS;
}

// CODE 2
static deserialize_result_t msg_deserialize_consensus(const uint8_t *buffer,
                                                      size_t buffer_size,
                                                      message_t *out_msg, size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(lmess_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.consensus.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    out_msg->content.validation.message.length = buffer[offset++];

    if (offset + out_msg->content.consensus.message.length > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }
    out_msg->content.consensus.message.data =
        malloc(out_msg->content.consensus.message.length);
    if (!out_msg->content.consensus.message.data) {
        LOG_ERROR("malloc message");
        return DESERIALIZE_INVALID_DATA;
    }
    memcpy(out_msg->content.consensus.message.data, buffer + offset,
           out_msg->content.consensus.message.length);
    offset += out_msg->content.consensus.message.length;

    out_msg->content.validation.signature.length = buffer[offset++];

    if (offset + out_msg->content.validation.signature.length > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }
    out_msg->content.consensus.signature.data =
        malloc(out_msg->content.consensus.signature.length);
    if (!out_msg->content.consensus.signature.data) {
        LOG_ERROR("malloc signature");
        return DESERIALIZE_INVALID_DATA;
    }
    memcpy(out_msg->content.consensus.signature.data, buffer + offset,
           out_msg->content.consensus.signature.length);
    offset += out_msg->content.consensus.signature.length;

    nb_t *net_peer_count = &out_msg->content.consensus.peer_count;
    memcpy(net_peer_count, buffer + offset, sizeof(nb_t));
    *net_peer_count = ntohs(*net_peer_count);
    offset += sizeof(nb_t);

    // int add_count = *net_peer_count > 3 ? 3 : 0;

    // for (nb_t i = 0; i < add_count; i++) {
    //     if (offset + sizeof(app_id_t) + sizeof(lsign_t) > buffer_size) {
    //         return DESERIALIZE_INVALID_DATA;
    //     }

    //     app_id_t *net_peer_id = &out_msg->content.consensus.peer_signatures[i].id;
    //     memcpy(net_peer_id, buffer + offset, sizeof(app_id_t));
    //     *net_peer_id = ntohs(*net_peer_id);
    //     offset += sizeof(app_id_t);

    //     lsign_t *net_peer_sig_len =
    //         &out_msg->content.consensus.peer_signatures[i].signature.length;
    //     memcpy(net_peer_sig_len, buffer + offset, sizeof(lsign_t));
    //     offset += sizeof(lsign_t);

    //     if (offset + *net_peer_sig_len > buffer_size) {
    //         return DESERIALIZE_INVALID_DATA;
    //     }
    //     memcpy(out_msg->content.consensus.peer_signatures[i].signature.data,
    //            buffer + offset, *net_peer_sig_len);
    //     offset += *net_peer_sig_len;
    // }

    LOG_INFO("CODE:%u ID: %u, LMESS: %u, LSIG: %u, NB_PEERS: %u", out_msg->code, *net_id,
             out_msg->content.consensus.message.length,
             out_msg->content.consensus.signature.length, *net_peer_count);

    msg_deserialize_print_success(offset);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_consensus_part(const uint8_t *buffer,
                                                           size_t buffer_size,
                                                           message_t *out_msg,
                                                           size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(lsign_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_peer_id = &out_msg->content.consensus.id;
    memcpy(net_peer_id, buffer + offset, sizeof(app_id_t));
    *net_peer_id = ntohs(*net_peer_id);
    offset += sizeof(app_id_t);

    nb_t *net_peer_count = &out_msg->content.consensus.peer_count;
    memcpy(net_peer_count, buffer + offset, sizeof(nb_t));
    *net_peer_count = ntohs(*net_peer_count);
    offset += sizeof(nb_t);

    for (nb_t i = 0; i < *net_peer_count; i++) {
        if (offset + sizeof(app_id_t) + sizeof(lsign_t) > buffer_size) {
            return DESERIALIZE_INVALID_DATA;
        }

        lsign_t *net_peer_sig_len =
            &out_msg->content.consensus.peer_signatures[i].signature.length;
        memcpy(net_peer_sig_len, buffer + offset, sizeof(lsign_t));
        offset += sizeof(lsign_t);

        if (offset + *net_peer_sig_len > buffer_size) {
            return DESERIALIZE_INVALID_DATA;
        }
        out_msg->content.consensus.peer_signatures[i].signature.data =
            malloc(*net_peer_sig_len);
        if (!out_msg->content.consensus.peer_signatures[i].signature.data) {
            LOG_ERROR("malloc signature");
            return DESERIALIZE_INVALID_DATA;
        }
        memcpy(out_msg->content.consensus.peer_signatures[i].signature.data,
               buffer + offset, *net_peer_sig_len);
        offset += *net_peer_sig_len;
    }

    msg_deserialize_print_success(offset);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_join_request(const uint8_t *buffer,
                                                         size_t buffer_size,
                                                         message_t *out_msg,
                                                         size_t offset) {
    // Pas de données supplémentaires
    msg_deserialize_print_success(offset);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_join_response(const uint8_t *buffer,
                                                          size_t buffer_size,
                                                          message_t *out_msg,
                                                          size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) > buffer_size)
        return DESERIALIZE_INVALID_DATA;

    app_id_t *net_id = &out_msg->content.join_resp.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    ip_t *ip = &out_msg->content.join_resp.ip;
    memcpy(ip, buffer + offset, sizeof(ip_t));
    offset += sizeof(ip_t);

    port_t *net_port = &out_msg->content.join_resp.port;
    memcpy(net_port, buffer + offset, sizeof(port_t));
    *net_port = ntohs(*net_port);
    offset += sizeof(port_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID : %u, IP : %s, Port : %u", *net_id, ip_to_str_static(ip), *net_port);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_peer_info(const uint8_t *buffer,
                                                      size_t buffer_size,
                                                      message_t *out_msg, size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) + sizeof(cle_pub_t) >
        buffer_size)
        return DESERIALIZE_INVALID_DATA;

    app_id_t *net_id = &out_msg->content.peer_info.info.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    ip_t *ip = &out_msg->content.peer_info.info.ip;
    memcpy(ip, buffer + offset, sizeof(ip_t));
    offset += sizeof(ip_t);

    port_t *net_port = &out_msg->content.peer_info.info.port;
    memcpy(net_port, buffer + offset, sizeof(port_t));
    *net_port = ntohs(*net_port);
    offset += sizeof(port_t);

    cle_pub_t *key = &out_msg->content.peer_info.info.key;
    memcpy(key, buffer + offset, sizeof(cle_pub_t));
    offset += sizeof(cle_pub_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID : %u, IP : %s, Port : %u, Clé publique : %s", *net_id,
             ip_to_str_static(ip), *net_port, key_without_newline(key->key, 0));

    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_peer_info_resp_ok(const uint8_t *buffer,
                                                              size_t buffer_size,
                                                              message_t *out_msg,
                                                              size_t offset) {
    // Pas de données supplémentaires
    msg_deserialize_print_success(offset);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t
msg_deserialize_peer_info_resp_alt(const uint8_t *buffer, size_t buffer_size,
                                   message_t *out_msg, size_t offset, bool skip_code) {

    if (!skip_code) { // Cas spécial car en 2 temps
        return DESERIALIZE_SUCCESS;
    }

    if (offset + sizeof(app_id_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.peer_info_resp.assigned_id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID : %u", *net_id);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_new_peer_info(const uint8_t *buffer,
                                                          size_t buffer_size,
                                                          message_t *out_msg,
                                                          size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) + sizeof(cle_pub_t) >
        buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.peer_info.info.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    ip_t *ip = &out_msg->content.peer_info.info.ip;
    memcpy(ip, buffer + offset, sizeof(ip_t));
    offset += sizeof(ip_t);

    port_t *net_port = &out_msg->content.peer_info.info.port;
    memcpy(net_port, buffer + offset, sizeof(port_t));
    *net_port = ntohs(*net_port);
    offset += sizeof(port_t);

    cle_pub_t *key = &out_msg->content.peer_info.info.key;
    memcpy(key, buffer + offset, sizeof(cle_pub_t));
    offset += sizeof(cle_pub_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID : %u, IP : %s, Port : %u, Clé publique : %s", *net_id,
             ip_to_str_static(ip), *net_port, key_without_newline(key->key, 0));
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_system_info(const uint8_t *buffer,
                                                        size_t buffer_size,
                                                        message_t *out_msg,
                                                        size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(ip_t) + sizeof(port_t) + sizeof(nb_t) >
        buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.system_info.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    ip_t *ip = &out_msg->content.system_info.auction_ip;
    memcpy(ip, buffer + offset, sizeof(ip_t));
    offset += sizeof(ip_t);

    port_t *net_port = &out_msg->content.system_info.auction_port;
    memcpy(net_port, buffer + offset, sizeof(port_t));
    *net_port = ntohs(*net_port);
    offset += sizeof(port_t);

    nb_t *net_peer_count = &out_msg->content.system_info.peer_count;
    memcpy(net_peer_count, buffer + offset, sizeof(nb_t));
    *net_peer_count = ntohs(*net_peer_count);
    offset += sizeof(nb_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID : %u, IP : %s, Port : %u, Count : %u", *net_id, ip_to_str_static(ip),
             *net_port, *net_peer_count);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_auction_start(const uint8_t *buffer,
                                                          size_t buffer_size,
                                                          message_t *out_msg,
                                                          size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(numv_t) + sizeof(prix_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.auction.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    numv_t *net_auction_id = &out_msg->content.auction.auction_id;
    memcpy(net_auction_id, buffer + offset, sizeof(numv_t));
    *net_auction_id = ntohl(*net_auction_id);
    offset += sizeof(numv_t);

    prix_t *net_price = &out_msg->content.auction.price;
    memcpy(net_price, buffer + offset, sizeof(prix_t));
    *net_price = ntohl(*net_price);
    offset += sizeof(prix_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID : %u, NUMV : %u, Prix : %u", *net_id, *net_auction_id, *net_price);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_auction_bid(const uint8_t *buffer,
                                                        size_t buffer_size,
                                                        message_t *out_msg,
                                                        size_t offset) {
    return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
}

static deserialize_result_t msg_deserialize_auction_bid_confirm(const uint8_t *buffer,
                                                                size_t buffer_size,
                                                                message_t *out_msg,
                                                                size_t offset) {
    return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
}

static deserialize_result_t msg_deserialize_auction_almost_end(const uint8_t *buffer,
                                                               size_t buffer_size,
                                                               message_t *out_msg,
                                                               size_t offset) {
    return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
}

static deserialize_result_t msg_deserialize_auction_end(const uint8_t *buffer,
                                                        size_t buffer_size,
                                                        message_t *out_msg,
                                                        size_t offset) {
    return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
}

// CODE 13
static deserialize_result_t msg_deserialize_system_leave(const uint8_t *buffer,
                                                         size_t buffer_size,
                                                         message_t *out_msg,
                                                         size_t offset) {

    if (offset + sizeof(app_id_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.leave.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    msg_deserialize_print_success(offset);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t
msg_deserialize_auction_reject_concurrent(const uint8_t *buffer, size_t buffer_size,
                                          message_t *out_msg, size_t offset) {
    return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
}

static deserialize_result_t msg_deserialize_auction_reject_invalid(const uint8_t *buffer,
                                                                   size_t buffer_size,
                                                                   message_t *out_msg,
                                                                   size_t offset) {
    return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
}

// CODE 16
static deserialize_result_t msg_deserialize_afk_message(const uint8_t *buffer,
                                                        size_t buffer_size,
                                                        message_t *out_msg,
                                                        size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(numv_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.peer_mgmt.id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    numv_t *net_auction_id = &out_msg->content.peer_mgmt.afk.auction_id;
    memcpy(net_auction_id, buffer + offset, sizeof(numv_t));
    *net_auction_id = ntohl(*net_auction_id);
    offset += sizeof(numv_t);

    msg_deserialize_print_success(offset);
    LOG_INFO("ID: %u, NUMV: %u", *net_id, *net_auction_id);
    return DESERIALIZE_SUCCESS;
}

static deserialize_result_t msg_deserialize_peers_remove(const uint8_t *buffer,
                                                         size_t buffer_size,
                                                         message_t *out_msg,
                                                         size_t offset) {

    if (offset + sizeof(app_id_t) + sizeof(nb_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    app_id_t *net_id = &out_msg->content.peer_mgmt.id;
    memcpy(&*net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    nb_t *net_peer_count = &out_msg->content.peer_mgmt.afk.peers.peer_count;
    memcpy(&*net_peer_count, buffer + offset, sizeof(nb_t));
    *net_peer_count = ntohs(*net_peer_count);
    offset += sizeof(nb_t);

    if (offset + *net_peer_count * sizeof(app_id_t) > buffer_size) {
        return DESERIALIZE_INVALID_DATA;
    }

    out_msg->content.peer_mgmt.afk.peers.peer_ids =
        malloc(*net_peer_count * sizeof(app_id_t));
    if (!out_msg->content.peer_mgmt.afk.peers.peer_ids) {
        LOG_ERROR("malloc peer_ids");
        return DESERIALIZE_INVALID_DATA;
    }

    for (nb_t i = 0; i < *net_peer_count; i++) {
        app_id_t *net_peer_id = &out_msg->content.peer_mgmt.afk.peers.peer_ids[i];
        memcpy(net_peer_id, buffer + offset, sizeof(app_id_t));
        *net_peer_id = ntohs(*net_peer_id);
        offset += sizeof(app_id_t);
    }

    msg_deserialize_print_success(offset);
    LOG_INFO("ID: %u, Nombre de pairs à retirer: %u", *net_id,
             out_msg->content.peer_mgmt.afk.peers.peer_count);
    return DESERIALIZE_SUCCESS;
}

// Désérialisation générique
deserialize_result_t msg_deserialize(const uint8_t *buffer, size_t buffer_size,
                                     message_t *out_msg, bool skip_code,
                                     code_t skip_code_val) {
    LOG_INFO("Début de la desérialisation du message...");
    if (!skip_code && (!buffer || buffer_size < sizeof(code_t))) {
        LOG_ERROR("Erreur : buffer ou buffer_size invalide.");
        return DESERIALIZE_INVALID_DATA;

    } else if (!out_msg) {
        LOG_ERROR("Erreur : out_msg NULL.");
        return DESERIALIZE_INVALID_DATA;
    }

    LOG_INFO("Taille du message à désérialiser : %zu octets", buffer_size);
    size_t offset = 0;

    // Lecture de l'en-tête fixe
    if (skip_code) {
        out_msg->code = skip_code_val;
        buffer_size += sizeof(code_t);
    } else {
        memcpy(&out_msg->code, buffer + offset, sizeof(code_t));
        offset += sizeof(code_t);
    }

    // Désérialisation selon le type
    switch (out_msg->code) {

        case CODE_VALIDATION: {
            return msg_deserialize_validation(buffer, buffer_size, out_msg, offset);
        }

        case CODE_CONSENSUS: {
            return msg_deserialize_consensus(buffer, buffer_size, out_msg, offset);
        }

        case CODE_CONSENSUS_PART: {
            return msg_deserialize_consensus_part(buffer, buffer_size, out_msg, offset);
        }

        case CODE_JOIN_REQUEST: {
            return msg_deserialize_join_request(buffer, buffer_size, out_msg, offset);
        }

        case CODE_JOIN_RESPONSE: {
            return msg_deserialize_join_response(buffer, buffer_size, out_msg, offset);
        }

        case CODE_PEER_INFO: {
            return msg_deserialize_peer_info(buffer, buffer_size, out_msg, offset);
        }

        case CODE_PEER_INFO_RESP_OK: {
            return msg_deserialize_peer_info_resp_ok(buffer, buffer_size, out_msg,
                                                     offset);
        }

        case CODE_PEER_INFO_RESP_ALT: {
            return msg_deserialize_peer_info_resp_alt(buffer, buffer_size, out_msg,
                                                      offset, skip_code);
        }

        case CODE_NEW_PEER_INFO: {
            return msg_deserialize_new_peer_info(buffer, buffer_size, out_msg, offset);
        }

        case CODE_SYSTEM_INFO: {
            return msg_deserialize_system_info(buffer, buffer_size, out_msg, offset);
        }

        case CODE_AUCTION_START: {
            return msg_deserialize_auction_start(buffer, buffer_size, out_msg, offset);
        }

        case CODE_AUCTION_BID:
            return msg_deserialize_auction_bid(buffer, buffer_size, out_msg, offset);

        case CODE_AUCTION_BID_CONFIRM:
            return msg_deserialize_auction_bid_confirm(buffer, buffer_size, out_msg,
                                                       offset);

        case CODE_AUCTION_ALMOST_END:
            return msg_deserialize_auction_almost_end(buffer, buffer_size, out_msg,
                                                      offset);

        case CODE_AUCTION_END:
            return msg_deserialize_auction_end(buffer, buffer_size, out_msg, offset);

        case CODE_AUCTION_REJECT_CONCURRENT:
            return msg_deserialize_auction_reject_concurrent(buffer, buffer_size, out_msg,
                                                             offset);

        case CODE_AUCTION_REJECT_INVALID:
            return msg_deserialize_auction_reject_invalid(buffer, buffer_size, out_msg,
                                                          offset);

        case CODE_SYSTEM_LEAVE: {
            return msg_deserialize_system_leave(buffer, buffer_size, out_msg, offset);
        }

        case CODE_SUPERVISOR_AFK:
            LOG_ERROR("Echec de la desérialisation. Implémentation requise.");
            return DESERIALIZE_INVALID_DATA;

        case CODE_CANCEL_TRANSACTION:
            return msg_deserialize_afk_message(buffer, buffer_size, out_msg, offset);

        case CODE_PEERS_REMOVE: {
            return msg_deserialize_peers_remove(buffer, buffer_size, out_msg, offset);
        }

        default:
            LOG_ERROR("Echec. Code non reconnu.");
            return DESERIALIZE_INVALID_DATA;
    }
}

// CODE 7 Partie sufixe
serialize_result_t bloc_info_serialize(const info_t *info, uint8_t *buffer,
                                       size_t buffer_size, size_t *out_length) {
    if (!info || !buffer) {
        LOG_ERROR("Erreur : info ou buffer NULL.");
        return SERIALIZE_INVALID_ARGS;

    } else if (buffer_size < bloc_info_size()) {
        LOG_ERROR("Erreur : buffer_size trop petit.");
        return SERIALIZE_BUFFER_TOO_SMALL;
    }
    *out_length = 0;

    app_id_t net_id = htons(info->id);
    memcpy(buffer + *out_length, &net_id, sizeof(app_id_t));
    *out_length += sizeof(app_id_t);

    memcpy(buffer + *out_length, &info->ip, sizeof(ip_t));
    *out_length += sizeof(ip_t);

    port_t net_port = htons(info->port);
    memcpy(buffer + *out_length, &net_port, sizeof(port_t));
    *out_length += sizeof(port_t);

    memcpy(buffer + *out_length, &info->key, sizeof(cle_pub_t));
    *out_length += sizeof(cle_pub_t);

    msg_serialize_print_success(out_length);
    return SERIALIZE_SUCCESS;
}

// CODE 7 Partie sufixe
deserialize_result_t bloc_info_deserialize(const uint8_t *buffer, size_t buffer_size,
                                           info_t *out_info) {
    if (!buffer || buffer_size < bloc_info_size()) {
        LOG_ERROR("Erreur : buffer NULL ou buffer_size trop petit.");
        return DESERIALIZE_INVALID_DATA;

    } else if (!out_info) {
        LOG_ERROR("Erreur : out_info NULL.");
        return DESERIALIZE_INVALID_DATA;
    }

    LOG_INFO("Taille du message à désérialiser : %zu octets", buffer_size);
    size_t offset = 0;

    app_id_t *net_id = &out_info->id;
    memcpy(net_id, buffer + offset, sizeof(app_id_t));
    *net_id = ntohs(*net_id);
    offset += sizeof(app_id_t);

    memcpy(&out_info->ip, buffer + offset, sizeof(ip_t));
    offset += sizeof(ip_t);

    port_t *net_port = &out_info->port;
    memcpy(net_port, buffer + offset, sizeof(port_t));
    *net_port = ntohs(*net_port);
    offset += sizeof(port_t);

    memcpy(&out_info->key, buffer + offset, sizeof(cle_pub_t));
    offset += sizeof(cle_pub_t);

    msg_deserialize_print_success(offset);

    LOG_INFO("ID : %u, IP : %s, Port : %u, Clé publique : %s", out_info->id,
             ip_to_str_static(&out_info->ip), out_info->port,
             key_without_newline(out_info->key.key, 0));
    return DESERIALIZE_SUCCESS;
}