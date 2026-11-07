#ifndef MESSAGE_H
#define MESSAGE_H

#include "../include/crypto.h"
#include "../include/peer.h"
#include "../lib/format.h"

#include <stdbool.h>
#include <time.h>

// --- Structures spécifiques pour chaque type de message ---
typedef struct msg_validation_t msg_validation_t;
typedef struct msg_consensus_t msg_consensus_t;
// typedef struct msg_join_request_t msg_join_request_t;
typedef struct msg_join_response_t msg_join_response_t;
typedef struct msg_peer_info_t msg_peer_info_t;
typedef struct msg_peer_info_response_t msg_peer_info_response_t;
typedef struct msg_system_info_t msg_system_info_t;
typedef struct msg_auction_t msg_auction_t;
typedef struct msg_leave_t msg_leave_t;
typedef struct msg_peer_management_t msg_peer_management_t;

// Structures pour un message
typedef struct message_t message_t;

// Message de validation (CODE = 1)
struct msg_validation_t {
    app_id_t id;
    mess_t message;
    sign_t signature;
};

// Message de consensus (CODE = 2 ou 20)
struct msg_consensus_t {
    app_id_t id;
    mess_t message;
    sign_t signature;
    nb_t peer_count;
    peer_signature_t *peer_signatures;
};

// Message de demande de jonction (CODE = 3)
// struct msg_join_request_t {
//     // Pas utile
// };

// Message de réponse à jonction (CODE = 4)
struct msg_join_response_t {
    app_id_t id;
    ip_t ip;
    port_t port;
};

// Message d'info pair (CODE = 5 ou 6)
struct msg_peer_info_t {
    info_t info;
};

// Réponses aux infos pair (CODE = 50[vide/pas utile]/51)
struct msg_peer_info_response_t {
    app_id_t assigned_id; // Pour CODE = 51
};

// Message d'info système (CODE = 7)
struct msg_system_info_t {
    app_id_t id;
    ip_t auction_ip;
    port_t auction_port;
    nb_t peer_count;
    // info_t *peer_info;
};

// Messages d'enchères (CODE = [8-12] ou [14-15])
struct msg_auction_t {
    app_id_t id;
    numv_t auction_id;
    prix_t price;
};

// Message de quitter système (CODE = 13)
struct msg_leave_t {
    app_id_t id;
};

// Messages de gestion des pairs (CODE = 16-18)
struct msg_peer_management_t {
    app_id_t id;

    union {
        numv_t auction_id; // Pour CODE = 16 ou 17
        struct {
            nb_t peer_count;    // Pour CODE = 18
            app_id_t *peer_ids; // Pour CODE = 18
        } peers;
    } afk;
};

// Structure générique des messages
struct message_t {
    code_t code;
    union {
        msg_validation_t validation;
        msg_consensus_t consensus;
        // msg_join_request_t join_req;
        msg_join_response_t join_resp;
        msg_peer_info_t peer_info;
        msg_peer_info_response_t peer_info_resp;
        msg_system_info_t system_info;
        msg_auction_t auction;
        msg_leave_t leave;
        msg_peer_management_t peer_mgmt;
    } content;
};

message_t *message_create(message_codes_t code);
void message_free(message_t *msg);
bool message_verify_signature(const message_t *msg, const cle_pub_t *public_key);
int message_sign(message_t *msg, EVP_PKEY *pkey);
const char *message_codes_to_string(message_codes_t code);

// Fonctions de calculs de taille pour les messages TCP boucle while, évitant le padding
size_t msg_size_join_response();
size_t msg_size_peer_info();
size_t msg_size_peer_info_resp();
size_t msg_size_new_peer_info();
size_t msg_prefix_size_system_info();

// Fonctions de calculs de taille pour Blocs
size_t bloc_info_size();

// ------------------------------ Fonctions free ------------------------------ //

int free_peer_ids_list(msg_peer_management_t *msg); // CODE 18

#endif