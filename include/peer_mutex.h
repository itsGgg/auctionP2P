#ifndef PEER_MUTEX_H
#define PEER_MUTEX_H

#include <pthread.h>

typedef struct peer_mutex_t peer_mutex_t;
typedef struct peer_t peer_t; // Forward declaration

/**
 * @warning
 * L'ordre des mutex dans cette énumération doit correspondre à l'ordre des appels de lock
 * pour éviter les deadlocks.
 * @warning En fonction des situations, le lock sur une socket doit être accompagné
 * d'une MAJ du flag associé type " in_use_* " défini dans network_handler
 */
typedef enum {
    PEER_MUTEX_UDP_LIAISON_RECV,
    PEER_MUTEX_TCP_PERSO_LISTEN,
    PEER_MUTEX_UDP_PERSO_RECV,
    PEER_MUTEX_UDP_ENCHERES_RECV,
    PEER_MUTEX_LOCATION,
    PEER_MUTEX_PEER_LIST,
    PEER_MUTEX_SALE_COUNTER,
    PEER_MUTEX_AUCTION_LIST,
    PEER_MUTEX_JOIN_LIST,
    PEER_MUTEX_SUPERVISING_AUCTION_ID
} peer_mutex_field_t;

/**
 * Cette structure contient tous les mutex nécessaires pour la synchronisation
 * des accès aux ressources partagées dans le module peer (exclus ceux qui sont
 * "considérées comme const", type id ; ip ; port ...).
 */
struct peer_mutex_t {
    pthread_mutex_t udp_liaison_recv;
    pthread_mutex_t tcp_perso_listen;
    pthread_mutex_t udp_perso_recv;
    pthread_mutex_t udp_encheres_recv;
    pthread_mutex_t location;
    pthread_mutex_t peer_list;
    pthread_mutex_t sale_counter;
    pthread_mutex_t auction_list;
    pthread_mutex_t join_list;
    pthread_mutex_t supervising_auction_id;
};

int peer_mutex_init(peer_mutex_t *mutexes);
void peer_mutex_destroy(peer_mutex_t *mutexes);

int peer_lock(peer_t *peer, peer_mutex_field_t field);
int peer_trylock(peer_t *peer, peer_mutex_field_t field);
int peer_unlock(peer_t *peer, peer_mutex_field_t field);

#endif