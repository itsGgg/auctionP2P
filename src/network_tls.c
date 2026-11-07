#include "../include/network_tls.h"
#include "../include/debug.h"
#include "../include/network.h"
#include "../include/peer.h"
#include <fcntl.h>

#include <stdint.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
/**
 * Convertit une connexion TCP en connexion TLS (côté client)
 * @param peer le pair contenant le contexte SSL
 * @param sock le socket TCP connecté
 */
int upgrade_to_tls_client(peer_t *peer, int sock, SSL **tls_socket) {
    if (peer == NULL || peer->tls.ssl_ctx_client == NULL || sock < 0) {
        LOG_ERROR("Paramètres invalides pour TLS client");
        return -1;
    }

    SSL *ssl = SSL_new(peer->tls.ssl_ctx_client);
    if (ssl == NULL) {
        LOG_ERROR("Échec création SSL object");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if (SSL_set_fd(ssl, sock) != 1) {
        LOG_ERROR("Échec association socket SSL");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return -1;
    }

    if (SSL_connect(ssl) != 1) {
        LOG_ERROR("Echec acceptation TLS");
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        LOG_ERROR("Erreur TLS: %s", err_buf);

        // Vérifier l'état de la socket
        int sock_fd = SSL_get_fd(ssl);
        int sock_error = 0;
        socklen_t sock_error_len = sizeof(sock_error);
        getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &sock_error, &sock_error_len);
        LOG_ERROR("Erreur socket: %s", strerror(sock_error));

        ERR_print_errors_fp(stderr);
        if (ssl != NULL) {
            SSL_free(ssl);
        }
        return -1;
    }

    *tls_socket = ssl;
    LOG_INFO("Connexion TLS : succès");
    return 0;
}

/**
 * Convertit une connexion TCP en connexion TLS (côté serveur)
 * @param peer le pair contenant le contexte SSL
 * @param sock le socket TCP accepté
 */
int upgrade_to_tls_server(peer_t *peer, int sock, SSL **tls_socket) {
    if (peer == NULL || peer->tls.ssl_ctx_server == NULL || sock < 0) {
        LOG_ERROR("Paramètres invalides pour TLS serveur");
        return -1;
    }
    SSL *ssl = SSL_new(peer->tls.ssl_ctx_server);
    if (ssl == NULL) {
        LOG_ERROR("Échec création SSL object");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if (SSL_set_fd(ssl, sock) != 1) {
        LOG_ERROR("Échec association socket SSL");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return -1;
    }

    int ret = SSL_accept(ssl);
    if (ret != 1) {
        unsigned long ssl_err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));

        if (strstr(err_buf, "unexpected eof") != NULL) {
            LOG_INFO("TLS serveur : connexion déjà établie avec un autre pair");
            SSL_free(ssl);
            return -2;
        }

        LOG_ERROR("Echec acceptation TLS");
        ERR_print_errors_fp(stderr);

        if (ssl != NULL) {
            SSL_free(ssl);
        }
        return -1;
    }

    *tls_socket = ssl;
    LOG_INFO("Connexion TLS acceptée");

    return 0;
}

/**
 * Envoie des données via TLS
 * @param ssl structure SSL
 * @param buf tampon contenant les données à envoyer
 * @param len longueur des données
 * @return
 */
ssize_t tls_send_all(SSL *ssl, const uint8_t *buf, size_t len, const struct timeval *tv) {
    size_t sent = 0;
    int sock = SSL_get_fd(ssl);
    // Rendre la socket non bloquante
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while (sent < len) {

        struct timeval timeout;
        if (tv) {
            timeout = *tv;
        } else {
            timeout.tv_sec = TCP_SEND_ALL_TIMEOUT;
            timeout.tv_usec = 0;
        }

        ssize_t n = SSL_write(ssl, buf + sent, len - sent);
        int ssl_err = SSL_get_error(ssl, n);
        // Données envoyées avec succès
        if (n > 0) {
            sent += n;
            // Attendre que la socket soit lisible
        } else if (ssl_err == SSL_ERROR_WANT_READ) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            select(sock + 1, &rfds, NULL, NULL, &timeout);
            continue;
            // Attendre que la socket soit écrivable
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            select(sock + 1, &wfds, NULL, NULL, &timeout);
            continue;
        } else {
            // Erreurs
            char error_buffer[256];
            ERR_error_string_n(ssl_err, error_buffer, sizeof(error_buffer));
            LOG_ERROR("tls_send_all : SSL_write error: %s", error_buffer);
            LOG_ERROR("SSL error code: %d", ssl_err);
            return -1;
        }
    }
    return sent;
}
/**
 * Reçoit des données via TLS
 * @param ssl structure SSL
 * @param buf tampon de stockage
 * @param len taille maximale du tampon
 * @return
 */
ssize_t tls_recv_all(SSL *ssl, uint8_t *buf, size_t len, const struct timeval *tv) {
    size_t received = 0;
    int sock = SSL_get_fd(ssl);
    // Rendre la socket non bloquante
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while (received < len) {

        struct timeval timeout;
        if (tv) {
            timeout = *tv;
        } else {
            timeout.tv_sec = TCP_RECV_ALL_TIMEOUT;
            timeout.tv_usec = 0;
        }

        ssize_t n = SSL_read(ssl, buf + received, len - received);
        int ssl_err = SSL_get_error(ssl, n);

        if (n > 0) {
            // Données reçues avec succès
            received += n;
        } else if (ssl_err == SSL_ERROR_WANT_READ) {
            // Attendre que le socket soit lisible
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            select(sock + 1, &rfds, NULL, NULL, &timeout);
            continue;
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            // Attendre que le socket soit écrivable
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            select(sock + 1, NULL, &wfds, NULL, &timeout);
            continue;
        } else if (n == 0) {
            // Connexion fermée
            break;
        } else {
            // Erreurs
            char error_buffer[256];
            ERR_error_string_n(ssl_err, error_buffer, sizeof(error_buffer));
            LOG_ERROR("tls_send_all : SSL_write error: %s", error_buffer);
            LOG_ERROR("SSL error code: %d", ssl_err);
            return -1;
        }
    }
    return received;
}
/**
 * Ferme proprement une connexion TLS
 * @param ssl Structure SSL à fermer
 * @warning close le socket associé
 */
void tls_close(SSL *ssl) {

    if (ssl == NULL)
        return;

    int fd = SSL_get_fd(ssl);
    if (fd < 0) {
        SSL_free(ssl);
        return;
    }

    // Rendre la socket non bloquante
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct timeval tv;
    tv.tv_sec = 2; //
    tv.tv_usec = 0;

    // Premier appel à SSL_shutdown
    int ret = SSL_shutdown(ssl);
    // LOG_INFO("Premier SSL_shutdown retourne: %d", ret);

    if (ret == 0) {
        // Boucle pour gérer les erreurs WANT_READ/WANT_WRITE
        int max_attempts = 5;
        int attempts = 0;

        while (attempts++ < max_attempts) {
            ret = SSL_shutdown(ssl);
            // LOG_INFO("Tentative %d de SSL_shutdown retourne: %d", attempts, ret);

            if (ret > 0) {
                // Fermeture réussie
                break;
            } else if (ret < 0) {
                int ssl_err = SSL_get_error(ssl, ret);

                if (ssl_err == SSL_ERROR_WANT_READ) {
                    // Attendre que la socket soit lisible
                    fd_set rfds;
                    FD_ZERO(&rfds);
                    FD_SET(fd, &rfds);
                    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
                        LOG_WARNING("Timeout en attente de lecture pendant SSL_shutdown");
                        break;
                    }
                    continue;
                } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
                    // Attendre que la socket soit écrivable
                    fd_set wfds;
                    FD_ZERO(&wfds);
                    FD_SET(fd, &wfds);
                    if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
                        LOG_WARNING("Timeout en attente d'écriture pendant SSL_shutdown");
                        break;
                    }
                    continue;
                } else {
                    // Erreur
                    LOG_WARNING("SSL_shutdown a échoué avec l'erreur: %d", ssl_err);
                    break;
                }
            }
        }
    }

    SSL_free(ssl);

    shutdown(fd, SHUT_RDWR);
    close(fd);
}

void tls_config_free(tls_config_t *tls_config) {
    if (tls_config == NULL)
        return;

    if (tls_config->pkey != NULL) {
        EVP_PKEY_free(tls_config->pkey);
    }
    if (tls_config->certificate != NULL) {
        X509_free(tls_config->certificate);
    }
    if (tls_config->ssl_ctx_client != NULL) {
        SSL_CTX_free(tls_config->ssl_ctx_client);
    }
    if (tls_config->ssl_ctx_server != NULL) {
        SSL_CTX_free(tls_config->ssl_ctx_server);
    }
}