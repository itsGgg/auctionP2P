#include "../include/network_consensus.h"
#include "../include/debug.h"
#include "../include/message.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_handler.h"
#include "../include/network_leave.h"
#include "../include/network_lost_udp.h"
#include "../include/network_missing_peer.h"
#include "../include/peer.h"
#include "../include/safe_output.h"
#include "../include/utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

/**
 *  Messages necessitants un processus de consensus:
 *  10 12 13 16 18
 **/

static void free_consensus_context(consensus_context_t *ctx) {

    if (ctx->sigs != NULL) {
        for (size_t i = 0; i < ctx->remaining_list.count + ctx->received_responses; i++) {
            if (ctx->sigs[i].signature.data != NULL) {
                free(ctx->sigs[i].signature.data);
                ctx->sigs[i].signature.data = NULL;
            }
        }
        free(ctx->sigs);
        ctx->sigs = NULL;
    }
}

/**
 * Initialise la structure permettant le suivi d'une validation
 * @return 0 si OK, -1 si erreur
 **/
static int initialize_consensus_context(peer_t *peer, uint8_t *mess, lmess_t lmess,
                                        consensus_context_t *ctx) {
    ctx->msg = mess;
    ctx->lmess = lmess;

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    int length = peer->peer_list.count;
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    if (peer_id_list_init(&ctx->remaining_list) < 0) {
        LOG_ERROR("Erreur lors de l'initialisation de la liste des IDs des pairs");
        goto cleanup_return;
    }
    // Ajout tous les ID des pairs dans la liste
    peer_list_t pl = peer->peer_list;
    for (size_t i = 0; i < length; i++) {
        peer_id_list_add(&ctx->remaining_list, pl.peers[i].id);
    }
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);

    ctx->received_responses = 0;
    LOG_INFO("Nombre total de réponses attendues : [%u]\n", ctx->remaining_list.count);

    ctx->sigs = malloc(sizeof(peer_signature_t) * length);
    if (!ctx->sigs) {
        LOG_ERROR("malloc sigs (initialize_consensus_context)");
        goto cleanup_return;
    }

    for (size_t i = 0; i < length; i++) {
        ctx->sigs[i].id = 0;
        ctx->sigs[i].signature.data = NULL;
        ctx->sigs[i].signature.length = 0;
    }

    ctx->done = false;

    return 0;

cleanup_return:
    free_consensus_context(ctx);
    ctx = NULL;
    return -1;
}

static int ask_csss_recv_answer(peer_t *peer, struct timeval *timeout,
                                message_t **out_msg) {

    peer_lock(peer, PEER_MUTEX_UDP_PERSO_RECV);
    int sock_listen = peer->sockets.sock_udp_perso_recv;
    peer_unlock(peer, PEER_MUTEX_UDP_PERSO_RECV);

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

    struct sockaddr_in6 src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    socklen_t addr_len = sizeof(src_addr);
    uint8_t recv_buf[UDP_MSG_BUF_SIZE];

    peer_lock(peer, PEER_MUTEX_UDP_PERSO_RECV);
    ssize_t recv_len = recvfrom(sock_listen, recv_buf, sizeof(recv_buf), 0,
                                (struct sockaddr *)&src_addr, &addr_len);
    peer_unlock(peer, PEER_MUTEX_UDP_PERSO_RECV);
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

    *out_msg = resp;
    return 0;
}

static void *ask_csss_handle_consensus_codes(void *arg) {
    consensus_thread_ctx_t *thread_ctx = (consensus_thread_ctx_t *)arg;
    if (!thread_ctx) {
        LOG_ERROR("ask_csss_handle_consensus_codes : thread_ctx NULL");
        return NULL;
    }

    peer_t *peer = thread_ctx->peer;
    message_t *msg = thread_ctx->msg;

    handle_perso_udp_codes(peer, msg);
    LOG_INFO("Fermeture du thread de gestion externe de la réponse au consensus.");
    if (msg) {
        message_free(msg);
    }
    free(thread_ctx);
    return NULL;
}

/**
 * Vérifie la validation d'un pair
 * @return 0 si valide, 1 si invalide, -1 si erreur
 **/
static int verify_validation(peer_t *peer, message_t *msg, consensus_context_t *ctx) {
    int ret = 0;

    // Comparaison du message reçu avec le message d'origine
    if (msg->content.validation.message.length != ctx->lmess ||
        memcmp(msg->content.validation.message.data, ctx->msg, ctx->lmess) != 0) {
        LOG_ERROR("Message reçu différent du message d'origine");
        ret = 1;
        goto cleanup;
    }

    app_id_t sender_id = msg->content.validation.id;

    if (peer_id_list_contains(&ctx->remaining_list, sender_id)) {

        cle_pub_t public_key;
        bool found_key = false;

        peer_lock(peer, PEER_MUTEX_PEER_LIST);
        for (size_t j = 0; j < peer->peer_list.count; j++) {
            if (peer->peer_list.peers[j].id == sender_id) {
                memcpy(&public_key, &peer->peer_list.peers[j].key, sizeof(cle_pub_t));
                found_key = true;
                break;
            }
        }
        peer_unlock(peer, PEER_MUTEX_PEER_LIST);

        if (!found_key) {
            LOG_ERROR("Clé publique du pair %u non trouvée", sender_id);
            ret = -1;
            goto cleanup;
        }

        EVP_PKEY *evp_key = NULL;
        uint8_t *key_ptr = public_key.key;
        if (char_to_evp_pkey(&key_ptr, ED25519_PUB_KEY_LEN, 0, &evp_key) < 0) {
            LOG_ERROR("Erreur lors de la conversion de la clé publique en EVP_PKEY");
            ret = -1;
            goto cleanup;
        }
        LOG_INFO("Clé publique du pair %u convertie en EVP_PKEY", sender_id);

        if (verify_signature_with_key(evp_key, &msg->content.validation.message,
                                      &msg->content.validation.signature) < 0) {
            LOG_ERROR("Signature invalide pour le message du pair %u", sender_id);
            ret = 1;
            goto cleanup;
        }
        LOG_INFO("Message vérifié avec la signature");

        ret = 0;
        goto cleanup;
    }
    LOG_INFO("ID=%u deja traité ou non attendu", sender_id);
    ret = 1;
    goto cleanup;

cleanup:
    return ret;
}

static void handle_validation(peer_t *peer, message_t *resp, int *count,
                              consensus_context_t *ctx) {

    int verify_result = verify_validation(peer, resp, ctx);
    if (verify_result < 0) {
        LOG_ERROR("Erreur lors de la vérification de la validation du pair %u",
                  resp->content.validation.id);
        return;
    } else if (verify_result == 1) {
        LOG_INFO("Validation du pair %u invalide", resp->content.validation.id);
        return;
    } else {
        LOG_INFO("Validation du pair %u valide", resp->content.validation.id);
        *count += 1;

        if (peer_id_list_remove(&ctx->remaining_list, resp->content.validation.id) < 0) {
            LOG_ERROR("Erreur lors de la suppression de l'ID %u de la liste des pairs "
                      "restants",
                      resp->content.validation.id);
        }
        ctx->received_responses++;

        ctx->sigs[ctx->received_responses - 1].id = resp->content.validation.id;

        ctx->sigs[ctx->received_responses - 1].signature.data =
            malloc(resp->content.validation.signature.length);
        if (!ctx->sigs[ctx->received_responses - 1].signature.data) {
            LOG_ERROR("malloc signature.data (handle_validation)");
            return;
        }
        memcpy(ctx->sigs[ctx->received_responses - 1].signature.data,
               resp->content.validation.signature.data,
               resp->content.validation.signature.length);

        ctx->sigs[ctx->received_responses - 1].signature.length =
            resp->content.validation.signature.length;

        LOG_INFO("Signature du pair %u enregistrée", resp->content.validation.id);

        if (ctx->remaining_list.count == 0) {
            ctx->done = true;
        }
    }
}

static int ask_csss_wait_for_answer(peer_t *peer, consensus_context_t *ctx) {
    struct timeval start_time, now, elapsed;
    gettimeofday(&start_time, NULL);

    int count = 0;

    while (1) {

        // Calculer le temps restant
        gettimeofday(&now, NULL);
        timersub(&now, &start_time, &elapsed);

        if (elapsed.tv_sec >= CONSENSUS_TIMEOUT) {
            break;
        }

        struct timeval timeout;
        timeout.tv_sec = CONSENSUS_TIMEOUT - elapsed.tv_sec;
        timeout.tv_usec = 0;

        message_t *resp = NULL;
        int response = ask_csss_recv_answer(peer, &timeout, &resp);
        if (response < 0) {
            break;
        } else if (response == 1) {
            message_free(resp);
            continue;
        }

        if (resp->code != CODE_VALIDATION) {
            pthread_t thread;
            consensus_thread_ctx_t *thread_ctx = malloc(sizeof(consensus_thread_ctx_t));
            if (!thread_ctx) {
                LOG_ERROR("malloc thread_ctx (wait_for_answer)");
                message_free(resp);
                return -1;
            }
            thread_ctx->peer = peer;
            thread_ctx->msg = resp;
            if (pthread_create(&thread, NULL, ask_csss_handle_consensus_codes,
                               thread_ctx) != 0) {
                LOG_ERROR("pthread_create (wait_for_answer)");
                free(thread_ctx);
                message_free(resp);
                return -1;
            }
            pthread_detach(thread);
            LOG_INFO("Thread créé pour gérer la réponse au consensus, externe CODE=%d",
                     resp->code);
        } else {
            handle_validation(peer, resp, &count, ctx);
            message_free(resp);
        }
    }

    return count;
}

/**
 *  Demande un consensus et attend des réponses
 *  @return 0 OK, -1 erreur
 **/
int ask_consensus(peer_t *peer, uint8_t *mess, lmess_t lmess) {
    atomic_store(&in_use_udp_perso,
                 true); // Un peu tardif, mais on a la relance de message au cas où on
    // se fait voler le code 2 par le event handler

    LOG_INFO("Début du consensus...");
    consensus_context_t *ctx = NULL;
    int ret = 0;

    peer_lock(peer, PEER_MUTEX_PEER_LIST);
    bool is_alone = peer->peer_list.count == 0;
    peer_unlock(peer, PEER_MUTEX_PEER_LIST);
    if (is_alone) {
        LOG_INFO("Je suis seul, consensus inutile");
        goto cleanup;
    }

    ctx = malloc(sizeof(consensus_context_t));
    memset(ctx, 0, sizeof(consensus_context_t));
    if (!ctx) {
        LOG_ERROR("malloc");
        ret = -1;
        goto cleanup;
    }

    if (initialize_consensus_context(peer, mess, lmess, ctx) < 0) {
        LOG_ERROR("Initialisation du consensus échouée");
        ret = -1;
        goto cleanup;
    }

    for (int attempt = 1; attempt <= CONSENSUS_MAX_ATTEMPT; attempt++) {
        LOG_INFO("Tentative [%d/%d] du consensus", attempt, CONSENSUS_MAX_ATTEMPT);
        int count = ask_csss_wait_for_answer(peer, ctx);
        LOG_INFO("Nombre de réponses reçues : [%d] à la tentative [%d/%d]", count,
                 attempt, CONSENSUS_MAX_ATTEMPT);
        // Relancer les pairs

        relaunch_consensus_peer(peer, ctx);

        if (ctx->done) {
            break;
        }
    }

    if (ctx->done) {
        LOG_INFO("Consensus terminé avec succès");
        // Préparer le message final
        send_final_message(peer, ctx);

    } else {
        // Pairs disparus
        LOG_WARNING("Consensus non terminé, pas assez de réponses");
        LOG_WARNING("Des pairs ont disparu");
        handle_missing_peer(peer, ctx->remaining_list.count, ctx->remaining_list.ids);
    }

cleanup:
    if (ctx != NULL) {
        free_consensus_context(ctx);
        ctx = NULL;
    }
    atomic_store(&in_use_udp_perso, false);
    return ret;
}

static int send_validation(peer_t *peer, uint8_t *mess, lmess_t lmess,
                           info_t destination) {

    int sock_send = -1;
    mess_t *message = NULL;
    sign_t *self_sig = NULL;
    message_t *validation = NULL;
    int ret = 0;

    message = malloc(sizeof(mess_t));
    if (!message) {
        LOG_ERROR("Échec d'allocation pour le message");
        ret = -1;
        goto cleanup_return;
    }
    message->data = mess;
    message->length = lmess;

    self_sig = malloc(sizeof(sign_t));
    if (!self_sig) {
        LOG_ERROR("Échec d'allocation pour la signature");
        ret = -1;
        goto cleanup_return;
    }

    if (sign_message_with_key(peer->tls.pkey, message, self_sig) < 0) {
        ret = -1;
        goto cleanup_return;
    }

    validation = msg_create_validation(peer->id, message, self_sig);
    if (!validation) {
        LOG_ERROR("Échec de création du message CODE_VALIDATION");
        goto cleanup_return;
    }

    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    if (msg_serialize(validation, msg_buf, sizeof(msg_buf), &msg_len) !=
        SERIALIZE_SUCCESS) {
        LOG_INFO("Erreur sérialisation CODE_VALIDATION.");
        goto cleanup_return;
    }

    sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);
    if (sock_send < 0) {
        goto cleanup_return;
    }

    struct sockaddr_in6 addr =
        make_addr(&destination.ip, destination.port, peer->interface_name);

    ssize_t sent =
        sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr));

    if (sent < 0) {
        LOG_ERROR("Échec de l’envoi du message CODE=%d [CODE_VALIDATION]",
                  CODE_VALIDATION);
        goto cleanup_return;
    }

    LOG_INFO("CODE=%d [CODE_VALIDATION] envoyé\n", CODE_VALIDATION);

cleanup_return:
    if (sock_send >= 0) {
        close(sock_send);
    }
    if (message != NULL) {
        free(message);
        message = NULL;
    }
    if (self_sig != NULL) {
        if (self_sig->data != NULL) {
            free(self_sig->data);
            self_sig->data = NULL;
        }
        free(self_sig);
        self_sig = NULL;
    }
    if (validation != NULL) {
        message_free(validation);
        validation = NULL;
    }
    return ret;
}

static int resp_csss_recv_answer(peer_t *peer, struct timeval *timeout,
                                 message_t **out_msg) {

    peer_lock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
    int sock_listen = peer->sockets.sock_udp_encheres_recv;
    peer_unlock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
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
        LOG_WARNING("recv_answer : timeout expiré");
        return -1; // timeout expiré
    }

    struct sockaddr_in6 src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    socklen_t addr_len = sizeof(src_addr);
    uint8_t recv_buf[UDP_MSG_BUF_SIZE];
    peer_lock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
    ssize_t recv_len = recvfrom(sock_listen, recv_buf, sizeof(recv_buf), 0,
                                (struct sockaddr *)&src_addr, &addr_len);
    peer_unlock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
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

    *out_msg = resp;
    return 0;
}

static void *resp_csss_handle_consensus_codes(void *arg) {
    consensus_thread_ctx_t *thread_ctx = (consensus_thread_ctx_t *)arg;
    if (!thread_ctx) {
        LOG_ERROR("resp_csss_handle_consensus_codes : thread_ctx NULL");
        return NULL;
    }

    peer_t *peer = thread_ctx->peer;
    message_t *msg = thread_ctx->msg;

    handle_encheres_codes(peer, msg);
    LOG_INFO("Fermeture du thread de gestion externe de la réponse au consensus.");
    if (msg) {
        message_free(msg);
    }
    free(thread_ctx);
    return NULL;
}

static int verify_consensus(peer_t *peer, message_t *msg, consensus_context_t *ctx,
                            info_t validation_dest) {
    int ret = 0;

    // Comparaison du message reçu avec le message d'origine
    if (msg->content.consensus.message.length != ctx->lmess ||
        memcmp(msg->content.consensus.message.data, ctx->msg, ctx->lmess) != 0) {
        LOG_ERROR("Message reçu différent du message d'origine");
        ret = 1;
        goto cleanup;
    }

    app_id_t sender_id = msg->content.consensus.id;

    if (validation_dest.id == sender_id) {

        cle_pub_t public_key = validation_dest.key;

        EVP_PKEY *evp_key = NULL;
        uint8_t *key_ptr = public_key.key;
        if (char_to_evp_pkey(&key_ptr, ED25519_PUB_KEY_LEN, 0, &evp_key) < 0) {
            LOG_ERROR("Erreur lors de la conversion de la clé publique en EVP_PKEY");
            ret = -1;
            goto cleanup;
        }
        LOG_INFO("Clé publique du pair %u convertie en EVP_PKEY", sender_id);

        if (verify_signature_with_key(evp_key, &msg->content.consensus.message,
                                      &msg->content.consensus.signature) != 0) {
            LOG_ERROR("Signature invalide pour le message du pair %u", sender_id);
            ret = 1;
            goto cleanup;
        }
        LOG_INFO("Message vérifié avec la signature");

        ret = 0;
        goto cleanup;
    }
    LOG_INFO("ID=%u deja traité ou non attendu", sender_id);
    ret = 1;
    goto cleanup;

cleanup:
    return ret;
}

static int handle_consensus(peer_t *peer, message_t *resp, consensus_context_t *ctx,
                            info_t validation_dest) {
    int verify_result = verify_consensus(peer, resp, ctx, validation_dest);
    if (verify_result < 0) {
        LOG_ERROR("Erreur lors de la vérification de la finalisation du consensus du "
                  "pair %u",
                  resp->content.consensus.id);
        return -1;
    } else if (verify_result == 1) {
        LOG_INFO("CODE=%d [CODE_CONSENSUS] du pair %u invalide\n", CODE_CONSENSUS,
                 resp->content.consensus.id);
        return 1; // Invalide
    } else {
        LOG_INFO("CODE=%d [CODE_CONSENSUS] du pair %u valide\n", CODE_CONSENSUS,
                 resp->content.consensus.id);
        return 0; // Valide
    }
}

static int resp_csss_wait_for_answer(peer_t *peer, consensus_context_t *ctx,
                                     info_t validation_dest) {
    struct timeval start_time, now, elapsed;
    gettimeofday(&start_time, NULL);

    while (1) {

        // Calculer le temps restant
        gettimeofday(&now, NULL);
        timersub(&now, &start_time, &elapsed);

        if (elapsed.tv_sec >= CONSENSUS_TIMEOUT) {
            break;
        }

        struct timeval timeout;
        timeout.tv_sec = CONSENSUS_TIMEOUT - elapsed.tv_sec;
        timeout.tv_usec = 0;

        message_t *resp = NULL;
        int response = resp_csss_recv_answer(peer, &timeout, &resp);
        if (response < 0) {
            break;
        } else if (response == 1) {
            message_free(resp);
            continue;
        }

        if (resp->code != CODE_CONSENSUS) {
            pthread_t thread;
            consensus_thread_ctx_t *thread_ctx = malloc(sizeof(consensus_thread_ctx_t));
            if (!thread_ctx) {
                LOG_ERROR("malloc thread_ctx (wait_for_answer)");
                message_free(resp);
                return -1;
            }
            thread_ctx->peer = peer;
            thread_ctx->msg = resp;
            if (pthread_create(&thread, NULL, resp_csss_handle_consensus_codes,
                               thread_ctx) != 0) {
                LOG_ERROR("pthread_create (wait_for_answer)");
                free(thread_ctx);
                message_free(resp);
                return -1;
            }
            pthread_detach(thread);
            LOG_INFO("Thread créé pour gérer la réponse au consensus, externe CODE=%d",
                     resp->code);
        } else {
            int ret = handle_consensus(peer, resp, ctx, validation_dest);
            message_free(resp);
            if (ret == 0) {
                // LOG_INFO("Réponse au consensus traitée avec succès, complet");
                return 0; // Réponse reçue avec succès

            } else if (ret < 0) {
                // LOG_ERROR("Erreur lors du traitement de la réponse au consensus");
                return -1; // Erreur dans le traitement

            } else {
                // LOG_WARNING("CODE_CONSENSUS traité, mais pas complet");
                // On continue à attendre d'autres réponses
            }
        }
    }

    return 1;
}

int respond_consensus(peer_t *peer, uint8_t *mess, lmess_t lmess,
                      info_t validation_dest) {

    atomic_store(&in_use_udp_encheres, true);

    consensus_context_t *ctx = NULL;
    int ret = 0;

    ctx = malloc(sizeof(consensus_context_t));
    memset(ctx, 0, sizeof(consensus_context_t));
    if (!ctx) {
        LOG_ERROR("malloc");
        ret = -1;
        goto cleanup;
    }

    if (initialize_consensus_context(peer, mess, lmess, ctx) < 0) {
        LOG_ERROR("Initialisation du consensus échouée");
        ret = -1;
        goto cleanup;
    }

    for (int attempt = 1; attempt <= CONSENSUS_MAX_ATTEMPT; attempt++) {

        if (send_validation(peer, mess, lmess, validation_dest) < 0) {
            ret = -1;
            goto cleanup;
        }

        LOG_INFO("Tentative [%d/%d] d'attente de la réponse du consensus", attempt,
                 CONSENSUS_MAX_ATTEMPT);

        int resp = resp_csss_wait_for_answer(peer, ctx, validation_dest);
        if (resp < 0) {
            LOG_ERROR("Échec de la réception des réponses du consensus");
            ret = -1;
            goto cleanup;
        } else if (resp == 0) {
            LOG_INFO("Réponse au consensus reçue et traitée avec succès");
            ret = 0;
            goto cleanup;
        }

        LOG_INFO("Aucune réponse reçue...");
    }

    LOG_WARNING("Le superviseur a disparu");
    handle_missing_supervisor(peer, validation_dest);

cleanup:
    if (ctx != NULL) {
        free_consensus_context(ctx);
        ctx = NULL;
    }
    atomic_store(&in_use_udp_encheres, false);
    LOG_INFO("Fin du consensus.\n");
    return ret;
}

// int process_response(int sock_listen, peer_t *peer, consensus_context_t *ctx) {
//     struct timeval start, now;
//     gettimeofday(&start, NULL);

//     while (1) {

//         if (ctx->done) {
//             LOG_INFO("Le Consensus est terminé");
//             return 0;
//         }

//         // On vérif si on a dépassé CONSENSUS_TIMEOUT
//         gettimeofday(&now, NULL);
//         long elapsed_ms =
//             (now.tv_sec - start.tv_sec) * 1000L + (now.tv_usec - start.tv_usec) /
//             1000L;
//         if (elapsed_ms > CONSENSUS_TIMEOUT * 1000) {
//             // Timeout atteint sans terminer le consensus
//             return 0;
//         }

//         uint8_t buff[UDP_MSG_BUF_SIZE];
//         ssize_t len = recv_response(sock_listen, peer, ctx, buff, sizeof(buff));

//         if (len < 0) {
//             LOG_ERROR("recv_response");
//             return -1;
//         } else if (len == 0) {
//             // Rien reçu mais pas d’erreur, on attend encore
//             continue;
//         }

//         if (handle_response(buff, len, peer, ctx) < 0) {
//             LOG_ERROR("handle_response");
//             return -1;
//         }
//     }
// }

// ssize_t recv_response(int sock_listen, peer_t *peer, consensus_context_t *ctx,
//                       uint8_t *buff, ssize_t buff_len) {

//     if (ctx->done) {

//         return 0;
//     }

//     fd_set read_fds;
//     FD_ZERO(&read_fds);
//     FD_SET(sock_listen, &read_fds);

//     struct timeval timeout;
//     timeout.tv_sec = CONSENSUS_TIMEOUT;
//     timeout.tv_usec = 0;

//     int ready = select(sock_listen + 1, &read_fds, NULL, NULL, &timeout);
//     if (ready < 0) {
//         if (errno == EINTR) {
//             return -2;
//         }
//         LOG_ERROR("select process_response consensus");
//         return -1;
//     }
//     if (ready == 0) {
//         // Pas de réponse, timeout
//         return 0;
//     }

//     struct sockaddr_in6 src_addr;
//     memset(&src_addr, 0, sizeof(src_addr));
//     socklen_t addr_len = sizeof(src_addr);
//     ssize_t len =
//         recvfrom(sock_listen, buff, buff_len, 0, (struct sockaddr *)&src_addr,
//         &addr_len);
//     LOG_INFO("recvfrom %d", len);
//     if (len < 0) {
//         if (errno == EAGAIN || errno == EWOULDBLOCK) {
//             return -1;
//         }
//         LOG_ERROR("recvfrom UDP");
//         return -1;
//     }
//     return len;
// }

// int handle_response(uint8_t *buff, ssize_t len, peer_t *peer, consensus_context_t
// *ctx)
// {
//     int ret = 0;
//     message_t *msg = NULL;

//     msg = malloc(sizeof(message_t));
//     if (!msg) {
//         LOG_ERROR("malloc");
//         ret = -1;
//         goto cleanup;
//     }

//     if (msg_deserialize(buff, len, msg, false, 0) == DESERIALIZE_SUCCESS &&
//         msg->code == 1) {
//         int res = verify_validation(msg, peer, ctx);
//         if (res == 0) {
//             if (is_all_validation_collected(ctx) == true) {
//                 ctx->done = true;
//                 if (send_final_message(peer, ctx) < 0) {
//                     LOG_ERROR("send_final_message");
//                     ret = -1;
//                     goto cleanup;
//                 } else {
//                     ret = 0;
//                     goto cleanup;
//                 }
//             }
//         }
//     } else {
//         LOG_ERROR("deserialize recv responses consensus");
//         ret = 0;
//         goto cleanup;
//     }
// cleanup:
//     if (msg != NULL) {
//         message_free(msg);
//         msg = NULL;
//     }
//     return ret;
// }

// /**
//  * Vérifie si on a collecté toutes les validations
//  **/
// bool is_all_validation_collected(consensus_context_t *ctx) {
//     return ctx->remaining_count == 0;
// }

/**
 * Prépare le message final et multidiffuse le message
 * @return -1 erreur, 0 OK
 **/
int send_final_message(peer_t *peer, consensus_context_t *ctx) {
    message_t *final_msg = NULL;
    sign_t *self_sig = NULL;
    peer_signature_t *first_sigs = NULL;
    mess_t *mess = malloc(sizeof(mess_t));
    int ret = 0;
    int sock_send = -1;

    if (!mess) {
        LOG_ERROR("Erreur malloc mess");
        return -1;
    }

    // CHAMP LMESS ET MESS
    mess->data = ctx->msg;
    mess->length = ctx->lmess;

    // CHAMP LSIG ET SIG
    self_sig = malloc(sizeof(sign_t));

    // signature
    if (sign_message_with_key(peer->tls.pkey, mess, self_sig) != 0) {
        LOG_ERROR("Erreur lors de la signature du message de consensus");
        return -1;
    }
    LOG_INFO("Signature du message de consensus effectuée");

    // if (self_sig) {
    //     memset(self_sig, 0, sizeof(sign_t));
    // } else {
    //     LOG_ERROR("memset");
    //     ret = -1;
    //     goto cleanup;
    // }
    // self_sig->data = 0;
    // self_sig->length = 0;

    // prendre les 3 premieres signatures si plus de 3 pairs
    if (ctx->received_responses > 3) {
        if (take_first_signatures(ctx, 3, &first_sigs) < 0) {
            LOG_ERROR("take_first_signatures");
            ret = -1;
            goto cleanup;
        }
    } else { // sinon on les prend toutes
        if (take_first_signatures(ctx, ctx->received_responses, &first_sigs) < 0) {
            LOG_ERROR("take_first_signatures");
            ret = -1;
            goto cleanup;
        }
    }

    LOG_INFO("Création du message CODE_CONSENSUS...");
    final_msg =
        msg_create_consensus(peer, mess, self_sig, ctx->received_responses, first_sigs);
    if (!final_msg) {
        LOG_ERROR("Échec de création du message CODE_CONSENSUS");
        ret = -1;
        goto cleanup;
    }

    uint8_t msg_buf[UDP_MSG_BUF_SIZE];
    size_t msg_len;

    LOG_INFO("Sérialisation du message CODE_CONSENSUS");
    if (msg_serialize(final_msg, msg_buf, sizeof(msg_buf), &msg_len) !=
        SERIALIZE_SUCCESS) {
        LOG_INFO("Erreur sérialisation CODE_CONSENSUS.");
        ret = -1;
        goto cleanup;
    }

    sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

    // Adresse enchère
    struct sockaddr_in6 addr =
        make_addr(&peer->auctions_ip, peer->auctions_port, peer->interface_name);

    if (sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr)) <
        0) {
        LOG_ERROR("Échec envoi CODE_CONSENSUS");
        ret = -1;
        goto cleanup;
    }

    // // on prépare et envoie les CONSENSUS_PART
    // if (ctx->received_responses > 3) {
    //     if (send_consensus_part(peer, ctx, sock_send, addr) < 0) {
    //         LOG_ERROR("send_consensus_part");
    //         ret = -1;
    //         goto cleanup;
    //     }
    // }

    LOG_INFO("Message final multidiffusé");
    ret = 0;
    goto cleanup;

cleanup:
    if (final_msg != NULL) {
        message_free(final_msg);
        final_msg = NULL;
    }
    if (self_sig != NULL) {
        free(self_sig);
        self_sig = NULL;
    }
    if (mess != NULL) {
        free(mess);
        mess = NULL;
    }
    if (sock_send >= 0) {
        close(sock_send);
        sock_send = -1;
    }
    if (first_sigs != NULL) {
        free(first_sigs);
        first_sigs = NULL;
    }
    if (ctx != NULL) {
        free_consensus_context(ctx);
        ctx = NULL;
    }
    return ret;
}

int take_first_signatures(consensus_context_t *ctx, uint8_t responses,
                          peer_signature_t **sigs) {
    *sigs = malloc(sizeof(peer_signature_t) * responses);
    if (!*sigs) {
        LOG_ERROR("malloc");
        return -1;
    }
    for (size_t i = 0; i < responses; i++) {
        (*sigs)[i].id = ctx->sigs[i].id;
        (*sigs)[i].signature.data = ctx->sigs[i].signature.data;
        (*sigs)[i].signature.length = ctx->sigs[i].signature.length;
    }
    return 0;
}

// int take_next_signatures(consensus_context_t *ctx, uint8_t responses,
//                          size_t current_index, peer_signature_t **sigs) {
//     *sigs = malloc(sizeof(peer_signature_t) * responses);
//     if (!*sigs) {
//         LOG_ERROR("malloc");
//         return -1;
//     }
//     for (size_t j = 0; j < responses; j++) {
//         (*sigs)[j].id = ctx->sigs[current_index + j].id;
//         (*sigs)[j].signature.data = ctx->sigs[current_index + j].signature.data;
//         (*sigs)[j].signature.length = ctx->sigs[current_index +
//         j].signature.length;
//     }

//     return 0;
// }

// int send_consensus_part(peer_t *peer, consensus_context_t *ctx, int sock_send,
//                         struct sockaddr_in6 addr) {
//     LOG_INFO("Envoi des messages CODE_CONSENSUS_PART");
//     message_t *consensus_part = NULL;
//     peer_signature_t *next_sigs = NULL;
//     int ret = 0;

//     for (size_t i = 3; i < ctx->received_responses; i += 3) {
//         int remaining =
//             (ctx->received_responses - i >= 3) ? 3 : (ctx->received_responses - i);

//         if (take_next_signatures(ctx, remaining, i, &next_sigs) < 0) {
//             LOG_ERROR("take_next_signatures");
//             ret = -1;
//             goto cleanup;
//         }

//         LOG_INFO("Création du message CODE_CONSENSUS_PART...");
//         consensus_part = msg_create_consensus_part(peer, remaining, next_sigs);
//         if (!consensus_part) {
//             LOG_ERROR("Échec de création du message CODE_CONSENSUS_PART");
//             ret = -1;
//             goto cleanup;
//         }
//         uint8_t msg_buf_part[UDP_MSG_BUF_SIZE];
//         size_t msg_len_part;
//         if (msg_serialize(consensus_part, msg_buf_part, sizeof(msg_buf_part),
//                           &msg_len_part) != SERIALIZE_SUCCESS) {
//             LOG_INFO("Erreur sérialisation CODE_CONSENSUS_PART.");
//             ret = -1;
//             goto cleanup;
//         }
//         if (sendto(sock_send, msg_buf_part, msg_len_part, 0, (struct sockaddr
//         *)&addr,
//                    sizeof(addr)) < 0) {
//             LOG_ERROR("Échec envoi CODE_CONSENSUS_PART");
//             ret = -1;
//             goto cleanup;
//         }
//         message_free(consensus_part);
//         consensus_part = NULL;
//         LOG_INFO("Envoi CODE_CONSENSUS_PART (%zu octets)", msg_len_part);

//         free(next_sigs);
//         next_sigs = NULL;
//     }
// cleanup:
//     if (next_sigs != NULL) {
//         free(next_sigs);
//         next_sigs = NULL;
//     }
//     if (consensus_part != NULL) {
//         message_free(consensus_part);
//         consensus_part = NULL;
//     }
//     return ret;
// }

// int handle_code_consensus(peer_t *peer, uint8_t *mess, lmess_t lmess) {
//     int ret = 0;
//     message_t *msg = NULL;
//     message_t *inner_msg = NULL;

//     LOG_INFO("Traitement du message de consensus");

//     // for (size_t i = 0; i < lmess; i++) {
//     //     LOG_WARNING("%02X", mess[i]);
//     // }

//     msg = malloc(sizeof(message_t));
//     memset(msg, 0, sizeof(message_t));
//     if (!msg) {
//         LOG_ERROR("malloc");
//         ret = -1;
//         goto cleanup;
//     }

//     if (msg_deserialize(mess, lmess, msg, false, 0) != DESERIALIZE_SUCCESS) {
//         LOG_ERROR("deserialize");
//         ret = -1;
//         goto cleanup;
//     }

//     inner_msg = malloc(sizeof(message_t));
//     memset(inner_msg, 0, sizeof(message_t));
//     if (!inner_msg) {
//         LOG_ERROR("malloc");
//         ret = -1;
//         goto cleanup;
//     }

//     if (msg_deserialize(msg->content.consensus.message.data,
//                         msg->content.consensus.message.length, inner_msg, false,
//                         0) != DESERIALIZE_SUCCESS) {
//         LOG_WARNING("deserialize inner message");
//         ret = -1;
//         goto cleanup;
//     }
// if (verify_consensus_signatures(peer, inner_msg) != 0) {
//     LOG_ERROR("Consensus non valide, action ignorée");
//     return -1;
// }
//     switch (inner_msg->code) {
//         case 13: // CODE_SYSTEM_LEAVE
//             LOG_INFO("Message de consensus CODE_SYSTEM_LEAVE");
//             if (delete_peer_info(peer, inner_msg->content.leave.id) < 0) {
//                 LOG_ERROR("Échec de la suppression des infos");
//                 ret = -1;
//                 goto cleanup;
//             }
//             break;

//         case 18: // CODE_PEERS_REMOVE
//             LOG_INFO("Message de consensus CODE_PEERS_REMOVE");
//             if (delete_missing_peers(
//                     peer, inner_msg->content.peer_mgmt.afk.peers.peer_ids,
//                     inner_msg->content.peer_mgmt.afk.peers.peer_count) < 0) {
//                 LOG_ERROR("Échec de la suppression des pairs");
//                 ret = -1;
//                 goto cleanup;
//             }
//             break;

//         default:
//             LOG_WARNING("Validation d'un code de message non implémenté : %d",
//                         inner_msg->code);
//             break;
//     }

//     ret = 0;
// cleanup:
//     if (msg != NULL) {
//         message_free(msg);
//         msg = NULL;
//     }
//     if (inner_msg != NULL) {
//         message_free(inner_msg);
//         inner_msg = NULL;
//     }
//     return ret;
// }

// void send_validation(peer_t *peer, message_t *msg, code_t code) {
//     mess_t *mess = malloc(sizeof(mess_t));
//     if (!mess) {
//         LOG_ERROR("CODE %u non crée", code);
//         return;
//     }
//     int sock_send = -1;

//     uint8_t *inner_msg = malloc(UDP_MSG_BUF_SIZE);
//     if (!inner_msg) {
//         LOG_ERROR("Échec d'allocation");
//         free(mess);
//         mess = NULL;
//         return;
//     }

//     size_t inner_length;

//     LOG_INFO("Sérialisation du message interne");
//     if (msg_serialize(msg, inner_msg, UDP_MSG_BUF_SIZE, &inner_length) !=
//         SERIALIZE_SUCCESS) {
//         LOG_ERROR("Erreur serialisation message interne");
//         return;
//     }

//     mess->data = inner_msg;
//     mess->length = inner_length;

//     LOG_INFO("Préparation de la signature");
//     // IL FAUT SIGNER !!!
//     sign_t *self_sig = malloc(sizeof(sign_t));
//     if (self_sig) {
//         memset(self_sig, 0, sizeof(sign_t));
//     } else {
//         LOG_ERROR("Échec d'allocation");
//         return;
//     }
// if (sign_message_with_key(peer->tls.pkey, mess, self_sig) != 0) {
//     LOG_ERROR("Erreur lors de la signature du message de validation");
//     return;
// }
//     LOG_INFO("Création du message CODE_VALIDATION");
//     message_t *validation = msg_create_validation(peer->id, mess, self_sig);

//     if (!validation) {
//         LOG_ERROR("Échec de création du message CODE_VALIDATION");
//         return;
//     }

//     uint8_t msg_buf[UDP_MSG_BUF_SIZE];
//     size_t msg_len;

//     LOG_INFO("Sérialisation du message CODE_VALIDATION");
//     if (msg_serialize(validation, msg_buf, sizeof(msg_buf), &msg_len) !=
//         SERIALIZE_SUCCESS) {
//         LOG_INFO("Erreur sérialisation CODE_VALIDATION.");
//         return;
//     }

//     // LOG_INFO("Buffer à envoyer (%zu octets):", msg_len);
//     // for (size_t i = 0; i < msg_len && i < 32; i++) {
//     //     LOG_INFO("%02X ", msg_buf[i]);
//     // }

//     info_t *destination = peer_list_find(&peer->peer_list, msg->content.leave.id);

//     sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

//     struct sockaddr_in6 addr = make_addr(&destination->ip, destination->port,
//     NULL); size_t s =
//         sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr,
//         sizeof(addr));
//     char dest_ip[INET6_ADDRSTRLEN];
//     inet_ntop(AF_INET6, &destination->ip.addr, dest_ip, sizeof(dest_ip));
//     LOG_INFO("Envoi UDP vers %s:%d", dest_ip, destination->port);

//     close(sock_send);
//     sock_send = -1;

//     if (s < 0) {
//         LOG_ERROR("Échec de l'envoi UDP: %s", strerror(errno));
//         return;
//     } else {
//         LOG_INFO("Message UDP envoyé (%zd octets)", s);
//     }

//     LOG_INFO("Validation Envoyée : CODE:%u ID:%u LMESS:%u LSIG:%u",
//     validation->code,
//              validation->content.validation.id,
//              validation->content.validation.message.length,
//              validation->content.validation.signature.length);
// }

// int verify_nb_signatures(peer_t *peer, nb_t peer_count) {
//     if (peer_count > 3) {
//         LOG_INFO("Je dois recevoir un consensus part");

//         // RAJOUTER UN TIMEOUT

//         atomic_store(&in_use_udp_encheres, true);
//         int sock_listen = peer->sockets.sock_udp_encheres_recv;
//         uint8_t buff[UDP_MSG_BUF_SIZE];
//         ssize_t len = recv_consensus_part(sock_listen, buff, sizeof(buff));
//         if (len < 0) {
//             LOG_ERROR("recv_consensus_part");
//             return -1;
//         }

//         if (handle_consensus_part(buff, len, peer) < 0) {
//             LOG_ERROR("handle_consensus_part");
//             return -1;
//         }
//         atomic_store(&in_use_udp_encheres, false);
//     }
//     return 0;
// }

// ssize_t recv_consensus_part(int sock_listen, uint8_t *buff, ssize_t buff_len) {
//     struct sockaddr_in6 addr;
//     memset(&addr, 0, sizeof(addr));
//     socklen_t addr_len = sizeof(addr);
//     ssize_t len =
//         recvfrom(sock_listen, buff, sizeof(buff), 0, (struct sockaddr *)&addr,
//         &addr_len);
//     if (len < 0) {
//         if (errno == EAGAIN || errno == EWOULDBLOCK) {
//             return -1;
//         }
//         LOG_ERROR("recvfrom UDP");
//         return -1;
//     }
//     return len;
// }

// int handle_consensus_part(uint8_t *buff, ssize_t buff_len, peer_t *peer) {
//     int ret = 0;
//     message_t *msg = NULL;

//     msg = malloc(sizeof(message_t));
//     if (!msg) {
//         LOG_ERROR("malloc");
//         ret = -1;
//         goto cleanup;
//     }

//     if (msg_deserialize(buff, buff_len, msg, false, 0) != DESERIALIZE_SUCCESS) {
//         LOG_ERROR("deserialize recv responses consensus");
//         ret = -1;
//         goto cleanup;
//     }

//     if (msg->code != 20) {
//         LOG_ERROR("Code de message incorrect");
//         ret = -1;
//         goto cleanup;
//     }

//     // Vérification des signatures
// if (verify_consensus_signatures(peer, msg) != 0) {
//     LOG_ERROR("Consensus non valide, action ignorée");
//     return -1;
// }
// cleanup:
//     if (msg != NULL) {
//         message_free(msg);
//         msg = NULL;
//     }
//     return ret;
// }

// int wait_for_answer(peer_t *peer, message_t *msg) {
//     atomic_store(&in_use_udp_encheres, true);

//     int ret = 0;

//     struct timeval start, now;
//     gettimeofday(&start, NULL);

//     uint8_t buff[UDP_MSG_BUF_SIZE];
//     struct sockaddr_in6 src_addr;
//     memset(&src_addr, 0, sizeof(src_addr));
//     socklen_t addr_len = sizeof(src_addr);

//     while (1) {

//         // On redemande un code 2 à intervalle régulier
//         gettimeofday(&now, NULL);
//         long elapsed_ms =
//             (now.tv_sec - start.tv_sec) * 1000L + (now.tv_usec - start.tv_usec) /
//             1000L;
//         if (elapsed_ms >
//             7 * CONSENSUS_TIMEOUT * 1000) { // 7 FOIS CAR ON Q PEUT FAIRE DES
//             RELANCES
//             // X3 = Q qui relance
//             // X4 = P qui relance
//             // Timeout atteint sans recevoir le code 2
//             // On considère que Q a disparu
//             LOG_WARNING("Consensus échoué, Q a disparu");
//             ret = -2;
//             goto cleanup;
//         }

//         peer_lock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);
//         ssize_t len =
//             recv_with_timeout(peer->sockets.sock_udp_encheres_recv, buff,
//             sizeof(buff), 0,
//                               (struct sockaddr *)&src_addr, &addr_len);
//         peer_unlock(peer, PEER_MUTEX_UDP_ENCHERES_RECV);

//         if (len < 0) {
//             if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                 ret = -1;
//                 goto cleanup;
//             }
//             LOG_ERROR("recvfrom UDP");
//             ret = -1;
//             goto cleanup;
//         }

//         if (len == 0) {
//             // Rien reçu mais pas d’erreur, on attend encore
//             // On relance le superviseur
//             if (elapsed_ms > 4 * CONSENSUS_TIMEOUT * 1000) {
//                 relaunch_supervisor(peer, msg);
//             }
//             continue;
//         }

//         msg = malloc(sizeof(message_t));
//         if (!msg) {
//             LOG_ERROR("Erreur malloc message");
//             ret = -1;
//             goto cleanup;
//         }

//         if (msg_deserialize(buff, len, msg, false, 0) != DESERIALIZE_SUCCESS) {
//             LOG_WARNING("Message invalide reçu sur l'adresse enchères.");
//             ret = 0;
//             goto cleanup;
//         }

//         switch (msg->code) {
//             case CODE_CONSENSUS:
//                 LOG_INFO("J'ai reçu le message final, vérification de signatures");
//                 // Vérifier les signatures

//                 verify_nb_signatures(peer, msg->content.consensus.peer_count);
//                 LOG_INFO("Consensus validé");
//                 handle_code_consensus(peer, buff, len);
//                 ret = 0;
//                 goto cleanup;
//             case CODE_CANCEL_TRANSACTION:
//                 LOG_INFO("Annulation de la transaction");
//                 ret = 0;
//                 goto cleanup;
//             default:
//                 LOG_WARNING("Message Ignoré : CODE %u", msg->code);
//                 break;
//         }
//     }

//     LOG_ERROR("Consensus échoué");
//     ret = -1;

// cleanup:
//     atomic_store(&in_use_udp_encheres, false);
//     return ret;
// }

// int recv_with_timeout(int sock_listen, uint8_t *buff, ssize_t buff_len, int flags,
//                       struct sockaddr *src_addr, socklen_t *addr_len) {
//     fd_set read_fds;
//     FD_ZERO(&read_fds);
//     FD_SET(sock_listen, &read_fds);

//     struct timeval timeout;
//     timeout.tv_sec = CONSENSUS_TIMEOUT;
//     timeout.tv_usec = 0;

//     int ready = select(sock_listen + 1, &read_fds, NULL, NULL, &timeout);
//     if (ready < 0) {
//         if (errno == EINTR) {
//             return -2;
//         }
//         LOG_ERROR("select process_response consensus");
//         return -1;
//     }
//     if (ready == 0) {
//         // Pas de réponse, timeout
//         return 0;
//     }

//     ssize_t len = recvfrom(sock_listen, buff, buff_len, flags, src_addr, addr_len);
//     if (len < 0) {
//         if (errno == EAGAIN || errno == EWOULDBLOCK) {
//             return -1;
//         }
//         LOG_ERROR("recvfrom UDP");
//         return -1;
//     }
//     return len;
// }

// int relaunch_supervisor(peer_t *peer, message_t *msg) {
//     uint8_t msg_buf[UDP_MSG_BUF_SIZE];
//     size_t msg_len;

//     if (msg_serialize(msg, msg_buf, sizeof(msg_buf), &msg_len) !=
//     SERIALIZE_SUCCESS) {
//         LOG_ERROR("Erreur sérialisation CODE_VALIDATION.");
//         return -1;
//     }

//     int sock_send = -1;
//     info_t *destination = peer_list_find(&peer->peer_list, msg->content.leave.id);

//     sock_send = udp_bind_unicast(&peer->ip, peer->port, peer->interface_name);

//     struct sockaddr_in6 addr = make_addr(&destination->ip, destination->port,
//     NULL); size_t s =
//         sendto(sock_send, msg_buf, msg_len, 0, (struct sockaddr *)&addr,
//         sizeof(addr));

//     if (s < 0) {
//         LOG_ERROR("Échec de l'envoi UDP: %s", strerror(errno));
//         close(sock_send);
//         sock_send = -1;
//         return -1;
//     }

//     LOG_WARNING("Superviseur relancé");

//     close(sock_send);
//     sock_send = -1;

//     return 0;
// }

/**
 * Méthode permettant de vérifier les signatures d'un consensus
 */
int verify_consensus_signatures(peer_t *peer, message_t *consensus_msg) {
    // Rechercher le superviseur dans la liste du paire
    info_t *supervisor =
        peer_list_find(&peer->peer_list, consensus_msg->content.consensus.id);
    if (!supervisor) {
        LOG_ERROR("Superviseur inconnu (ID=%u)", consensus_msg->content.consensus.id);
        return 1;
    }
    // Vérifier sa signature
    if (!message_verify_signature(consensus_msg, &supervisor->key)) {
        LOG_ERROR("Signature du superviseur invalide");
        return 1;
    }

    nb_t nb_signatures = consensus_msg->content.consensus.peer_count;
    for (nb_t i = 0; i < nb_signatures; i++) {
        app_id_t id = consensus_msg->content.consensus.peer_signatures[i].id;
        sign_t *sig = &consensus_msg->content.consensus.peer_signatures[i].signature;

        info_t *pinfo = peer_list_find(&peer->peer_list, id);
        if (pinfo == NULL) {
            LOG_ERROR("ID pair inconnu (ID=%u)", id);
            return 1;
        }
        message_t tmp;
        memset(&tmp, 0, sizeof(message_t));
        tmp.code = CODE_VALIDATION;
        tmp.content.validation.id = id;
        tmp.content.validation.message.data =
            consensus_msg->content.consensus.message.data;
        tmp.content.validation.message.length =
            consensus_msg->content.consensus.message.length;
        tmp.content.validation.signature.data = sig->data;
        tmp.content.validation.signature.length = sig->length;

        if (!message_verify_signature(&tmp, &pinfo->key)) {
            LOG_ERROR("Signature invalide pour le pair ID=%u", id);
            return 1;
        }
    }
    return 0;
}