#ifndef NETWORK_AUCTION_H
#define NETWORK_AUCTION_H

#include "../include/message.h"
#include "../include/peer.h"

int send_auction_start(peer_t *peer, const char *a_price);
int send_auction_bid(peer_t *peer, const char *a_id, const char *a_price,
                     bool is_bid_add);
int send_auction_almost_end(peer_t *peer);
int send_auction_end(peer_t *peer);

void handle_auction_start(peer_t *peer, message_t *msg);
void handle_auction_bid(peer_t *peer, message_t *msg);
void handle_auction_bid_confirm(peer_t *peer, message_t *msg);
void handle_auction_almost_end(peer_t *peer, message_t *msg);
void handle_auction_end(peer_t *peer, message_t *msg);
void handle_auction_reject_concurrent(peer_t *peer, message_t *msg);
void handle_auction_reject_invalid(peer_t *peer, message_t *msg);

int start_monitoring_auction(peer_t *peer);

#endif