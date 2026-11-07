#include "../include/peer_mutex.h"
#include "../include/peer.h"

int peer_mutex_init(peer_mutex_t *m) {
    if (pthread_mutex_init(&m->udp_liaison_recv, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->tcp_perso_listen, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->udp_perso_recv, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->udp_encheres_recv, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->location, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->peer_list, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->sale_counter, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->auction_list, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->join_list, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&m->supervising_auction_id, NULL) != 0)
        return -1;

    return 0;
}

void peer_mutex_destroy(peer_mutex_t *m) {
    pthread_mutex_destroy(&m->udp_liaison_recv);
    pthread_mutex_destroy(&m->tcp_perso_listen);
    pthread_mutex_destroy(&m->udp_perso_recv);
    pthread_mutex_destroy(&m->udp_encheres_recv);
    pthread_mutex_destroy(&m->location);
    pthread_mutex_destroy(&m->peer_list);
    pthread_mutex_destroy(&m->sale_counter);
    pthread_mutex_destroy(&m->auction_list);
    pthread_mutex_destroy(&m->join_list);
    pthread_mutex_destroy(&m->supervising_auction_id);
}

int peer_lock(peer_t *peer, peer_mutex_field_t field) {
    switch (field) {
        case PEER_MUTEX_UDP_LIAISON_RECV:
            return pthread_mutex_lock(&peer->mutex.udp_liaison_recv);
        case PEER_MUTEX_TCP_PERSO_LISTEN:
            return pthread_mutex_lock(&peer->mutex.tcp_perso_listen);
        case PEER_MUTEX_UDP_PERSO_RECV:
            return pthread_mutex_lock(&peer->mutex.udp_perso_recv);
        case PEER_MUTEX_UDP_ENCHERES_RECV:
            return pthread_mutex_lock(&peer->mutex.udp_encheres_recv);
        case PEER_MUTEX_LOCATION:
            return pthread_mutex_lock(&peer->mutex.location);
        case PEER_MUTEX_PEER_LIST:
            return pthread_mutex_lock(&peer->mutex.peer_list);
        case PEER_MUTEX_SALE_COUNTER:
            return pthread_mutex_lock(&peer->mutex.sale_counter);
        case PEER_MUTEX_AUCTION_LIST:
            return pthread_mutex_lock(&peer->mutex.auction_list);
        case PEER_MUTEX_JOIN_LIST:
            return pthread_mutex_lock(&peer->mutex.join_list);
        case PEER_MUTEX_SUPERVISING_AUCTION_ID:
            return pthread_mutex_lock(&peer->mutex.supervising_auction_id);

        default:
            return -1;
    }
}

int peer_trylock(peer_t *peer, peer_mutex_field_t field) {
    switch (field) {
        case PEER_MUTEX_UDP_LIAISON_RECV:
            return pthread_mutex_trylock(&peer->mutex.udp_liaison_recv);
        case PEER_MUTEX_TCP_PERSO_LISTEN:
            return pthread_mutex_trylock(&peer->mutex.tcp_perso_listen);
        case PEER_MUTEX_UDP_PERSO_RECV:
            return pthread_mutex_trylock(&peer->mutex.udp_perso_recv);
        case PEER_MUTEX_UDP_ENCHERES_RECV:
            return pthread_mutex_trylock(&peer->mutex.udp_encheres_recv);
        case PEER_MUTEX_LOCATION:
            return pthread_mutex_trylock(&peer->mutex.location);
        case PEER_MUTEX_PEER_LIST:
            return pthread_mutex_trylock(&peer->mutex.peer_list);
        case PEER_MUTEX_SALE_COUNTER:
            return pthread_mutex_trylock(&peer->mutex.sale_counter);
        case PEER_MUTEX_AUCTION_LIST:
            return pthread_mutex_trylock(&peer->mutex.auction_list);
        case PEER_MUTEX_JOIN_LIST:
            return pthread_mutex_trylock(&peer->mutex.join_list);
        case PEER_MUTEX_SUPERVISING_AUCTION_ID:
            return pthread_mutex_trylock(&peer->mutex.supervising_auction_id);

        default:
            return -1;
    }
}

int peer_unlock(peer_t *peer, peer_mutex_field_t field) {
    switch (field) {
        case PEER_MUTEX_UDP_LIAISON_RECV:
            return pthread_mutex_unlock(&peer->mutex.udp_liaison_recv);
        case PEER_MUTEX_TCP_PERSO_LISTEN:
            return pthread_mutex_unlock(&peer->mutex.tcp_perso_listen);
        case PEER_MUTEX_UDP_PERSO_RECV:
            return pthread_mutex_unlock(&peer->mutex.udp_perso_recv);
        case PEER_MUTEX_UDP_ENCHERES_RECV:
            return pthread_mutex_unlock(&peer->mutex.udp_encheres_recv);
        case PEER_MUTEX_LOCATION:
            return pthread_mutex_unlock(&peer->mutex.location);
        case PEER_MUTEX_PEER_LIST:
            return pthread_mutex_unlock(&peer->mutex.peer_list);
        case PEER_MUTEX_SALE_COUNTER:
            return pthread_mutex_unlock(&peer->mutex.sale_counter);
        case PEER_MUTEX_AUCTION_LIST:
            return pthread_mutex_unlock(&peer->mutex.auction_list);
        case PEER_MUTEX_JOIN_LIST:
            return pthread_mutex_unlock(&peer->mutex.join_list);
        case PEER_MUTEX_SUPERVISING_AUCTION_ID:
            return pthread_mutex_unlock(&peer->mutex.supervising_auction_id);

        default:
            return -1;
    }
}