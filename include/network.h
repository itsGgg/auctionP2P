#ifndef NETWORK_H
#define NETWORK_H

#include "../include/message.h"
#include "../include/peer.h"

#define TCP_MSG_BUF_SIZE 1024
#define UDP_MSG_BUF_SIZE 1024

#define TCP_SEND_ALL_TIMEOUT 3
#define TCP_RECV_ALL_TIMEOUT 3

#define TCP_ACCPT_TIMEOUT 3

// Fonctions utilitaires/primaires
int get_local_ip(ip_t *ip, char **interface_name);
port_t get_available_port(port_t min, port_t max);
char *ip_to_string(const ip_t *ip);
char *ip_to_str_static(const ip_t *ip);
const char *in6_addr_to_str_static(const struct in6_addr *addr);
void generate_multicast_address(ip_t *ip);

ssize_t tcp_send_all(int sock, uint8_t *buf, size_t len, const struct timeval *timeout);
ssize_t tcp_recv_all(int sock, uint8_t *buf, size_t len, const struct timeval *timeout);

int tcp_accept_with_timeout(int listen_sock, struct timeval *timeout);
#endif