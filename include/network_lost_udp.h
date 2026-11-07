#ifndef NETWORK_LOST_UDP_H
#define NETWORK_LOST_UDP_H

#include "../include/network.h"
#include "../include/network_consensus.h"

int relaunch_consensus_peer(peer_t *peer, consensus_context_t *ctx);
int resend_message(peer_t *peer, message_t *msg, int sock_send, struct sockaddr_in6 addr);

#endif
