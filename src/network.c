#include "../include/network.h"
#include "../include/debug.h"
#include "../include/safe_output.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int get_local_ip(ip_t *out_ip, char **interface_name) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        LOG_ERROR("getifaddrs");
        return -1;
    }

    // Première phase : chercher une IPv6 globale
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET6)
            continue;

        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;

        if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr))
            continue;
        if (IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr))
            continue;
        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr))
            continue; // sauter fe80::

        memcpy(out_ip->addr, &addr6->sin6_addr, sizeof(out_ip->addr));

        *interface_name = strdup(ifa->ifa_name);

        freeifaddrs(ifaddr);
        return 0;
    }

    // Deuxième phase : accepter une link-local en dernier recours
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET6)
            continue;

        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;

        if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr))
            continue;
        if (IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr))
            continue;

        // C'est une link-local
        memcpy(out_ip->addr, &addr6->sin6_addr, sizeof(out_ip->addr));

        *interface_name = strdup(ifa->ifa_name);

        freeifaddrs(ifaddr);
        return 0;
    }

    freeifaddrs(ifaddr);
    LOG_ERROR("Aucune IP locale valide trouvée");
    return -1;
}

port_t get_available_port(port_t min, port_t max) {

    int max_attemps = 10;
    int attemps = 0;

    while (attemps++ < max_attemps) {
        port_t port = min + rand() % (max - min + 1);
        int sock = socket(PF_INET6, SOCK_DGRAM, 0);
        if (sock < 0) {
            LOG_ERROR("get_available_port: socket");
            continue;
        }
        struct sockaddr_in6 addr = {
            .sin6_family = AF_INET6, .sin6_addr = in6addr_any, .sin6_port = htons(port)};

        int ok = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok));

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(sock);
            return port;
        }
        close(sock);
    }

    for (port_t port = min; port <= max; port++) {
        int sock = socket(PF_INET6, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("get_available_port: socket");
            continue;
        }
        struct sockaddr_in6 addr = {
            .sin6_family = AF_INET6, .sin6_addr = in6addr_any, .sin6_port = htons(port)};

        int ok = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok));

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(sock);
            return port;
        }
        close(sock);
    }

    LOG_ERROR("Pas de port dispo");
    return -1;
}

/*
 * Renvoie l'adresse IP dans un char*
 * Ne pas oublier de libérer la mémoire après utilisation
 * WARNING : Ne pas utiliser si pour stocker dans peer_t
 */
char *ip_to_string(const ip_t *ip) {
    char *str = malloc(INET6_ADDRSTRLEN);
    if (str)
        inet_ntop(AF_INET6, ip->addr, str, INET6_ADDRSTRLEN);
    return str;
}

/*
 * WARNING 1 : le contenu est réécrit à chaque appel, pas stocker.
 * A utliser directement dans une fonction ou copier
 * WARNING 2 : Ne pas utiliser si pour stocker dans peer_t
 * !! Utilisation que pour affichage !!
 */
char *ip_to_str_static(const ip_t *ip) {
    static char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, ip->addr, str, sizeof(str));
    return str;
}

/*
 * WARNING 1 : le contenu est réécrit à chaque appel, pas stocker.
 * A utliser directement dans une fonction ou copier
 * WARNING 2 : Ne pas utiliser si pour stocker dans peer_t
 * !! Utilisation que pour affichage !!
 */
const char *in6_addr_to_str_static(const struct in6_addr *addr) {
    static char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, addr, str, sizeof(str));
    return str;
}

void generate_multicast_address(ip_t *ip) {
    // Prefixe standard de multidiffusion locale
    // ff12::[valeur aléatoire]
    ip->addr[0] = 0xff;
    ip->addr[1] = 0x12;

    // Le reste peut être pseudo-aléatoire
    for (int i = 2; i < 16; ++i) {
        ip->addr[i] = (uint8_t)(rand() % 256);
    }
}

/**
 * Envoie un message TCP en s'assurant que tout le message est envoyé
 * @param tv : si spécifiée, la valeur est utilisée pour le timemout sinon mettre NULL
 */
ssize_t tcp_send_all(int sock, uint8_t *buf, size_t len, const struct timeval *tv) {
    size_t sent = 0;
    while (sent < len) {

        struct timeval timeout;
        if (tv) {
            timeout = *tv;
        } else {
            timeout.tv_sec = TCP_SEND_ALL_TIMEOUT;
            timeout.tv_usec = 0;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);

        int r = select(sock + 1, NULL, &wfds, NULL, &timeout);
        if (r < 0) {
            LOG_ERROR("tcp_send_all : select send");
            return -1;

        } else if (r == 0) {
            LOG_ERROR("tcp_send_all : timeout");
            return -1;
        }

        ssize_t n = send(sock, buf + sent, len - sent, 0);
        if (n < 0) {
            LOG_ERROR("tcp_send_all : send");
            return -1;

        } else if (n == 0) {
            LOG_ERROR("tcp_send_all : send 0 — socket inutilisable ?");
            return -1; // ou break + gestion spéciale
        }
        sent += n;
    }
    return sent;
}

/**
 * Reçoit un message TCP en s'assurant que tout le message est reçu
 * @param tv : si spécifiée, la valeur est utilisée pour le timemout sinon mettre NULL
 */
ssize_t tcp_recv_all(int sock, uint8_t *buf, size_t len, const struct timeval *tv) {
    size_t received = 0;
    while (1) {

        struct timeval timeout;
        if (tv) {
            timeout = *tv;
        } else {
            timeout.tv_sec = TCP_RECV_ALL_TIMEOUT;
            timeout.tv_usec = 0;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        int r = select(sock + 1, &rfds, NULL, NULL, &timeout);
        if (r < 0) {
            LOG_ERROR("tcp_recv_all : select recv");
            return -1;

        } else if (r == 0) {
            LOG_ERROR("tcp_recv_all : timeout");
            return -1;
        }

        ssize_t n = recv(sock, buf + received, len - received, 0);
        if (n < 0) {
            LOG_ERROR("tcp_recv_all : recv");
            return -1;

        } else if (n == 0) {
            // Fermeture propre
            break;
        }
        received += n;
        if (received == len) {
            // tampon plein, on arrête pour éviter overflow
            break;
        }
    }
    return received;
}

int tcp_accept_with_timeout(int listen_sock, struct timeval *timeout) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_sock, &rfds);

    struct timeval local_timeout;
    if (timeout) {
        local_timeout = *timeout;
    } else {
        local_timeout.tv_sec = TCP_ACCPT_TIMEOUT;
        local_timeout.tv_usec = 0;
    }

    int ready = select(listen_sock + 1, &rfds, NULL, NULL, &local_timeout);
    if (ready < 0) {
        LOG_ERROR("tcp_accept_with_timeout : select");
        return -1;
    }

    if (ready == 0) {
        LOG_WARNING("tcp_accept_with_timeout : timeout");
        return -1;
    }

    struct sockaddr_in6 client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sock < 0) {
        LOG_ERROR("tcp_accept_with_timeout : accept");
        return -1;
    }

    LOG_INFO("Connexion acceptée de %s:%d",
             in6_addr_to_str_static(&client_addr.sin6_addr),
             ntohs(client_addr.sin6_port));

    return client_sock;
}
