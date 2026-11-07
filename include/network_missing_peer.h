#ifndef NETWORK_MISSING_PEER_H
#define NETWORK_MISSING_PEER_H

#include "../include/network.h"

int handle_missing_peer(peer_t *peer, nb_t count, app_id_t *ids);
int delete_missing_peers(peer_t *peer, app_id_t *ids, nb_t count);
void handle_peers_remove(peer_t *peer, message_t *msg);
int handle_missing_supervisor(peer_t *peer, info_t supervisor);

#endif