#ifndef NETWORK_JOIN_H
#define NETWORK_JOIN_H

#include "../include/message.h"
#include "../include/peer.h"

/**
 * Phase 0 : Paramétrage
 */
int network_init(peer_t *peer);

/**
 * Phase 1 : Découverte des pairs existants sur le réseau
 * @return 0 si aucun pair trouvé, 1 si au moins un pair trouvé, -1 en cas d'erreur
 */
int network_discover_peers(peer_t *peer);

/**
 * Phase 2 : Création du système
 */
int network_create_system(peer_t *peer);

/**
 * Phase 2 ALT : Connexion à un pair existant
 */
int network_join_existing(peer_t *peer);

//////////////////////////////////////////////////////////////////////////////////////
// ---------- Fonctions gestion receveur du code provenant de event loop ---------- //
//////////////////////////////////////////////////////////////////////////////////////

void handle_join_request(peer_t *peer, message_t *msg, struct sockaddr_in6 src_addr);
void handle_peer_info(peer_t *peer, message_t *msg, int sock, SSL *tls_sock);
void handle_new_peer_info(peer_t *peer, message_t *msg);

#endif