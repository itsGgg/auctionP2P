#include "../include/network_join.h"
#include "../include/debug.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_sockets.h"
#include "../include/peer.h"
#include "../include/safe_output.h"
#include "../include/utils.h"
#include "../lib/format.h"

#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

int network_init(peer_t *peer) {
    if (!peer) {
        LOG_WARNING("peer invalide");
        return -1;
    }

    LOG_INFO("Création des infos utilisateur...\n");

    ip_t iploc;
    char *interface_name = NULL;
    LOG_INFO("Détection de l'adresse ip...");
    if (get_local_ip(&iploc, &interface_name) != 0) {
        LOG_ERROR("Aucune adresse IPv6 trouvée (hors loopback)");
        return -1;
    }
    LOG_INFO("Détectée -- IPv6 locale : %s\n", ip_to_str_static(&iploc));

    if (interface_name) {
        LOG_INFO("Interface : %s\n", interface_name);
    } else {
        LOG_INFO("Interface : Non spécifiée (adresse globale)\n");
    }

    LOG_INFO("Choix du port...");
    port_t find_port =
        get_available_port(49152, 65535); // Utilisation des ports dynamiques/éphémères
    if (find_port == 0) {
        LOG_ERROR("Aucun port libre trouvé");
        return -1;
    }
    LOG_INFO("Port %d LIBRE sur cette machine.\n", find_port);

    // NE RIEN STOCKER DANS PEER AVANT (D'APPELER peer_init / CETTE LIGNE)
    if (peer_init(peer, &iploc, find_port, interface_name) < 0) {
        LOG_ERROR("Erreur lors de l'initialisation du pair");
        return -1;
    }

    safe_printf("Voici vos informations : \n");
    print_peer(peer);
    free(interface_name);

    return 0;
}

/**
 * Reçoit la réponse de jointure
 * @return 0 si la réponse est valide, -1 si select error,
 * 1 en cas d'erreur lors de l'échange avec le pair
 */
static int recv_join_response(peer_t *peer, struct timeval *timeout,
                              message_t **out_msg) {

    int sock_listen = peer->sockets.sock_udp_perso_recv;

    // Préparer le select sur le socket UDP
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(sock_listen, &rset);
    struct timeval tv = *timeout;
    int ready = select(sock_listen + 1, &rset, NULL, NULL, &tv);
    if (ready < 0) {
        LOG_ERROR("select");
        return -1;
    } else if (ready == 0) {
        return -1; // timeout expiré
    }
    LOG_INFO("DÉTECTÉ !");
    struct sockaddr_in6 src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    socklen_t addr_len = sizeof(src_addr);
    uint8_t recv_buf[UDP_MSG_BUF_SIZE];

    ssize_t recv_len = recvfrom(sock_listen, recv_buf, sizeof(recv_buf), 0,
                                (struct sockaddr *)&src_addr, &addr_len);
    if (recv_len < 0) {
        perror("recvfrom");
        return 1;
    }

    message_t *resp = malloc(sizeof(message_t));
    if (!resp) {
        LOG_ERROR("Erreur malloc message");
        return 1;
    }

    if (msg_deserialize(recv_buf, recv_len, resp, false, 0) != DESERIALIZE_SUCCESS) {
        message_free(resp);
        return 1;
    }

    if (resp->code != CODE_JOIN_RESPONSE) {
        LOG_WARNING("Message inattendu : CODE=%d", resp->code);
        message_free(resp);
        return 1;
    }

    *out_msg = resp;
    return 0;
}

/**
 * Attends les réponses UDP unicast après l'envoi du CODE_JOIN_REQUEST en UDP multicast
 * @return Le nombre de pair enregistré
 */
static int wait_for_join_response(peer_t *peer) {

    struct timeval start_time, now, elapsed;
    gettimeofday(&start_time, NULL);

    int count = 0;

    while (1) {
        // Calculer le temps restant
        gettimeofday(&now, NULL);
        timersub(&now, &start_time, &elapsed);

        if (elapsed.tv_sec >= JOIN_TIMEOUT_SEC) {
            break;
        }

        struct timeval timeout;
        timeout.tv_sec = JOIN_TIMEOUT_SEC - elapsed.tv_sec;
        timeout.tv_usec = 0;

        message_t *resp = NULL;
        int response = recv_join_response(peer, &timeout, &resp);
        if (response < 0) {
            break;
        } else if (response == 1) {
            message_free(resp);
            continue;
        }
        LOG_INFO("CODE=%d [CODE_JOIN_RESPONSE] reçu.", CODE_JOIN_RESPONSE);

        LOG_INFO("Ajout du pair distant à la liste des pairs connus...");
        info_t new_peer;
        new_peer.id = resp->content.join_resp.id;
        new_peer.port = resp->content.join_resp.port;
        memcpy(new_peer.ip.addr, resp->content.join_resp.ip.addr, IPV6_ADDR_LEN);
        // On ne connait pas encore la clé publique du pair distant

        peer_list_add(&peer->peer_list, &new_peer);
        LOG_INFO("");
        message_free(resp);
        count++;
    }

    return count;
}

int network_discover_peers(peer_t *peer) {
    safe_printf("Découverte des pairs existants...\n");

    // Adresse de liaison
    ip_t ip;
    inet_pton(AF_INET6, LIAISON_MULTICAST_ADDR, ip.addr);
    struct sockaddr_in6 addr =
        make_addr(&ip, LIAISON_MULTICAST_PORT, peer->interface_name);

    // Préparation du message
    message_t *msg = msg_create_join_request();
    if (!msg) {
        return -1;
    }

    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    int res = msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len);
    message_free(msg);
    if (res != SERIALIZE_SUCCESS) {
        return -1;
    }

    for (int attempt = 1; attempt <= JOIN_ATTEMPT_MAX; attempt++) {
        safe_printf("\nTentative [%d/%d] — Demande de connexion en cours...\n", attempt,
                    JOIN_ATTEMPT_MAX);

        int sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);
        if (sock_send < 0) {
            return -1;
        }

        int sent = sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr,
                          sizeof(addr));
        close(sock_send);
        if (sent < 0) {
            LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_JOIN_REQUEST]",
                      CODE_JOIN_REQUEST);
            return -1;
        }

        LOG_INFO("CODE=%d [CODE_JOIN_REQUEST] envoyé, en attente de réponse...\n",
                 CODE_JOIN_REQUEST);
        int found = wait_for_join_response(peer);
        if (found != 0) {
            return found < 0 ? -1 : 1;
        }
        LOG_INFO("Aucun pair n’a répondu dans le délai.");
        safe_printf("Demande de connexion échouée.\n");
    }

    safe_printf("\nAucun pair actif détecté après %d tentatives.\n\n", JOIN_ATTEMPT_MAX);
    return 0; // Aucun pair — c’est le premier
}

int network_create_system(peer_t *peer) {

    // a. Générer une adresse multicast encheres
    LOG_INFO("Génération de l'adresse multicast et du port d'enchères...");
    ip_t auc_ip;
    generate_multicast_address(&auc_ip);
    port_t auc_port = get_available_port(AUCTIONS_MIN_PORT, AUCTIONS_MAX_PORT);
    peer->auctions_ip = auc_ip;
    peer->auctions_port = auc_port;

    // b. Initialiser sockets multicast encheres
    if (network_sockets_setup_encheres(&peer->sockets, &peer->auctions_ip,
                                       peer->auctions_port, peer->interface_name) < 0) {
        network_sockets_close(&peer->sockets);
        LOG_ERROR("Échec lors de la création du système.");
        return -1;
    }

    safe_printf("Le système a été créé avec succès :\n");
    print_auctions_system(peer);

    return 0;
}

static int send_peer_info(peer_t *peer, int sock, SSL *tls_socket) {
    message_t *msg = msg_create_peer_info(peer);
    if (!msg) {
        return -1;
    }

    uint8_t buf[TCP_MSG_BUF_SIZE];
    size_t len;
    if (msg_serialize(msg, buf, sizeof(buf), &len) != SERIALIZE_SUCCESS) {
        return -1;
    }

    int ret = 0;
    if (TLS_ON) {
        ret = tls_send_all(tls_socket, buf, len, NULL);
    } else {
        ret = tcp_send_all(sock, buf, len, NULL);
    }

    if (ret < 0) {
        LOG_ERROR("send_peer_info : Erreur lors de l’envoi du message");
        message_free(msg);
        return -1;
    }

    message_free(msg);
    return 0;
}

static int recv_peer_info_resp_alt(peer_t *peer, int sock, SSL *tls_socket,
                                   struct timeval *timeout) {

    uint8_t recv_buf_id[sizeof(app_id_t)];
    ssize_t recv_len_id;

    if (TLS_ON) {
        recv_len_id = tls_recv_all(tls_socket, recv_buf_id, sizeof(recv_buf_id), timeout);
    } else {
        recv_len_id = tcp_recv_all(sock, recv_buf_id, sizeof(recv_buf_id), timeout);
    }

    if (recv_len_id <= 0) {
        LOG_WARNING("Erreur ou fermeture socket TCP.");
        return -1;
    }
    message_t *resp_code = malloc(sizeof(message_t));
    if (!resp_code) {
        LOG_ERROR("Erreur malloc");
        return -1;
    }

    if (msg_deserialize(recv_buf_id, recv_len_id, resp_code, true,
                        CODE_PEER_INFO_RESP_ALT) != DESERIALIZE_SUCCESS) {
        message_free(resp_code);
        return -1;
    }
    peer->id = resp_code->content.peer_info_resp.assigned_id;
    LOG_INFO("Nouvel ID = %u\n", peer->id);
    message_free(resp_code);
    return 0;
}

static int recv_peer_info_resp(peer_t *peer, int sock, SSL *tls_socket) {

    struct timeval timeout = {
        .tv_sec = PEER_RESPONSE_TIMEOUT,
        .tv_usec = 0,
    };

    // Lecture de la réponse
    uint8_t recv_buf[sizeof(code_t)];
    ssize_t recv_len;

    if (TLS_ON) {
        recv_len = tls_recv_all(tls_socket, recv_buf, sizeof(recv_buf), &timeout);
    } else {
        recv_len = tcp_recv_all(sock, recv_buf, sizeof(recv_buf), &timeout);
    }

    if (recv_len <= 0) {
        LOG_WARNING("Message non reçu.");
        return -1;
    }

    message_t *resp = malloc(sizeof(message_t));
    if (!resp) {
        LOG_ERROR("Erreur malloc");
        return -1;
    }

    if (msg_deserialize(recv_buf, recv_len, resp, false, 0) != DESERIALIZE_SUCCESS) {
        message_free(resp);
        return -1;
    }

    switch (resp->code) {
        case CODE_PEER_INFO_RESP_OK:
            LOG_INFO(
                "CODE=%d [CODE_PEER_INFO_RESP_OK] reçu. Mon identifiant est valide\n",
                CODE_PEER_INFO_RESP_OK);
            break;

        case CODE_PEER_INFO_RESP_ALT:
            LOG_INFO("CODE=%d [CODE_PEER_INFO_RESP_ALT] reçu. Lecture du nouvel ID....",
                     CODE_PEER_INFO_RESP_ALT, sizeof(app_id_t));
            if (recv_peer_info_resp_alt(peer, sock, tls_socket, &timeout) < 0) {
                LOG_ERROR("Erreur lors de la lecture du nouvel ID.");
                message_free(resp);
                return -1;
            }
            break;

        default:
            LOG_WARNING("Message inattendu reçu (CODE=%d)", resp->code);
            message_free(resp);
            return -1;
    }
    message_free(resp);
    return 0;
}

static int add_all_peers_system_info(peer_t *peer, int sock, SSL *tls_socket,
                                     int peer_count) {
    int count = 0;
    while (count < peer_count) {
        uint8_t recv_buf[bloc_info_size()];
        ssize_t recv_len;

        if (TLS_ON) {
            recv_len = tls_recv_all(tls_socket, recv_buf, sizeof(recv_buf), NULL);
        } else {
            recv_len = tcp_recv_all(sock, recv_buf, sizeof(recv_buf), NULL);
        }

        if (recv_len <= 0) {
            LOG_ERROR("Erreur récupération des infos.");
            return -1;
        }

        info_t *resp = malloc(sizeof(info_t));
        if (!resp) {
            LOG_ERROR("Erreur malloc");
            return -1;
        }

        if (bloc_info_deserialize(recv_buf, recv_len, resp) != DESERIALIZE_SUCCESS) {
            free(resp);
            return -1;
        }

        info_t new_peer;
        new_peer.id = resp->id;
        new_peer.port = resp->port;

        memcpy(new_peer.ip.addr, resp->ip.addr, IPV6_ADDR_LEN);
        memcpy(new_peer.key.key, resp->key.key, ED25519_PUB_KEY_LEN);

        peer_list_add(&peer->peer_list, &new_peer);

        free(resp);
        count++;
    }
    return count;
}

static int recv_system_info(peer_t *peer, int sock, SSL *tls_socket) {

    struct timeval timeout = {
        .tv_sec = PEER_RESPONSE_TIMEOUT,
        .tv_usec = 0,
    };

    // Lecture de la réponse
    uint8_t recv_buf[msg_prefix_size_system_info()];
    ssize_t recv_len;

    if (TLS_ON) {
        recv_len = tls_recv_all(tls_socket, recv_buf, sizeof(recv_buf), &timeout);
    } else {
        recv_len = tcp_recv_all(sock, recv_buf, sizeof(recv_buf), &timeout);
    }

    if (recv_len <= 0) {
        LOG_WARNING("Erreur ou fermeture socket TCP.");
        return -1;
    }

    message_t *resp = malloc(sizeof(message_t));
    if (!resp) {
        LOG_ERROR("Erreur malloc");
        return -1;
    }

    if (msg_deserialize(recv_buf, recv_len, resp, false, 0) != DESERIALIZE_SUCCESS) {
        message_free(resp);
        return -1;
    }

    switch (resp->code) {
        case CODE_SYSTEM_INFO:
            LOG_INFO("CODE=%d [CODE_SYSTEM_INFO] reçu.\n", CODE_SYSTEM_INFO);

            LOG_INFO("Stockage des infos du système...");
            peer->auctions_ip = resp->content.system_info.auction_ip;
            peer->auctions_port = resp->content.system_info.auction_port;

            LOG_INFO("Initialisation des sockets enchères...");
            if (network_sockets_setup_encheres(&peer->sockets, &peer->auctions_ip,
                                               peer->auctions_port,
                                               peer->interface_name) < 0) {
                LOG_ERROR("Échec lors de la jointure au système d'enchères.");
                return -1;
            }

            LOG_INFO("Récupération et ajout des infos des pairs du système...\n");
            if (add_all_peers_system_info(peer, sock, tls_socket,
                                          resp->content.system_info.peer_count) < 0) {
                LOG_ERROR("Échec lors de l'ajout des pairs du système.");
                message_free(resp);
                return -1;
            }

            safe_printf("Vous avez rejoint le système d'enchères :\n");
            print_auctions_system(peer);
            break;

        default:
            LOG_WARNING("Message inattendu reçu (CODE=%d)", resp->code);
            message_free(resp);
            return -1;
    }

    message_free(resp);
    return 0;
}

int network_join_existing(peer_t *peer) {
    int client_sock = -1;
    SSL *tls_socket_client = NULL;

    for (size_t i = 0; i < peer->peer_list.count; ++i) {
        info_t *candidate = &peer->peer_list.peers[i];
        LOG_INFO("Tentative de connexion à %s:%d...", ip_to_str_static(&candidate->ip),
                 candidate->port);

        client_sock =
            tcp_connect_sock(&candidate->ip, candidate->port, peer->interface_name);
        if (client_sock < 0) {
            LOG_WARNING("Connexion échouée à %s:%d", ip_to_str_static(&candidate->ip),
                        candidate->port);
            goto cleanup;
        }

        LOG_INFO("Connexion réussie à %s:%d", ip_to_str_static(&candidate->ip),
                 candidate->port);

        if (TLS_ON) {
            // Vérification certificat serveur dans recv_system_info
            LOG_INFO("Tentative de sécurisation côté client avec TLS");
            if (init_tls_context(peer, 0) < 0) {
                LOG_ERROR("Erreur lors de l'initialisation du contexte SSL pour le TLS");
                goto cleanup;
            }
            if (upgrade_to_tls_client(peer, client_sock, &tls_socket_client) != 0) {
                LOG_ERROR("Sécurisation TLS client: échouée");
                goto cleanup;
            }

            LOG_INFO("Sécurisation TLS client : réussie");
        }

        LOG_INFO("Envoi du message CODE=%d [CODE_PEER_INFO]...", CODE_PEER_INFO);

        if (send_peer_info(peer, client_sock, tls_socket_client) < 0) {
            LOG_WARNING("Échec de l'envoi du message CODE=%d [CODE_PEER_INFO]",
                        CODE_PEER_INFO);
            goto cleanup;
        }

        LOG_INFO("CODE=%d [CODE_PEER_INFO] envoyé.", CODE_PEER_INFO);
        LOG_INFO("Attente de la réponse CODE=(%d/%d) "
                 "[CODE_PEER_INFO_RESP_OK]/[CODE_PEER_INFO_RESP_ALT] ...",
                 CODE_PEER_INFO_RESP_OK, CODE_PEER_INFO_RESP_ALT);

        if (recv_peer_info_resp(peer, client_sock, tls_socket_client) < 0) {
            goto cleanup;
        }

        LOG_INFO("Attente de la réponse CODE=%d [CODE_SYSTEM_INFO] ...",
                 CODE_SYSTEM_INFO);

        if (recv_system_info(peer, client_sock, tls_socket_client) < 0) {
            goto cleanup;
        }

        if (tls_socket_client != NULL) {
            tls_close(tls_socket_client);

        } else if (client_sock >= 0) {
            close(client_sock);
        }

        return 0; // Jointure réussie
    }

cleanup:

    if (tls_socket_client != NULL) {
        tls_close(tls_socket_client);

    } else if (client_sock >= 0) {
        close(client_sock);
    }

    LOG_ERROR("Aucune jointure possible - tous les pairs ont échoué.");
    return -1;
}

static int send_join_response(peer_t *peer, message_t *msg,
                              struct sockaddr_in6 src_addr) {

    message_t *resp = msg_create_join_response(peer);
    if (!resp) {
        return -1;
    }

    uint8_t out_buf[UDP_MSG_BUF_SIZE];
    size_t out_len;
    if (msg_serialize(resp, out_buf, sizeof(out_buf), &out_len) != SERIALIZE_SUCCESS) {
        message_free(resp);
        return -1;
    }

    LOG_INFO("Adresse IP du pair demandeur : %s",
             in6_addr_to_str_static(&src_addr.sin6_addr));
    LOG_INFO("Port du pair demandeur : %u", ntohs(src_addr.sin6_port));

    int sendsock = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);
    if (sendsock < 0) {
        message_free(resp);
        return -1;
    }

    int ret = sendto(sendsock, out_buf, out_len, 0, (struct sockaddr *)&src_addr,
                     sizeof(src_addr));

    close(sendsock);
    message_free(resp);
    if (ret == 0) {
        LOG_WARNING("sendto : 0 octet envoyé");
    }
    return (ret < 0) ? -1 : 0;
}

void handle_join_request(peer_t *peer, message_t *msg, struct sockaddr_in6 src_addr) {
    LOG_INFO("CODE=%d [CODE_JOIN_REQUEST] reçu.\n", CODE_JOIN_REQUEST);
    if (send_join_response(peer, msg, src_addr) < 0) {
        LOG_WARNING("Échec de l’envoi du message UDP CODE=%d [CODE_JOIN_RESPONSE].",
                    CODE_JOIN_RESPONSE);
    } else {
        LOG_INFO("CODE=%d [CODE_JOIN_RESPONSE] envoyé.", CODE_JOIN_RESPONSE);
    }
}

static int send_peer_info_resp_ok(int sock, SSL *tls_sock) {
    message_t *resp = msg_create_peer_info_resp_ok();
    if (!resp) {
        return -1;
    }

    uint8_t out_buf[TCP_MSG_BUF_SIZE];
    size_t out_len;
    if (msg_serialize(resp, out_buf, sizeof(out_buf), &out_len) != SERIALIZE_SUCCESS) {
        message_free(resp);
        return -1;
    }

    LOG_INFO("Envoi du message CODE=%d [CODE_PEER_INFO_RESP_OK]...",
             CODE_PEER_INFO_RESP_OK);

    int ret = 0;
    if (TLS_ON) {
        ret = tls_send_all(tls_sock, out_buf, out_len, NULL);
    } else {
        ret = tcp_send_all(sock, out_buf, out_len, NULL);
    }
    if (ret < 0) {
        LOG_ERROR("Erreur lors de l'envoi du message");
        message_free(resp);
        return -1;
    }
    LOG_INFO("CODE=%d [CODE_PEER_INFO_RESP_OK] envoyé.", CODE_PEER_INFO_RESP_OK);
    message_free(resp);
    return 0;
}

/**
 * @param dest_info sera mis à jour avec le nouvel ID
 */
static int send_peer_info_resp_alt(peer_t *peer, info_t *dest_info, int sock,
                                   SSL *tls_sock) {
    app_id_t new_id = generate_valid_id(peer);
    if (new_id == 0) {
        LOG_ERROR("Erreur lors de la génération d'un nouvel ID valide.");
        return -1;
    }
    LOG_INFO("Nouveau ID généré : %u\n", new_id);
    if (new_id < 0) {
        LOG_ERROR("Échec de la génération d'un nouvel ID valide.");
        return -1;
    }
    // Mettre à jour l'ID du pair
    dest_info->id = new_id;
    message_t *resp = msg_create_peer_info_resp_alt(dest_info->id);
    if (!resp) {
        return -1;
    }

    uint8_t out_buf[TCP_MSG_BUF_SIZE];
    size_t out_len;
    if (msg_serialize(resp, out_buf, sizeof(out_buf), &out_len) != SERIALIZE_SUCCESS) {
        message_free(resp);
        return -1;
    }

    LOG_INFO("Envoi du message CODE=%d [CODE_PEER_INFO_RESP_ALT]...",
             CODE_PEER_INFO_RESP_ALT);

    int ret = 0;
    if (TLS_ON) {
        ret = tls_send_all(tls_sock, out_buf, out_len, NULL);
    } else {
        ret = tcp_send_all(sock, out_buf, out_len, NULL);
    }
    if (ret < 0) {
        LOG_ERROR("Erreur lors de l'envoi du message");
        message_free(resp);
        return -1;
    }
    LOG_INFO("CODE=%d [CODE_PEER_INFO_RESP_ALT] envoyé.\n", CODE_PEER_INFO_RESP_ALT);
    message_free(resp);
    return 0;
}

static int send_new_peer_info(int sock, SSL *tls_sock, uint8_t *msg_buf, size_t msg_len) {
    LOG_INFO("Envoi du message CODE=%d [CODE_NEW_PEER_INFO]...", CODE_NEW_PEER_INFO);

    struct timeval timeout = {.tv_sec = TCP_SEND_ALL_TIMEOUT, .tv_usec = 0};
    ssize_t sent = 0;
    if (TLS_ON) {
        sent = tls_send_all(tls_sock, msg_buf, msg_len, &timeout);
    } else {
        sent = tcp_send_all(sock, msg_buf, msg_len, &timeout);
    }

    if (sent < 0 || (size_t)sent != msg_len) {
        LOG_ERROR("send_new_peer_info : erreur d'envoi");
        return -1;
    }
    LOG_INFO("CODE=%d [CODE_NEW_PEER_INFO] envoyé.\n", CODE_NEW_PEER_INFO);
    return 0;
}

static int share_new_peer_info(peer_t *peer, info_t *new_peer_info) {
    message_t *msg = msg_create_new_peer_info(new_peer_info);
    if (!msg) {
        LOG_ERROR("Échec de création du message.");
        return -1;
    }

    uint8_t msg_buf[TCP_MSG_BUF_SIZE];
    size_t msg_len;
    serialize_result_t ret = msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len);
    message_free(msg);
    if (ret != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de sérialisation du message.");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_PEER_LIST);

    int success_count = 0;
    for (size_t i = 0; i < peer->peer_list.count; ++i) {
        int sock = -1;
        SSL *tls_sock = NULL;
        info_t *candidate = &peer->peer_list.peers[i];

        LOG_INFO("Tentative de connexion à %s:%d...", ip_to_str_static(&candidate->ip),
                 candidate->port);
        sock = tcp_connect_sock(&candidate->ip, candidate->port, peer->interface_name);
        if (sock < 0) {
            LOG_WARNING("Connexion échouée à %s:%d", ip_to_str_static(&candidate->ip),
                        candidate->port);
            goto cleanup; // Continue
        }

        if (TLS_ON) {
            LOG_INFO("Tentative de sécurisation côté client avec TLS");
            if (init_tls_context(peer, 0) < 0) {
                LOG_ERROR("Erreur lors de l'initialisation du contexte SSL pour le TLS");
                goto cleanup; // Continue
            }
            if (upgrade_to_tls_client(peer, sock, &tls_sock) != 0) {
                LOG_ERROR("Sécurisation TLS client: échouée");
                goto cleanup; // Continue
            }
            LOG_INFO("Sécurisation TLS client : réussie");
        }
        LOG_INFO("Connexion réussie à %s:%d", ip_to_str_static(&candidate->ip),
                 candidate->port);

        if (send_new_peer_info(sock, tls_sock, msg_buf, msg_len) < 0) {
            LOG_WARNING("Échec envoi CODE_NEW_PEER_INFO à %s",
                        ip_to_str_static(&candidate->ip));
            goto cleanup; // Continue
        }
        success_count++;
    cleanup:
        if (tls_sock != NULL) {
            tls_close(tls_sock);
        } else if (sock >= 0) {
            close(sock);
        }
    }

    if (success_count < peer->peer_list.count) {
        LOG_WARNING("CODE=%d [CODE_NEW_PEER_INFO] partiellement envoyé.",
                    CODE_NEW_PEER_INFO);
        LOG_WARNING("Seul %d/%d pairs ont reçu les infos du nouveau pair.\n",
                    success_count, peer->peer_list.count);
    } else {
        LOG_INFO("CODE=%d [CODE_NEW_PEER_INFO] entièrement envoyé. (Total partages : "
                 "%d)\n",
                 CODE_NEW_PEER_INFO, success_count);
    }

    peer_unlock(peer, PEER_MUTEX_PEER_LIST);
    return 0;
}

static int send_all_peers_system_info(peer_t *peer, int sock, SSL *tls_sock, int nb) {
    int count = 0;
    while (count < nb) {
        uint8_t info_buf[bloc_info_size()];
        size_t info_len;
        info_t *send_bloc = malloc(sizeof(info_t));
        if (!send_bloc) {
            LOG_ERROR("Erreur malloc");
            return -1;
        }

        if (count == 0) {
            send_bloc->id = peer->id;
            memcpy(send_bloc->ip.addr, peer->ip.addr, sizeof(ip_t));
            send_bloc->port = peer->port;
            memcpy(send_bloc->key.key, peer->public_key.key, sizeof(cle_pub_t));
        } else {
            info_t *remote_info = &peer->peer_list.peers[count - 1];
            send_bloc->id = remote_info->id;
            memcpy(send_bloc->ip.addr, remote_info->ip.addr, sizeof(ip_t));
            send_bloc->port = remote_info->port;
            memcpy(send_bloc->key.key, remote_info->key.key, sizeof(cle_pub_t));
        }

        if (bloc_info_serialize(send_bloc, info_buf, sizeof(info_buf), &info_len) !=
            SERIALIZE_SUCCESS) {
            free(send_bloc);
            return -1;
        }
        int ret = 0;
        if (TLS_ON) {
            ret = tls_send_all(tls_sock, info_buf, info_len, NULL);
        } else {
            ret = tcp_send_all(sock, info_buf, info_len, NULL);
        }
        if (ret < 0) {
            LOG_ERROR("Erreur lors du bloc info");
            free(send_bloc);
            return -1;
        }

        LOG_INFO("Envoi d'un bloc info CODE=%d [CODE_SYSTEM_INFO]...", CODE_SYSTEM_INFO);
        free(send_bloc);
        count++;
    }

    return count;
}

static int send_system_info(peer_t *peer, int sock, SSL *tls_sock) {

    peer_lock(peer, PEER_MUTEX_PEER_LIST); // Plus haut niveau de verrouillage pour éviter
                                           // conflit sur nb et send_nb
    message_t *msg = msg_create_system_info(peer);
    if (!msg) {
        LOG_ERROR("Échec de création du message.");
        return -1;
    }

    uint8_t msg_buf[TCP_MSG_BUF_SIZE];
    size_t msg_len;
    if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) != SERIALIZE_SUCCESS) {
        LOG_ERROR("Échec de sérialisation du message.");
        message_free(msg);
        return -1;
    }

    int ret = 0;
    if (TLS_ON) {
        ret = tls_send_all(tls_sock, msg_buf, msg_len, NULL);
    } else {
        ret = tcp_send_all(sock, msg_buf, msg_len, NULL);
    }
    if (ret < 0) {
        LOG_ERROR("Erreur lors de l'envoi du message");
        message_free(msg);
        return -1;
    }

    int nb = msg->content.system_info.peer_count;
    message_free(msg);
    LOG_INFO("Préfixe CODE=%d [CODE_SYSTEM_INFO] envoyé.", CODE_SYSTEM_INFO);

    int send_nb = send_all_peers_system_info(peer, sock, tls_sock, nb);
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    if (send_nb < 0) {
        LOG_ERROR("Erreur lors de l'envoi des infos de chaque pair du système.");
        return -1;
    } else if (nb != send_nb) {
        LOG_WARNING("CODE=%d [CODE_SYSTEM_INFO] partiellement envoyé.", CODE_SYSTEM_INFO);
        LOG_WARNING("Seul %d/%d des infos sur les pairs du système ont été envoyés.\n",
                    send_nb, nb);
    } else {
        LOG_INFO("CODE=%d [CODE_SYSTEM_INFO] entièrement envoyé. (Total blocs : %d)\n",
                 CODE_SYSTEM_INFO, send_nb);
    }
    return 0;
}

void handle_peer_info(peer_t *peer, message_t *msg, int sock, SSL *tls_sock) {
    LOG_INFO("CODE=%d [CODE_PEER_INFO] reçu.\n", CODE_PEER_INFO);

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    info_t *new_peer_info =
        &msg->content.peer_info.info; // Celui là sera copié en bas avant stockage
    int found_peer = peer_list_contains(&peer->peer_list, new_peer_info->id);
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    if (peer->id != new_peer_info->id && !found_peer) {
        LOG_INFO("ID du nouveau pair valide.\n");
        if (send_peer_info_resp_ok(sock, tls_sock) < 0) {
            LOG_ERROR("Échec de l'envoi du message CODE=%d [CODE_PEER_INFO_RESP_OK].",
                      CODE_PEER_INFO_RESP_OK);
            return;
        }
    } else {
        LOG_INFO("ID du nouveau pair invalide, un nouveau ID sera attribué.\n");
        if (send_peer_info_resp_alt(peer, new_peer_info, sock, tls_sock) < 0) {
            LOG_ERROR("Échec de l'envoi du message CODE=%d [CODE_PEER_INFO_RESP_ALT].",
                      CODE_PEER_INFO_RESP_ALT);
            return;
        }
    }
    LOG_INFO("Envoi des infos du nouveau pair à tous les autres pairs du système...");
    if (share_new_peer_info(peer, new_peer_info) < 0) {
        LOG_ERROR("Échec de l'envoi des infos du pair.");
        return;
    }

    LOG_INFO("Envoi des infos du système au nouveau pair...");
    if (send_system_info(peer, sock, tls_sock) < 0) {
        LOG_ERROR("Échec de l'envoi des infos du système.");
        return;
    }

    LOG_INFO("Ajout du nouveau pair à la liste des pairs connus...");
    info_t new_peer;
    new_peer.id = new_peer_info->id;
    new_peer.port = new_peer_info->port;
    memcpy(new_peer.ip.addr, new_peer_info->ip.addr, IPV6_ADDR_LEN);
    memcpy(new_peer.key.key, new_peer_info->key.key, ED25519_PUB_KEY_LEN);

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    peer_list_add(&peer->peer_list, &new_peer);
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);
}

void handle_new_peer_info(peer_t *peer, message_t *msg) {
    LOG_INFO("CODE=%d [CODE_NEW_PEER_INFO] reçu.", CODE_NEW_PEER_INFO);

    info_t *new_peer_info = &msg->content.peer_info.info;

    LOG_INFO("Ajout du nouveau pair à la liste des pairs connus...");
    info_t new_peer;
    new_peer.id = new_peer_info->id;
    new_peer.port = new_peer_info->port;
    memcpy(new_peer.ip.addr, new_peer_info->ip.addr, IPV6_ADDR_LEN);
    memcpy(new_peer.key.key, new_peer_info->key.key, ED25519_PUB_KEY_LEN);

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    peer_list_add(&peer->peer_list, &new_peer);
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);
}