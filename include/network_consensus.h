#ifndef NETWORK_CONSENSUS_H
#define NETWORK_CONSENSUS_H

#include "../include/message.h"
#include "../include/network.h"
#include "../include/peer_id_list.h"

#include <stdint.h>

typedef struct consensus_context_t consensus_context_t;
typedef struct consensus_thread_ctx_t consensus_thread_ctx_t;
struct consensus_context_t {
    uint8_t *msg;                  // Message initial, à ne pas modifier, ni libérer
    lmess_t lmess;                 // Longueur du message msg
    peer_id_list_t remaining_list; // Liste des IDs des pairs qui doivent répondre
    uint8_t received_responses;    // Nombre de réponses reçues
    peer_signature_t *sigs;        // Signatures recues des pairs
    bool done;
};

struct consensus_thread_ctx_t {
    peer_t *peer;
    message_t *msg; // Message à traiter
};

int ask_consensus(peer_t *peer, uint8_t *mess, lmess_t lmess);
int respond_consensus(peer_t *peer, uint8_t *mess, lmess_t lmess, info_t validation_dest);

// int verify_validation(message_t *msg, peer_t *peer, consensus_context_t *ctx);
// bool is_all_validation_collected(consensus_context_t *ctx);
int send_final_message(peer_t *peer, consensus_context_t *ctx);

// int handle_code_consensus(peer_t *peer, uint8_t *mess, lmess_t lmess);

// int process_response(int sock, peer_t *peer, consensus_context_t *ctx);
// ssize_t recv_response(int sock_listen, peer_t *peer, consensus_context_t *ctx,
//                       uint8_t *buff, ssize_t buff_len);
// int handle_response(uint8_t *buff, ssize_t len, peer_t *peer, consensus_context_t
// *ctx);

// void send_validation(peer_t *peer, message_t *msg, code_t code);
int take_first_signatures(consensus_context_t *ctx, uint8_t responses,
                          peer_signature_t **sigs);
// int take_next_signatures(consensus_context_t *ctx, uint8_t responses,
//                          size_t current_index, peer_signature_t **sigs);
// int send_consensus_part(peer_t *peer, consensus_context_t *ctx, int sock_send,
//                         struct sockaddr_in6 addr);

// int verify_nb_signatures(peer_t *peer, nb_t peer_count);
// ssize_t recv_consensus_part(int sock_listen, uint8_t *buff, ssize_t buff_len);
// int handle_consensus_part(uint8_t *buff, ssize_t buff_len, peer_t *peer);

// int recv_with_timeout(int sock_listen, uint8_t *buff, ssize_t buff_len, int flags,
//                       struct sockaddr *src_addr, socklen_t *addr_len);

// int relaunch_supervisor(peer_t *peer, message_t *msg);

#endif