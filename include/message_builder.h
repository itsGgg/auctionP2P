#ifndef MESSAGE_BUILDER_H
#define MESSAGE_BUILDER_H

#include "../include/message.h"

/* ----------------------------------------------------------------- */
/*                     CONSTRUCTEURS SPÉCIALISÉS                     */
/* ----------------------------------------------------------------- */
message_t *msg_create_validation(app_id_t id, mess_t *message, sign_t *signature);
message_t *msg_create_consensus(const peer_t *peer, mess_t *message, sign_t *signature,
                                nb_t peer_count, peer_signature_t *peer_signatures);
message_t *msg_create_consensus_part(const peer_t *peer, nb_t peer_count,
                                     peer_signature_t *peer_signatures);
message_t *msg_create_join_request();
message_t *msg_create_join_response(const peer_t *peer);
message_t *msg_create_peer_info(const peer_t *peer);
message_t *msg_create_peer_info_resp_ok();
message_t *msg_create_peer_info_resp_alt(app_id_t id);
message_t *msg_create_new_peer_info(const info_t *new_peer);
message_t *msg_create_system_info(const peer_t *peer);

message_t *msg_create_auction_start(app_id_t supervisor_id, numv_t auction_id,
                                    prix_t price);
message_t *msg_create_auction_bid(app_id_t bidder_id, numv_t auction_id,
                                  prix_t new_price);
message_t *msg_create_auction_bid_confirm(app_id_t bidder_id, numv_t auction_id,
                                          prix_t new_price);
message_t *msg_create_auction_almost_end(app_id_t supervisor_id, numv_t auction_id,
                                         prix_t last_price);
message_t *msg_create_auction_end(app_id_t winner_id, numv_t auction_id,
                                  prix_t last_price);
message_t *msg_create_system_leave(app_id_t id);
message_t *msg_create_auction_reject_concurrent(app_id_t rejected_id, numv_t auction_id,
                                                prix_t price);
message_t *msg_create_auction_reject_invalid(app_id_t rejected_id, numv_t auction_id,
                                             prix_t price);
message_t *msg_create_supervisor_afk(const peer_t *supervisor, numv_t auction_id);
message_t *msg_create_cancel_transaction(const peer_t *supervisor, numv_t auction_id);
message_t *msg_create_peers_remove(const peer_t *peer, nb_t afk_count, app_id_t *left);

int copy_signature(sign_t *dest, const sign_t *src);
int copy_peer_signatures(peer_signature_t **dest, const peer_signature_t *src,
                         nb_t count);
// ETC... à  ajouter

#endif