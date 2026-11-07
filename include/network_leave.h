#ifndef NETWORK_LEAVE_H
#define NETWORK_LEAVE_H

#include "../include/message.h"
#include "../include/peer.h"

int send_system_leave(peer_t *peer);
int delete_peer_info(peer_t *peer, app_id_t id);
void handle_system_leave(peer_t *peer, message_t *msg);

#endif
