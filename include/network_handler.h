#ifndef NETWORK_HANDLER_H
#define NETWORK_HANDLER_H

#include "../include/network.h"
#include "../include/peer.h"

/**
 * Ces variables servent à ne pas déclencher un nouveau thread par le event handler tant
 * que le nouveau thread créé pour le lire, ne l'a pas encore fait.
 */
extern atomic_bool in_use_liaison;
extern atomic_bool in_use_perso_tcp;
extern atomic_bool in_use_udp_perso;
extern atomic_bool in_use_udp_encheres;

extern atomic_bool close_supervising_auction;

// Boucle principale d’événements réseau pour un pair
void network_event_loop(peer_t *peer);

// Gestion des codes de messages reçus
void handle_liaison_codes(peer_t *peer, message_t *msg, struct sockaddr_in6 src_addr);
void handle_perso_tcp_codes(peer_t *peer, message_t *msg, int client_sock,
                            SSL *tls_socket, uint8_t *buffer);
void handle_perso_udp_codes(peer_t *peer, message_t *msg);
void handle_encheres_codes(peer_t *peer, message_t *msg);

#endif
