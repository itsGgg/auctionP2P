#ifndef NETWORK_SOCKETS_H
#define NETWORK_SOCKETS_H

#include "../lib/format.h"

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15 // grep SO_REUSEPORT /usr/include/* -r
#endif

typedef struct network_sockets_t network_sockets_t;
struct network_sockets_t {
    int sock_udp_liaison_recv;
    int sock_tcp_perso_listen;
    int sock_udp_perso_recv;
    int sock_udp_encheres_recv;
};

struct sockaddr_in6 make_addr(const ip_t *ip, port_t port, const char *interface_name);

int tcp_sock();
int tcp_connect_sock(const ip_t *ip, port_t port, const char *interface_name);
int tcp_bind_listen(const ip_t *ip, port_t port, const char *interface_name);

int udp_sock();
int udp_bind_unicast(const ip_t *ip, port_t port, const char *interface_name);
int udp_bind_multicast(const char *mcast_addr, port_t port, const char *interface_name);

int network_sockets_init(network_sockets_t *ns, const ip_t *perso_ip, port_t perso_port,
                         const char *interface_name);
int network_sockets_setup_encheres(network_sockets_t *ns, const ip_t *mip, port_t mport,
                                   const char *interface_name);
void network_sockets_close(network_sockets_t *ns);

void print_network_sockets(const network_sockets_t *ns);

#endif
