#include "../include/network_handler.h"
#include "../include/console.h"
#include "../include/debug.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_auction.h"
#include "../include/network_consensus.h"
#include "../include/network_join.h"
#include "../include/network_leave.h"
#include "../include/network_missing_peer.h"
#include "../include/safe_output.h"
#include "../include/utils.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <readline/readline.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

atomic_bool in_use_liaison = false;
atomic_bool in_use_perso_tcp = false;
atomic_bool in_use_udp_perso = false;
atomic_bool in_use_udp_encheres = false;
atomic_bool close_supervising_auction = false;

static void handle_update_location(peer_t *peer) {
    peer_lock(peer, PEER_MUTEX_LOCATION);
    user_location_t peer_location = peer->location;
    peer_unlock(peer, PEER_MUTEX_LOCATION);

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    size_t join_list_count = peer->join_list.count;
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);

    if (join_list_count > 0 && peer_location != LOCATION_AUCTION) {
        update_location(peer, LOCATION_AUCTION);
    } else if (join_list_count == 0 && peer_location != LOCATION_LOBBY) {
        update_location(peer, LOCATION_LOBBY);
    }
}

static void handle_supervising_auction(peer_t *peer) {
    if (atomic_load(&close_supervising_auction)) {
        if (send_auction_end(peer) < 0) {
            LOG_ERROR("Erreur lors de la fermeture de l'enchère");
        }
        atomic_store(&close_supervising_auction, false);
    }
}

/**
 * Traite ce qui n'est pas lié aux sockets
 */
static void handle_peer(peer_t *peer) {
    handle_update_location(peer);
    handle_supervising_auction(peer);
}

static void *handle_liaison(void *arg) {
    LOG_INFO("Message reçu sur l'adresse liaison.");
    peer_t *peer = (peer_t *)arg;

    uint8_t buffer[UDP_MSG_BUF_SIZE];
    struct sockaddr_in6 src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    socklen_t addr_len = sizeof(src_addr);

    message_t *msg = NULL;

    peer_lock(peer, PEER_MUTEX_UDP_LIAISON_RECV);
    ssize_t len = recvfrom(peer->sockets.sock_udp_liaison_recv, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&src_addr, &addr_len);
    peer_unlock(peer, PEER_MUTEX_UDP_LIAISON_RECV);
    atomic_store(&in_use_liaison, false);

    if (len < 0) {
        LOG_ERROR("recvfrom liaison");
        goto cleanup_return;
    }

    // Compare l’IP source avec peer->ip
    if (memcmp(&src_addr.sin6_addr, peer->ip.addr, IPV6_ADDR_LEN) == 0 &&
        ntohs(src_addr.sin6_port) == peer->port) {
        LOG_INFO("Ignoré — message de moi-même");
        goto cleanup_return;
    }

    msg = malloc(sizeof(message_t));
    if (!msg) {
        LOG_ERROR("Erreur malloc message");
        goto cleanup_return;
    }

    if (msg_deserialize(buffer, len, msg, false, 0) != DESERIALIZE_SUCCESS) {
        LOG_WARNING("Message invalide reçu en liaison.");
        goto cleanup_return;
    }

    handle_liaison_codes(peer, msg, src_addr);

cleanup_return:
    if (msg) {
        message_free(msg);
    }
    LOG_INFO("Fermeture du thread de la gestion du message liaison.\n");
    return NULL;
}

static int perso_tcp_recv_all_deser(int client_sock, SSL *tls_socket, size_t buf_size,
                                    message_t *out_msg, uint8_t deser_skip_code_val) {

    uint8_t deser_buf[buf_size];
    ssize_t deser_len;

    if (TLS_ON) {
        deser_len =
            tls_recv_all(tls_socket, deser_buf, sizeof(deser_buf) - sizeof(code_t), NULL);
    } else {
        deser_len = tcp_recv_all(client_sock, deser_buf,
                                 sizeof(deser_buf) - sizeof(code_t), NULL);
    }

    if (deser_len <= 0) {
        LOG_WARNING("perso_tcp_recv_all_deser : recv_all");
        return -1;
    }

    if (msg_deserialize(deser_buf, deser_len, out_msg, true, deser_skip_code_val) !=
        DESERIALIZE_SUCCESS) {
        LOG_WARNING("perso_tcp_recv_all_deser : message invalide");
        return -1;
    }
    return 0;
}

static void *handle_perso_tcp(void *arg) {
    LOG_INFO("Message reçu sur l'adresse perso TCP.");

    peer_t *peer = (peer_t *)arg;

    struct sockaddr_in6 src_addr;
    socklen_t addr_len = sizeof(src_addr);
    memset(&src_addr, 0, sizeof(src_addr));

    message_t *msg = NULL;
    SSL *tls_socket = NULL;

    peer_lock(peer, PEER_MUTEX_TCP_PERSO_LISTEN);
    int client_sock = accept(peer->sockets.sock_tcp_perso_listen,
                             (struct sockaddr *)&src_addr, &addr_len);
    peer_unlock(peer, PEER_MUTEX_TCP_PERSO_LISTEN);
    atomic_store(&in_use_perso_tcp, false);

    if (client_sock < 0) {
        LOG_ERROR("handle_perso_tcp : accept");
        goto cleanup_return;
    }

    LOG_INFO("Connexion TCP acceptée depuis %s:%d",
             in6_addr_to_str_static(&src_addr.sin6_addr), ntohs(src_addr.sin6_port));

    uint8_t buffer[sizeof(code_t)];
    ssize_t len;

    if (TLS_ON) {
        LOG_INFO("Tentative de sécurisation avec TLS");
        if (init_tls_context(peer, 1) < 0) {
            LOG_ERROR("Erreur lors de l'initialisation du contexte SSL pour le TLS");
            goto cleanup_return;
        }
        int ret = upgrade_to_tls_server(peer, client_sock, &tls_socket);
        if (ret != 0) {
            if (ret == -2) {
                LOG_WARNING("Un autre pair a déjà établi une connexion TLS avec ce pair");
                goto cleanup_return;
            } else {
                LOG_ERROR("Sécurisation TLS: échouée");
                goto cleanup_return;
            }
        } else {
            LOG_INFO("Sécurisation TLS : réussie");
            len = tls_recv_all(tls_socket, buffer, sizeof(buffer), NULL);
        }

    } else {
        len = tcp_recv_all(client_sock, buffer, sizeof(buffer), NULL);
    }

    if (len <= 0) {
        LOG_WARNING("handle_perso_tcp : recv_all");
        goto cleanup_return;
    }

    msg = malloc(sizeof(message_t));
    if (!msg) {
        LOG_ERROR("handle_perso_tcp : malloc message");
        goto cleanup_return;
    }

    handle_perso_tcp_codes(peer, msg, client_sock, tls_socket, buffer);

cleanup_return:
    if (tls_socket) {
        tls_close(tls_socket);
    } else if (client_sock >= 0) {
        close(client_sock);
    }
    if (msg) {
        message_free(msg);
    }
    LOG_INFO("Fermeture du thread de la gestion du message perso TCP.\n");
    return NULL;
}

static void *handle_perso_udp(void *arg) {
    LOG_INFO("Message reçu sur l'adresse perso UDP.");

    peer_t *peer = (peer_t *)arg;

    uint8_t buff[UDP_MSG_BUF_SIZE];
    struct sockaddr_in6 src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    socklen_t addr_len = sizeof(src_addr);
    message_t *msg = NULL;

    peer_lock(peer, PEER_MUTEX_UDP_PERSO_RECV);
    ssize_t len = recvfrom(peer->sockets.sock_udp_perso_recv, buff, sizeof(buff), 0,
                           (struct sockaddr *)&src_addr, &addr_len);
    peer_unlock(peer, PEER_MUTEX_UDP_PERSO_RECV);
    atomic_store(&in_use_udp_perso, false);

    if (len < 0) {
        LOG_ERROR("recvfrom perso UDP");
        goto cleanup_return;
    }

    // Compare l’IP source avec peer->ip
    if (memcmp(&src_addr.sin6_addr, peer->ip.addr, IPV6_ADDR_LEN) == 0 &&
        ntohs(src_addr.sin6_port) == peer->port) {
        LOG_INFO("Ignoré — message de moi-même");
        goto cleanup_return;
    }

    msg = malloc(sizeof(message_t));
    if (!msg) {
        LOG_ERROR("Erreur malloc message");
        goto cleanup_return;
    }

    if (msg_deserialize(buff, len, msg, false, 0) != DESERIALIZE_SUCCESS) {
        LOG_WARNING("Message invalide reçu en perso UDP");
        goto cleanup_return;
    }

    handle_perso_udp_codes(peer, msg);

cleanup_return:
    if (msg) {
        message_free(msg);
    }
    LOG_INFO("Fermeture du thread de la gestion du message perso UDP.\n");
    return NULL;
}

static void *handle_encheres(void *arg) {
    LOG_INFO("Message reçu sur l'adresse enchères.");

    peer_t *peer = (peer_t *)arg;

    uint8_t buffer[UDP_MSG_BUF_SIZE];
    struct sockaddr_in6 src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    socklen_t addr_len = sizeof(src_addr);

    message_t *msg = NULL;

    peer_lock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
    ssize_t len = recvfrom(peer->sockets.sock_udp_encheres_recv, buffer, sizeof(buffer),
                           0, (struct sockaddr *)&src_addr, &addr_len);
    peer_unlock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
    atomic_store(&in_use_udp_encheres, false);

    if (len < 0) {
        LOG_ERROR("recvfrom enchères");
        goto cleanup_return;
    }

    // Compare l’IP source avec peer->ip
    if (memcmp(&src_addr.sin6_addr, peer->ip.addr, IPV6_ADDR_LEN) == 0 &&
        ntohs(src_addr.sin6_port) == peer->port) {
        LOG_INFO("Ignoré — message de moi-même");
        goto cleanup_return;
    }

    msg = malloc(sizeof(message_t));
    if (!msg) {
        LOG_ERROR("Erreur malloc message");
        goto cleanup_return;
    }

    if (msg_deserialize(buffer, len, msg, false, 0) != DESERIALIZE_SUCCESS) {
        LOG_WARNING("Message invalide reçu sur l'adresse enchères.");
        goto cleanup_return;
    }

    handle_encheres_codes(peer, msg);

cleanup_return:
    if (msg) {
        message_free(msg);
    }
    LOG_INFO("Fermeture du thread de la gestion du message enchères.\n");
    return NULL;
}

void network_event_loop(peer_t *peer) {

    print_network_sockets(&peer->sockets);

    int fds[] = {STDIN_FILENO, peer->sockets.sock_udp_liaison_recv,
                 peer->sockets.sock_tcp_perso_listen, peer->sockets.sock_udp_perso_recv,
                 peer->sockets.sock_udp_encheres_recv};
    const size_t nfds = sizeof(fds) / sizeof(*fds);

    int maxfd = 0;
    for (size_t i = 0; i < nfds; ++i) {
        int fd = fds[i];
        if (fd < 0)
            continue;
        if (fd > maxfd)
            maxfd = fd;
    }

    while (!atomic_load(&should_exit)) {

        handle_peer(peer);

        fd_set readfds;
        FD_ZERO(&readfds);

        // Ajoute tous les FDs au set
        for (size_t i = 0; i < nfds; ++i) {
            int fd = fds[i];
            if (fd < 0) {
                LOG_WARNING("Socket non initialisé, fd: %d", fd);
                continue; // ignore les sockets non initialisés
            }
            FD_SET(fd, &readfds);
        }

        struct timeval timeout = {
            .tv_sec = 0,       // Refresh
            .tv_usec = 500000, // 500ms
        };

        int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            LOG_ERROR("select");
            break;
        }

        pthread_t thread;

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            rl_callback_read_char();
        }

        else if (!atomic_load(&in_use_liaison) && FD_ISSET(fds[1], &readfds)) {
            if (pthread_create(&thread, NULL, handle_liaison, peer) != 0) {
                LOG_ERROR("pthread_create : socket liaison");
            } else {
                atomic_store(&in_use_liaison, true);
                LOG_INFO("");
                LOG_INFO("Thread créé pour gérer : socket liaison");
                pthread_detach(thread);
            }
        }

        else if (!atomic_load(&in_use_perso_tcp) && FD_ISSET(fds[2], &readfds)) {
            if (pthread_create(&thread, NULL, handle_perso_tcp, peer) != 0) {
                LOG_ERROR("pthread_create : socket TCP perso");
            } else {
                atomic_store(&in_use_perso_tcp, true);
                LOG_INFO("");
                LOG_INFO("Thread créé pour gérer : socket TCP perso");
                pthread_detach(thread);
            }
        }

        else if (!atomic_load(&in_use_udp_perso) && FD_ISSET(fds[3], &readfds)) {
            if (pthread_create(&thread, NULL, handle_perso_udp, peer) != 0) {
                LOG_ERROR("pthread_create : socket UDP perso");
            } else {
                atomic_store(&in_use_udp_perso, true);
                LOG_INFO("");
                LOG_INFO("Thread créé pour gérer : socket UDP perso");
                pthread_detach(thread);
            }
        }

        else if (!atomic_load(&in_use_udp_encheres) && FD_ISSET(fds[4], &readfds)) {
            if (pthread_create(&thread, NULL, handle_encheres, peer) != 0) {
                LOG_ERROR("pthread_create : socket UDP enchères");
            } else {
                atomic_store(&in_use_udp_encheres, true);
                LOG_INFO("");
                LOG_INFO("Thread créé pour gérer : socket UDP enchères");
                pthread_detach(thread);
            }
        }
    }
}

void handle_liaison_codes(peer_t *peer, message_t *msg, struct sockaddr_in6 src_addr) {
    switch (msg->code) {

        case CODE_JOIN_REQUEST:
            handle_join_request(peer, msg, src_addr);
            break;

        default:
            LOG_WARNING("Code de message inconnu : %d", msg->code);
            break;
    }
}

void handle_perso_tcp_codes(peer_t *peer, message_t *msg, int client_sock,
                            SSL *tls_socket, uint8_t *buffer) {
    switch (buffer[0]) {

        case CODE_PEER_INFO: {
            if (perso_tcp_recv_all_deser(client_sock, tls_socket, msg_size_peer_info(),
                                         msg, CODE_PEER_INFO) < 0) {
                LOG_ERROR("perso_tcp_recv_all_deser : CODE_PEER_INFO");
                return;
            }
            handle_peer_info(peer, msg, client_sock, tls_socket);
            break;
        }

        case CODE_NEW_PEER_INFO: {
            if (perso_tcp_recv_all_deser(client_sock, tls_socket,
                                         msg_size_new_peer_info(), msg,
                                         CODE_NEW_PEER_INFO) < 0) {
                LOG_ERROR("perso_tcp_recv_all_deser : CODE_NEW_PEER_INFO");
                return;
            }
            handle_new_peer_info(peer, msg);
            break;
        }

        default:
            LOG_WARNING("Code de message inconnu : %d", msg->code);
            break;
    }
}

void handle_perso_udp_codes(peer_t *peer, message_t *msg) {
    switch (msg->code) {

        case CODE_CONSENSUS:
            LOG_WARNING("J'ai reçu un CODE_CONSENSUS");
            break;

        case CODE_CONSENSUS_PART:
            LOG_WARNING("J'ai reçu un CODE_CONSENSUS_PART");
            break;

        case CODE_AUCTION_BID_CONFIRM:
            handle_auction_bid_confirm(peer, msg);
            break;

        case CODE_AUCTION_END:
            handle_auction_end(peer, msg);
            break;

        case CODE_SYSTEM_LEAVE: // 13
            handle_system_leave(peer, msg);
            break;

        case CODE_SUPERVISOR_AFK: // 16
            LOG_WARNING("J'ai reçu un CODE_SUPERVISOR_AFK");
            break;

        case CODE_PEERS_REMOVE: // 18
            handle_peers_remove(peer, msg);
            break;

        default:
            LOG_WARNING("Code de message inconnu : %d", msg->code);
            break;
    }
}

void handle_encheres_codes(peer_t *peer, message_t *msg) {
    switch (msg->code) {

        case CODE_CONSENSUS:
            LOG_WARNING("J'ai reçu un CODE_CONSENSUS");
            break;

        case CODE_CONSENSUS_PART:
            LOG_WARNING("J'ai reçu un CODE_CONSENSUS_PART");
            break;

        case CODE_AUCTION_START:
            handle_auction_start(peer, msg);
            break;

        case CODE_AUCTION_BID:
            handle_auction_bid(peer, msg);
            break;

        case CODE_AUCTION_BID_CONFIRM:
            handle_auction_bid_confirm(peer, msg);
            break;

        case CODE_AUCTION_ALMOST_END:
            handle_auction_almost_end(peer, msg);
            break;

        case CODE_AUCTION_END:
            handle_auction_end(peer, msg);
            break;

        case CODE_SYSTEM_LEAVE: // 13
            handle_system_leave(peer, msg);
            break;

        case CODE_AUCTION_REJECT_CONCURRENT:
            handle_auction_reject_concurrent(peer, msg);
            break;

        case CODE_AUCTION_REJECT_INVALID:
            handle_auction_reject_invalid(peer, msg);
            break;

        case CODE_SUPERVISOR_AFK: // 16
            LOG_WARNING("J'ai reçu un CODE_SUPERVISOR_AFK");
            break;

        case CODE_PEERS_REMOVE: // 18
            handle_peers_remove(peer, msg);
            break;

        default:
            LOG_WARNING("Code de message inconnu : %d", msg->code);
            break;
    }
}