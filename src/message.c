#include "../include/message.h"
#include "../include/crypto.h"
#include "../include/debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

message_t *message_create(message_codes_t code) {
    message_t *msg = malloc(sizeof(message_t));
    if (!msg) {
        LOG_ERROR("Erreur création message");
        return NULL;
    }

    memset(msg, 0, sizeof(message_t));
    msg->code = code;
    return msg;
}

void message_free(message_t *msg) {
    if (!msg)
        return;

    switch (msg->code) {
        case CODE_VALIDATION:
            if (msg->content.validation.message.data) {
                free(msg->content.validation.message.data);
                msg->content.validation.message.data = NULL;
            }
            if (msg->content.validation.signature.data) {
                free(msg->content.validation.signature.data);
                msg->content.validation.signature.data = NULL;
            }
            break;
        case CODE_CONSENSUS:
            if (msg->content.consensus.message.data) {
                free(msg->content.consensus.message.data);
                msg->content.consensus.message.data = NULL;
            }
            if (msg->content.consensus.signature.data) {
                free(msg->content.consensus.signature.data);
                msg->content.consensus.signature.data = NULL;
            }
            if (msg->content.consensus.peer_signatures) {
                // for (size_t i = 0; i < msg->content.consensus.peer_count; i++) {
                //     if (msg->content.consensus.peer_signatures[i].signature.data) {
                //         free(msg->content.consensus.peer_signatures[i].signature.data);
                //         msg->content.consensus.peer_signatures[i].signature.data =
                //         NULL;
                //     }
                // }
                free(msg->content.consensus.peer_signatures);
                msg->content.consensus.peer_signatures = NULL;
            }
            break;
        case CODE_CONSENSUS_PART: // 20
            if (msg->content.consensus.peer_signatures) {
                for (size_t i = 0; i < msg->content.consensus.peer_count; i++) {
                    if (msg->content.consensus.peer_signatures[i].signature.data) {
                        free(msg->content.consensus.peer_signatures[i].signature.data);
                        msg->content.consensus.peer_signatures[i].signature.data = NULL;
                    }
                }
                free(msg->content.consensus.peer_signatures);
                msg->content.consensus.peer_signatures = NULL;
            }
            break;
        case CODE_PEERS_REMOVE: // 18
            free_peer_ids_list(&msg->content.peer_mgmt);
            break;
        default:
            break;
    }
    free(msg);
}
/**
 * Méthode permettant de vérifier la signature d'un message avec la clé publique fournie.
 * @param[in] msg pointeur vers le message à vérifier
 * @param[in] public_key pointeur vers la clé publique utilisée pour la vérification
 */
bool message_verify_signature(const message_t *msg, const cle_pub_t *public_key) {
    if (msg == NULL || msg->code == 0 || public_key == NULL) {
        LOG_ERROR("Paramètres invalides: pointeurs NULL fournis");
        return false;
    }
    bool ret = true;
    EVP_PKEY *pub_key = NULL;
    uint8_t *clone_key = malloc(ED25519_PUB_KEY_LEN);
    if (clone_key == NULL) {
        LOG_ERROR("Échec d'allocation mémoire pour la duplication de la clé publique");
        return false;
    }
    memcpy(clone_key, public_key->key, ED25519_PUB_KEY_LEN);

    if (clone_key == NULL) {
        LOG_ERROR("La duplication de la clé publique a échoué");
        return false;
    }
    // Tentative de conversion de la clé en structure EVP_PKEY
    if (char_to_evp_pkey(&clone_key, ED25519_PUB_KEY_LEN, 0, &pub_key) != 0) {
        LOG_ERROR("Conversion de la clé privée en EVP_PKEY échouée");
        ret = false;
        goto cleanup_return;
    }
    int res = 0;
    if (msg->code == CODE_VALIDATION) {
        res = verify_signature_with_key(pub_key, &msg->content.validation.message,
                                        &msg->content.validation.signature);
        if (res != 0) {
            LOG_ERROR(
                "Échec lors de la vérification de signature du message de validation");

            ret = false;
            goto cleanup_return;
        }
    } else if (msg->code == CODE_CONSENSUS || msg->code == CODE_CONSENSUS_PART) {
        res = verify_signature_with_key(pub_key, &msg->content.consensus.message,
                                        &msg->content.consensus.signature);
        if (res != 0) {
            LOG_ERROR(
                "Échec lors de la vérification de signature du message de consensus");
            ret = false;
            goto cleanup_return;
        }
    } else {
        LOG_ERROR(
            "Échec de vérification de signature: type de message non supporté (code: %d)",
            message_codes_to_string(msg->code));
        ret = false;
        goto cleanup_return;
    }

cleanup_return:
    if (clone_key != NULL) {
        free(clone_key);
    }
    if (pub_key != NULL) {
        EVP_PKEY_free(pub_key);
    }
    return ret;
}
/**
 * Méthode permettant de signer un message avec la clé privée fournie.
 * @param[in,out] msg pointeur vers le message à signer
 * @param[in] private_key pointeur vers la clé privée utilisée pour la signature
 */
int message_sign(message_t *msg, EVP_PKEY *pkey) {
    if (msg == NULL || msg->code == 0 || pkey == NULL) {
        LOG_ERROR("Paramètres invalides: pointeurs NULL fournis");
        return -1;
    }
    // uint8_t *clone_key = malloc(ED25519_PRIV_KEY_LEN);
    // if (clone_key == NULL) {
    //     LOG_ERROR("Échec d'allocation mémoire pour la duplication de la clé privée");
    //     return -1;
    // }
    int ret = 0;
    // memcpy(clone_key, private_key->key, ED25519_PRIV_KEY_LEN);
    // if (clone_key == NULL) {
    //     LOG_ERROR("La duplication de la clé publique a échoué");
    //     return -1;
    // }
    // EVP_PKEY *pv_key = NULL;
    // if (char_to_evp_pkey(&clone_key, 1, &pv_key) != 0) {
    //     LOG_ERROR("Conversion de la clé publlique en EVP_PKEY échouée");
    //     ret = -1;
    //     goto cleanup_return;
    // }

    if (msg->code == CODE_VALIDATION) {
        if (sign_message_with_key(pkey, &msg->content.validation.message,
                                  &msg->content.validation.signature) != 0) {
            LOG_ERROR("Échec lors de la signature du message de validation");
            ret = -1;
            goto cleanup_return;
        }
    } else if (msg->code == CODE_CONSENSUS || msg->code == CODE_CONSENSUS_PART) {
        if (sign_message_with_key(pkey, &msg->content.consensus.message,
                                  &msg->content.consensus.signature) != 0) {
            LOG_ERROR("Échec lors de la signature du message de consensus");
            ret = -1;
            goto cleanup_return;
        }
    } else {
        LOG_ERROR(
            "Échec de vérification de signature: type de message non supporté (code: %d)",
            message_codes_to_string(msg->code));
        ret = -1;
        goto cleanup_return;
    }

cleanup_return:

    return ret;
}

// ------------------------------ Fonctions free ------------------------------ //

// CODE 18
int free_peer_ids_list(msg_peer_management_t *msg) {
    if (msg == NULL) {
        return 0;
    }

    if (msg->afk.peers.peer_ids != NULL) {
        free(msg->afk.peers.peer_ids);
        msg->afk.peers.peer_ids = NULL;
    }
    msg->afk.peers.peer_count = 0;
    return 0;
}

/* ========================================================================= */
/*                            Debug & Affichage                              */
/* ========================================================================= */

const char *message_codes_to_string(message_codes_t code) {
    switch (code) {
        case CODE_VALIDATION:
            return "CODE_VALIDATION";
        case CODE_CONSENSUS:
            return "CODE_CONSENSUS";
        case CODE_CONSENSUS_PART:
            return "CODE_CONSENSUS_PART";
        case CODE_JOIN_REQUEST:
            return "CODE_JOIN_REQUEST";
        case CODE_JOIN_RESPONSE:
            return "CODE_JOIN_RESPONSE";
        case CODE_PEER_INFO:
            return "CODE_PEER_INFO";
        case CODE_PEER_INFO_RESP_OK:
            return "CODE_PEER_INFO_RESP_OK";
        case CODE_PEER_INFO_RESP_ALT:
            return "CODE_PEER_INFO_RESP_ALT";
        case CODE_NEW_PEER_INFO:
            return "CODE_NEW_PEER_INFO";
        case CODE_SYSTEM_INFO:
            return "CODE_SYSTEM_INFO";
        case CODE_AUCTION_START:
            return "CODE_AUCTION_START";
        case CODE_AUCTION_BID:
            return "CODE_AUCTION_BID";
        case CODE_AUCTION_BID_CONFIRM:
            return "CODE_AUCTION_BID_CONFIRM";
        case CODE_AUCTION_ALMOST_END:
            return "CODE_AUCTION_ALMOST_END";
        case CODE_AUCTION_END:
            return "CODE_AUCTION_END";
        case CODE_SYSTEM_LEAVE:
            return "CODE_SYSTEM_LEAVE";
        case CODE_AUCTION_REJECT_CONCURRENT:
            return "CODE_REJECT_CONCURRENT";
        case CODE_AUCTION_REJECT_INVALID:
            return "CODE_REJECT_INVALID";
        case CODE_SUPERVISOR_AFK:
            return "CODE_SUPERVISOR_AFK";
        case CODE_CANCEL_TRANSACTION:
            return "CODE_CANCEL_TRANSACTION";
        case CODE_PEERS_REMOVE:
            return "CODE_PEERS_REMOVE";
        default:
            return "UNKNOWN_CODE";
    }
}

size_t msg_size_join_response() {
    message_t msg;
    return sizeof(msg.code) + sizeof(msg.content.join_resp.id) +
           sizeof(msg.content.join_resp.ip) + sizeof(msg.content.join_resp.port);
}

size_t msg_size_peer_info() {
    message_t msg;
    return sizeof(msg.code) + sizeof(msg.content.peer_info.info.id) +
           sizeof(msg.content.peer_info.info.ip) +
           sizeof(msg.content.peer_info.info.port) +
           sizeof(msg.content.peer_info.info.key);
}

size_t msg_size_peer_info_resp() {
    message_t msg;
    return sizeof(msg.code) + sizeof(msg.content.peer_info_resp.assigned_id);
}

size_t msg_size_new_peer_info() {
    return msg_size_peer_info();
}

size_t msg_prefix_size_system_info() {
    message_t msg;
    return sizeof(msg.code) + sizeof(msg.content.system_info.id) +
           sizeof(msg.content.system_info.auction_ip) +
           sizeof(msg.content.system_info.auction_port) +
           sizeof(msg.content.system_info.peer_count);
}

/**
 * Taille pour la struct info_t
 */
size_t bloc_info_size() {
    info_t info;
    return sizeof(info.id) + sizeof(info.ip) + sizeof(info.port) + sizeof(info.key);
}
