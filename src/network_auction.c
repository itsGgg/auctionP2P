#include "../include/network_auction.h"
#include "../include/debug.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_consensus.h"
#include "../include/safe_output.h"

// #include <stdatomic.h>
#include <unistd.h>

extern atomic_bool close_supervising_auction;

static int update_peer_auction_start(peer_t *peer, auction_t *auction) {
    if (!peer || !auction) {
        LOG_ERROR("Peer ou enchère invalide");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    int ret = auction_list_add(&peer->auction_list, auction);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (ret < 0) {
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    ret = auc_id_list_add(&peer->join_list, auction->numv);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (ret < 0) {
        peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
        auction_list_remove(&peer->auction_list, auction->numv);
        peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    peer->supervising_auction_id = auction->numv;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);

    return 0;
}

int send_auction_start(peer_t *peer, const char *a_price) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    int is_supervisor = peer->supervising_auction_id != 0;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    if (is_supervisor) {
        safe_printf("Action impossible. Vous êtes déjà superviseur d'une enchère.\n");
        return -1;
    }

    prix_t price;
    if (str_to_uint32(a_price, &price) < 0) {
        return -1;
    }

    auction_t *auction = create_auction(peer, price);
    if (!auction) {
        return -1;
    }

    message_t *msg =
        msg_create_auction_start(peer->id, auction->numv, auction->starting_price);
    if (!msg) {
        goto cleanup_error;
    }

    uint8_t buf[UDP_MSG_BUF_SIZE];
    size_t len;
    int ret = msg_serialize(msg, buf, sizeof(buf), &len);
    message_free(msg);
    if (ret != SERIALIZE_SUCCESS) {
        goto cleanup_error;
    }

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);
    int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    ssize_t sent = sendto(sock_send, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_send);
    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_AUCTION_START]",
                  CODE_AUCTION_START);
        goto cleanup_error;
    }

    LOG_INFO("CODE=%d [CODE_AUCTION_START] envoyé.\n", CODE_AUCTION_START);

    if (update_peer_auction_start(peer, auction) < 0) {
        LOG_ERROR(
            "Échec de la mise à jour des infos du pair après sa création de l'enchère");
        goto cleanup_error;
    }

    print_auction(auction);
    free(auction);
    return 0;

cleanup_error:
    if (auction) {
        free(auction);
    }
    return -1;
}

static int send_auction_bid_constraints_check(peer_t *peer, numv_t *auc_numv,
                                              prix_t *auc_price, bool is_bid_add) {

    if (!peer || !auc_numv || !auc_price) {
        LOG_ERROR("Peer ou enchère invalide");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_t *auction = auction_list_find(&peer->auction_list, *auc_numv);
    bool found = auction != NULL;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!found) {
        safe_printf("NUMV d'enchère non valide.\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    int is_joined = auc_id_list_contains(&peer->join_list, *auc_numv);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (!is_joined) {
        safe_printf("Action impossible. Vous ne participez pas à cette enchère.\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_state_t state = auction->state;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (state != AUCTION_OPEN) {
        safe_printf("Action impossible. L'enchère n'est pas ouverte.\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    int is_supervisor = *auc_numv == peer->supervising_auction_id;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    if (is_supervisor) {
        safe_printf(
            "Action impossible. Vous ne pouvez pas enchérir votre propre enchère.\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    int is_last_bidder = auction->bidder_id == peer->id;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (is_last_bidder) {
        safe_printf("Action impossible. Vous êtes le dernier enchérisseur.\n");
        return -1;
    }

    if (is_bid_add) {
        peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
        auction_t *auction = auction_list_find(&peer->auction_list, *auc_numv);
        if (auction) {
            *auc_price += auction->bid_price;
        }
        peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

        if (!auction) {
            safe_printf("NUMV d'enchère non valide.\n");
            return -1;
        }
    }

    return 0;
}

int send_auction_bid(peer_t *peer, const char *a_id, const char *a_price,
                     bool is_bid_add) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    numv_t a_numv;
    prix_t price;
    if (str_to_uint32(a_id, &a_numv) < 0 || str_to_uint32(a_price, &price) < 0) {
        return -1;
    }

    if (send_auction_bid_constraints_check(peer, &a_numv, &price, is_bid_add) < 0) {
        return -1;
    }

    message_t *msg = msg_create_auction_bid(peer->id, a_numv, price);
    if (!msg) {
        return -1;
    }

    uint8_t buf[UDP_MSG_BUF_SIZE];
    size_t len;
    if (msg_serialize(msg, buf, sizeof(buf), &len) != SERIALIZE_SUCCESS) {
        message_free(msg);
        return -1;
    }
    message_free(msg);

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);
    int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    ssize_t sent = sendto(sock_send, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_send);
    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_AUCTION_BID]",
                  CODE_AUCTION_BID);
        return -1;
    }

    LOG_INFO("CODE=%d [CODE_AUCTION_BID] envoyé.\n", CODE_AUCTION_BID);
    return 0;
}

static int send_auction_almost_end_constraints_check(peer_t *peer, numv_t *auc_numv,
                                                     prix_t *auc_price) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    *auc_numv = peer->supervising_auction_id;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);

    if (*auc_numv == 0) {
        safe_printf("Action impossible. Vous n'êtes pas superviseur d'une enchère.\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_t *auction = auction_list_find(&peer->auction_list, *auc_numv);
    bool found = auction != NULL;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!found) {
        safe_printf("NUMV d'enchère non valide.\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    *auc_price = auction->bid_price;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    return 0;
}

int send_auction_almost_end(peer_t *peer) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    numv_t a_numv = 0;
    prix_t price = 0;

    if (send_auction_almost_end_constraints_check(peer, &a_numv, &price) < 0) {
        return -1;
    }

    message_t *msg = msg_create_auction_almost_end(peer->id, a_numv, price);
    if (!msg) {
        return -1;
    }

    uint8_t buf[UDP_MSG_BUF_SIZE];
    size_t len;
    if (msg_serialize(msg, buf, sizeof(buf), &len) != SERIALIZE_SUCCESS) {
        message_free(msg);
        return -1;
    }
    message_free(msg);

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);
    int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    ssize_t sent = sendto(sock_send, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_send);
    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_AUCTION_ALMOST_END]",
                  CODE_AUCTION_ALMOST_END);
        return -1;
    }

    LOG_INFO("CODE=%d [CODE_AUCTION_ALMOST_END] envoyé.\n", CODE_AUCTION_ALMOST_END);

    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Votre enchère est presque terminée.\n");
    spf_priority(
        "Elle sera finalisée dans %d secondes,\n   si il n'y a aucun enchérissement "
        "valide entre temps.\n",
        BID_CONFIRM_END_TIMEOUT);
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
    return 0;
}

static int update_auction_end(peer_t *peer) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    auction_t *auction =
        auction_list_find(&peer->auction_list, peer->supervising_auction_id);
    bool found = auction != NULL;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!found) {
        LOG_ERROR("Échec de la recherche de l'enchère supervisée");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction->state = AUCTION_CLOSED;
    safe_printf("Finalisation de votre enchère :\n");
    print_auction_end(auction);
    peer->supervising_auction_id = 0;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    int removed = auc_id_list_remove(&peer->join_list, auction->numv) >= 0;
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (!removed) {
        LOG_ERROR(
            "Échec de la suppression de l'enchère de la liste des enchères rejointes");
        return -1;
    } else {
        LOG_INFO("Enchère retirée de la liste des enchères rejointes.\n");
    }

    return 0;
}

/**
 * @warning En cas de succès :
 * Met à jour l'état de l'enchère supervisée sur AUCTION_CLOSED
 * et remet le champ supervising_auction_id à 0
 */
int send_auction_end(peer_t *peer) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    message_t *msg = NULL;

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    if (peer->supervising_auction_id != 0) {
        auction_t *auction =
            auction_list_find(&peer->auction_list, peer->supervising_auction_id);

        if (auction) {
            msg = msg_create_auction_end(auction->bidder_id, auction->numv,
                                         auction->bid_price);
        } else {
            LOG_ERROR("Échec de la recherche de l'enchère supervisée");
        }
    } else {
        LOG_WARNING("Action impossible. Vous n'êtes pas superviseur d'une enchère.\n");
    }
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    if (!msg) {
        return -1;
    }

    uint8_t buf[UDP_MSG_BUF_SIZE];
    size_t len;

    int ret = msg_serialize(msg, buf, sizeof(buf), &len);
    message_free(msg);
    if (ret != SERIALIZE_SUCCESS) {
        return -1;
    }

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);
    int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    ssize_t sent = sendto(sock_send, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_send);
    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_AUCTION_END]",
                  CODE_AUCTION_END);
        return -1;
    }

    LOG_INFO("CODE=%d [CODE_AUCTION_END] envoyé.\n", CODE_AUCTION_END);

    if (update_auction_end(peer) < 0) {
        return -1;
    }

    if (CONSENSUS_ON) {
        if (ask_consensus(peer, buf, len) < 0) {
            LOG_ERROR(
                "Échec lors du consensus pour le message CODE=%d [CODE_AUCTION_END]",
                CODE_AUCTION_END);
            return -1;
        }
    }
    return 0;
}

static int check_auction_discovery(peer_t *peer, auction_t *auction) {
    if (!auction) {
        LOG_ERROR("Échec de l'allocation mémoire pour l'enchère");
        return -1;
    }

    int ret = 0;

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!auction_list_contains(&peer->auction_list, auction->numv)) {
        safe_printf("Nouvelle enchère découverte :\n");
        print_auction(auction);
        if (auction_list_add(&peer->auction_list, auction) < 0) {
            LOG_ERROR("Échec de l'ajout de l'enchère à la liste des enchères connues");
            ret = -1;
            goto unlock_return;
        }
        LOG_INFO("Ajout de l'enchère à la liste des enchères connues.\n");

        if (AUTO_JOIN_AUCTIONS) {
            LOG_INFO("AUTO-JOIN : Ajout de l'enchère à la liste des enchères rejointes.");

            peer_lock(peer, PEER_MUTEX_JOIN_LIST);
            ret = auc_id_list_add(&peer->join_list, auction->numv);
            peer_unlock(peer, PEER_MUTEX_JOIN_LIST);

            if (ret < 0) {
                ret = -1;
                goto unlock_return;
            }
        }
        ret = 1;
        goto unlock_return;
    }

unlock_return:
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    return ret;
}

void handle_auction_start(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_START] reçu.\n", CODE_AUCTION_START);

    auction_t *recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        free(recv_auction);
        return;
    }
    check_auction_discovery(peer, recv_auction); // Pas besoin de check return
    free(recv_auction);
}

static bool is_bid_valid(auction_t *auction, prix_t last_price) {
    if (!auction) {
        LOG_ERROR("Enchère invalide");
        return false;
    }

    if (last_price >= auction->bid_price) {
        safe_printf("Enchérissement invalide : le prix proposé est inférieur ou égal au "
                    "prix actuel. Refusé.\n");
        return false;
    }

    safe_printf("Enchérissement valide ! Accepté.\n");
    return true;
}

static int send_reject_invalid(peer_t *peer, auction_t rejected) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    message_t *msg = msg_create_auction_reject_invalid(rejected.bidder_id, rejected.numv,
                                                       rejected.bid_price);
    if (!msg) {
        return -1;
    }

    uint8_t buf[UDP_MSG_BUF_SIZE];
    size_t len;

    int ret = msg_serialize(msg, buf, sizeof(buf), &len);
    message_free(msg);
    if (ret != SERIALIZE_SUCCESS) {
        return -1;
    }

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);
    int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    ssize_t sent = sendto(sock_send, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_send);
    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_AUCTION_REJECT_CONCURRENT]",
                  CODE_AUCTION_REJECT_CONCURRENT);
        return -1;
    }

    LOG_INFO("CODE=%d [CODE_AUCTION_REJECT_CONCURRENT] envoyé.\n",
             CODE_AUCTION_REJECT_CONCURRENT);
    return 0;
}

static int send_auction_bid_confirm(peer_t *peer, auction_t *auction) {
    if (!peer) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    message_t *msg = msg_create_auction_bid_confirm(auction->bidder_id, auction->numv,
                                                    auction->bid_price);
    if (!msg) {
        return -1;
    }

    uint8_t buf[UDP_MSG_BUF_SIZE];
    size_t len;

    int ret = msg_serialize(msg, buf, sizeof(buf), &len);
    message_free(msg);
    if (ret != SERIALIZE_SUCCESS) {
        return -1;
    }

    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);
    int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    ssize_t sent = sendto(sock_send, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_send);

    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_AUCTION_BID_CONFIRM]",
                  CODE_AUCTION_BID_CONFIRM);
        return -1;
    }

    LOG_INFO("CODE=%d [CODE_AUCTION_BID_CONFIRM] envoyé.\n", CODE_AUCTION_BID_CONFIRM);
    if (CONSENSUS_ON) {
        if (ask_consensus(peer, buf, len) < 0) {
            LOG_ERROR("Échec lors du consensus pour le message CODE=%d "
                      "[CODE_AUCTION_BID_CONFIRM]",
                      CODE_AUCTION_BID_CONFIRM);
            return -1;
        }
    }
    return 0;
}

void handle_auction_bid(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_BID] reçu.\n", CODE_AUCTION_BID);

    auction_t *recv_auction = NULL;

    recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        goto cleanup_return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        goto cleanup_return;
    }

    check_auction_discovery(peer, recv_auction); // Pas besoin de check return

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_t *old_stats = auction_list_find(&peer->auction_list, recv_auction->numv);

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    if (auc_id_list_contains(&peer->join_list, recv_auction->numv)) {
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
        spf_priority("Proposition reçue pour %senchère NUMV=%u :\n",
                     peer->supervising_auction_id != recv_auction->numv ? "l'" : "votre ",
                     recv_auction->numv);
        spf_priority("---------------------------------------------------------------\n");
        spf_priority_unlock();
        safe_printf("Voici les détails de l'enchérissement proposé :\n");
        print_compare_auction(old_stats, recv_auction);
    }
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    int is_supervisor = peer->supervising_auction_id == recv_auction->numv;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);

    if (!is_supervisor) {
        LOG_INFO("Vous n'êtes pas le superviseur de cette enchère, pas de traitement.\n");
        goto cleanup_return;
    }

    if (!is_bid_valid(recv_auction, old_stats->bid_price)) {
        if (send_reject_invalid(peer, *recv_auction) < 0) {
            // Affichage spéciale si besoin
        }
        goto cleanup_return;
    }

    if (send_auction_bid_confirm(peer, recv_auction) < 0) {
        goto cleanup_return;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    int updated =
        update_auction(auction_list_find(&peer->auction_list, recv_auction->numv),
                       recv_auction) < 0;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    if (updated < 0) {
        goto cleanup_return;
    }

cleanup_return:
    if (recv_auction) {
        free(recv_auction);
    }
}

void handle_auction_bid_confirm(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_BID_CONFIRM] reçu.\n", CODE_AUCTION_BID_CONFIRM);

    auction_t *recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        free(recv_auction);
        return;
    }

    check_auction_discovery(peer, recv_auction); // Pas besoin de check return

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_t *old_stats = auction_list_find(&peer->auction_list, recv_auction->numv);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    if (auc_id_list_contains(&peer->join_list, recv_auction->numv)) {
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
        if (peer->id == recv_auction->numv) {
            spf_priority("Votre enchérissement a été acceptée pour l'enchère NUMV=%u :\n",
                         recv_auction->numv);
        } else {
            spf_priority("Enchérissement acceptée pour l'enchère NUMV=%u :\n",
                         recv_auction->numv);
        }
        spf_priority("---------------------------------------------------------------\n");
        spf_priority_unlock();
        safe_printf("Voici les détails de l'enchérissement :\n");
        print_compare_auction(old_stats, recv_auction);
    }
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    if (update_auction(old_stats, recv_auction) < 0) {
        LOG_ERROR("Échec de la mise à jour de l'enchère supervisée");
    }
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    u_int8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;
    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de la sérialisation du message CODE_AUCTION_BID_CONFIRM");
        return;
    }

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    app_id_t creator_id =
        auction_list_find(&peer->auction_list, recv_auction->numv)->creator_id;
    info_t *validation_dest = peer_list_find(&peer->peer_list, creator_id);
    bool found = validation_dest != NULL;
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    info_t dest;
    if (found) {
        dest = *validation_dest;
    }

    if (CONSENSUS_ON) {
        if (found) {
            if (respond_consensus(peer, msg_buf, msg_len, dest) < 0) {
                LOG_ERROR("Échec de la réponse au consensus pour le message "
                          "CODE_AUCTION_BID_CONFIRM");
            } else {
                LOG_INFO("Réponse au consensus pour le message CODE_AUCTION_BID_CONFIRM "
                         "réussie");
            }
        } else {
            LOG_WARNING("Aucun pair trouvé pour la validation du message "
                        "CODE_AUCTION_BID_CONFIRM");
        }
    }

    free(recv_auction);
}

void handle_auction_almost_end(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_ALMOST_END] reçu.\n", CODE_AUCTION_ALMOST_END);

    auction_t *recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        free(recv_auction);
        return;
    }

    check_auction_discovery(peer, recv_auction); // Pas besoin de check return

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    bool is_joined = auc_id_list_contains(&peer->join_list, recv_auction->numv);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (is_joined) {
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
        spf_priority("Alerte : l'enchère NUMV=%u est presque terminée.\n",
                     recv_auction->numv);
        spf_priority(
            "Elle sera finalisée dans %d secondes,\n   si il n'y a aucun enchérissement "
            "valide entre temps.\n",
            BID_CONFIRM_END_TIMEOUT);
        spf_priority("---------------------------------------------------------------\n");
        spf_priority_unlock();
    }

    free(recv_auction);
}

void handle_auction_end(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_END] reçu.\n", CODE_AUCTION_END);

    auction_t *recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        free(recv_auction);
        return;
    }

    check_auction_discovery(peer, recv_auction); // Pas besoin de check return

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_t *old_stats = auction_list_find(&peer->auction_list, recv_auction->numv);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);

    update_auction(old_stats, recv_auction);

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    bool is_joined = auc_id_list_contains(&peer->join_list, recv_auction->numv);
    bool removed = auc_id_list_remove(&peer->join_list, recv_auction->numv) >= 0;
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    if (is_joined) {
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
        spf_priority("Alerte : l'enchère NUMV=%u est terminée.\n", recv_auction->numv);
        spf_priority("---------------------------------------------------------------\n");
        spf_priority_unlock();
        safe_printf("Voici le résultat de l'enchère :\n");
        print_auction_end(old_stats);
    }
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!removed) {
        LOG_ERROR(
            "Échec de la suppression de l'enchère de la liste des enchères rejointes");
    } else {
        LOG_INFO("Suppression de l'enchère de la liste des enchères rejointes.\n");
    }

    u_int8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;
    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de la sérialisation du message CODE_AUCTION_END");
        return;
    }

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    app_id_t creator_id =
        auction_list_find(&peer->auction_list, recv_auction->numv)->creator_id;
    info_t *validation_dest = peer_list_find(&peer->peer_list, creator_id);
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
                    "Échec de la réponse au consensus pour le message CODE_AUCTION_END");
            } else {
                LOG_INFO("Réponse au consensus pour le message CODE_AUCTION_END réussie");
            }
        } else {
            LOG_WARNING(
                "Aucun pair trouvé pour la validation du message CODE_AUCTION_END");
        }
    }

    free(recv_auction);
}

void handle_auction_reject_concurrent(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_REJECT_CONCURRENT] reçu.\n",
             CODE_AUCTION_REJECT_CONCURRENT);

    auction_t *recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        free(recv_auction);
        return;
    }

    check_auction_discovery(peer, recv_auction); // Pas besoin de check return

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    bool is_joined = auc_id_list_contains(&peer->join_list, recv_auction->numv);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (is_joined) {
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
        spf_priority(
            "Concurrence : l'enchérissement de ID=%d sur NUMV=%u a été refusé.\n",
            recv_auction->bidder_id, recv_auction->numv);
        spf_priority("---------------------------------------------------------------\n");
        spf_priority_unlock();
    }
    free(recv_auction);
}

void handle_auction_reject_invalid(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_AUCTION_REJECT_INVALID] reçu.\n",
             CODE_AUCTION_REJECT_INVALID);

    auction_t *recv_auction = malloc(sizeof(auction_t));
    if (!recv_auction) {
        LOG_ERROR("Échec malloc pour l'enchère");
        return;
    }

    if (msg_auction_to_auction(&msg->content.auction, recv_auction, msg->code) < 0) {
        LOG_ERROR("Échec de la conversion du message d'enchère");
        free(recv_auction);
        return;
    }

    check_auction_discovery(peer, recv_auction); // Pas besoin de check return

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    bool is_joined = auc_id_list_contains(&peer->join_list, recv_auction->numv);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (is_joined) {
        spf_priority_lock();
        spf_priority("---------------------------------------------------------------\n");
        spf_priority("Non valide : l'enchérissement de ID=%d sur NUMV=%u a été refusé.\n",
                     recv_auction->bidder_id, recv_auction->numv);
        spf_priority("Raison : prix inférieur au prix courant de la vente\n");
        spf_priority("---------------------------------------------------------------\n");
        spf_priority_unlock();
    }
    free(recv_auction);
}

int start_monitoring_auction(peer_t *peer) {
    if (peer == NULL) {
        LOG_ERROR("Peer invalide");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);
    numv_t monitoring_auc = peer->supervising_auction_id;
    peer_unlock(peer, PEER_MUTEX_SUPERVISING_AUCTION_ID);

    if (monitoring_auc == 0) {
        LOG_ERROR("Aucune enchère à surveiller");
        return -1;
    }

    bool end_auc = false;
    bool is_almost_end = false;
    int sleep_time = BID_END_TIMEOUT;
    while (!end_auc) {
        sleep(sleep_time);
        peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
        auction_t *auction = auction_list_find(&peer->auction_list, monitoring_auc);
        bool found = auction != NULL;
        peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
        if (!found) {
            LOG_ERROR("Enchère introuvable dans la liste des enchères");
            return -1;
        }

        if (!is_almost_end) {
            peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
            time_t current_time = time(NULL);
            is_almost_end = current_time - auction->last_bid_time >= BID_END_TIMEOUT;
            peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
            if (is_almost_end) {
                LOG_INFO("Enchère presque terminée, envoi de l'alerte.\n");
                if (send_auction_almost_end(peer) < 0) {
                    return -1;
                }
                sleep_time = BID_CONFIRM_END_TIMEOUT;

            } else {
                peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
                sleep_time = BID_END_TIMEOUT - (current_time - auction->last_bid_time);
                peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
            }

        } else {
            peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
            time_t current_time = time(NULL);
            end_auc = current_time - auction->last_bid_time >= BID_END_TIMEOUT;

            if (!end_auc) {
                is_almost_end = false;

                sleep_time = BID_END_TIMEOUT - (current_time - auction->last_bid_time);
            }
            peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
        }
    }

    atomic_store(&close_supervising_auction, true);

    return 0;
}
