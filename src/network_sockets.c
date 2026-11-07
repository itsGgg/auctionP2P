#include "../include/network_sockets.h"
#include "../include/debug.h"
#include "../include/utils.h"
#include "../lib/config.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * @param ip Si NULL, on utilise in6addr_any
 * @param interface_name Si non NULL, on utilise l'interface spécifiée
 */
struct sockaddr_in6 make_addr(const ip_t *ip, port_t port, const char *interface_name) {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    if (ip == NULL) {
        addr.sin6_addr = in6addr_any;
    } else {
        memcpy(&addr.sin6_addr, ip->addr, sizeof(ip->addr));
    }

    if (interface_name != NULL) {
        addr.sin6_scope_id = if_nametoindex(interface_name);
    }

    return addr;
}

int tcp_sock() {
    int sock = socket(PF_INET6, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("socket TCP");
        return -1;
    }

    int ok = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &ok, sizeof(ok)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEPORT");
        close(sock);
        return -1;
    }

    return sock;
}

int tcp_connect_sock(const ip_t *ip, port_t port, const char *interface_name) {
    int sock = tcp_sock();
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in6 addr = make_addr(ip, port, interface_name);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("connect");
        close(sock);
        return -1;
    }

    return sock;
}

int tcp_bind_listen(const ip_t *ip, port_t port, const char *interface_name) {
    int sock = tcp_sock();
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in6 addr = make_addr(ip, port, interface_name);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind");
        close(sock);
        return -1;
    }

    if (listen(sock, MAX_PEERS) < 0) {
        LOG_ERROR("listen");
        close(sock);
        return -1;
    }

    return sock;
}

int udp_sock() {
    int sock = socket(PF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERROR("socket UDP");
        return -1;
    }

    int ok = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &ok, sizeof(ok)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEPORT");
        close(sock);
        return -1;
    }

    return sock;
}

int udp_bind_unicast(const ip_t *ip, port_t port, const char *interface_name) {
    int sock = udp_sock();
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in6 addr = make_addr(ip, port, interface_name);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind");
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

int udp_bind_multicast(const char *mcast_addr, port_t port, const char *interface_name) {
    int sock = udp_bind_unicast(NULL, port, interface_name);
    if (sock < 0) {
        return -1;
    }

    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));
    inet_pton(AF_INET6, mcast_addr, &group.ipv6mr_multiaddr);
    if (interface_name != NULL) {
        group.ipv6mr_interface = if_nametoindex(interface_name);
        if (group.ipv6mr_interface == 0) {
            LOG_ERROR("if_nametoindex");
            close(sock);
            return -1;
        }
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        LOG_ERROR("join group");
        close(sock);
        return -1;
    }

    return sock;
}

int network_sockets_init(network_sockets_t *ns, const ip_t *perso_ip, port_t perso_port,
                         const char *interface_name) {
    if (!ns || !perso_ip)
        return -1;

    memset(ns, 0, sizeof(network_sockets_t));

    ns->sock_udp_liaison_recv = udp_bind_multicast(
        LIAISON_MULTICAST_ADDR, LIAISON_MULTICAST_PORT, interface_name);
    ns->sock_udp_perso_recv = udp_bind_unicast(NULL, perso_port, interface_name);
    ns->sock_tcp_perso_listen = tcp_bind_listen(NULL, perso_port, interface_name);

    if (ns->sock_udp_liaison_recv < 0 || ns->sock_udp_perso_recv < 0 ||
        ns->sock_tcp_perso_listen < 0) {
        LOG_ERROR("Échec de l'initialisation d'au moins un socket réseau.");
        network_sockets_close(ns);
        return -1;
    }

    LOG_INFO("Sockets réseau initialisés avec succès.");
    return 0;
}

int network_sockets_setup_encheres(network_sockets_t *ns, const ip_t *mip, port_t mport,
                                   const char *interface_name) {
    if (!ns || !mip)
        return -1;

    // Fermer d'abord si déjà ouverts
    if (ns->sock_udp_encheres_recv > 0) {
        close(ns->sock_udp_encheres_recv);
    }

    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, mip->addr, ip_str, sizeof(ip_str));

    ns->sock_udp_encheres_recv = udp_bind_multicast(ip_str, mport, interface_name);

    if (ns->sock_udp_encheres_recv < 0) {
        LOG_ERROR("Échec de l'initialisation des sockets enchères");
        return -1;
    }

    LOG_INFO("Sockets enchères initialisés avec succès.");
    return 0;
}

void network_sockets_close(network_sockets_t *ns) {
    if (!ns)
        return;

    int *fds[] = {&ns->sock_udp_liaison_recv, &ns->sock_udp_encheres_recv,
                  &ns->sock_udp_perso_recv, &ns->sock_tcp_perso_listen};

    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); ++i) {
        if (*fds[i] > 0) {
            close(*fds[i]);
            *fds[i] = -1;
        }
    }
}

void print_network_sockets(const network_sockets_t *ns) {
    struct sockaddr_in6 addr;
    socklen_t len = sizeof(addr);

    if (getsockname(ns->sock_udp_liaison_recv, (struct sockaddr *)&addr, &len) == 0) {
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr.sin6_addr, ipstr, sizeof(ipstr));
        LOG_INFO("Socket LIAISON liée sur [%s]:%d", ipstr, ntohs(addr.sin6_port));
    }

    if (getsockname(ns->sock_tcp_perso_listen, (struct sockaddr *)&addr, &len) == 0) {
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr.sin6_addr, ipstr, sizeof(ipstr));
        LOG_INFO("Socket TCP PERSO liée sur [%s]:%d", ipstr, ntohs(addr.sin6_port));
    }

    if (getsockname(ns->sock_udp_perso_recv, (struct sockaddr *)&addr, &len) == 0) {
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr.sin6_addr, ipstr, sizeof(ipstr));
        LOG_INFO("Socket UDP PERSO liée sur [%s]:%d", ipstr, ntohs(addr.sin6_port));
    }

    if (getsockname(ns->sock_udp_encheres_recv, (struct sockaddr *)&addr, &len) == 0) {
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr.sin6_addr, ipstr, sizeof(ipstr));
        LOG_INFO("Socket ENCHERES liée sur [%s]:%d\n", ipstr, ntohs(addr.sin6_port));
    }
}
