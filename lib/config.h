#ifndef CONFIG_H
#define CONFIG_H

// pour IPV6_ADDR_LEN
#include <arpa/inet.h>
#include <netinet/in.h>

// Configuration réseau
#define LIAISON_MULTICAST_ADDR "ff12::1234" // Adresse IPv6 multicast de liaison
#define LIAISON_MULTICAST_PORT 3107         // Port multicast de liaison
#define AUCTIONS_MIN_PORT 9000
#define AUCTIONS_MAX_PORT 12000

// Délais et timeouts (en secondes)
#define CONSENSUS_TIMEOUT 5        // Temps d'attente pour le consensus
#define CONSENSUS_MAX_ATTEMPT 3    // Tentatives max pour le consensus
#define JOIN_TIMEOUT_SEC 5         // Temps d'attente pour rejoindre le système
#define JOIN_ATTEMPT_MAX 3         // Nombre max de tentatives pour rejoindre
#define BID_END_TIMEOUT 30         // Temps d'inactivité avant fin d'enchère
#define BID_CONFIRM_END_TIMEOUT 15 // Temps pour la confirmation de fin de l'enchère
#define PEER_RESPONSE_TIMEOUT 5    // Temps d'attente pour les réponses
#define PEER_RESPONSE_RETRY 3      // Nombre de réessais pour les messages UDP

// Peer infos
#define ID_MIN 1                 // 0 réservé pour erreur et debug
#define ID_MAX UINT16_MAX        // 65535
#define IPV6_ADDR_LEN 16         // Longueur d'une adresse IPv6
#define ED25519_PUB_KEY_LEN 113  // Longueur max clé publique ED25519
#define ED25519_PRIV_KEY_LEN 185 // Longueur max clé privée ED25519

// Auctions
#define AUTO_JOIN_AUCTIONS 1 // 1 == auto-join activé, 0 == auto-join désactivé

// Limites
#define MAX_PEERS 0 //  0 == (sans limite, soit 4096, ou parfois 128)

#define TLS_ON 1 // 1 == TLS activé, 0 == TLS désactivé

#define CONSENSUS_ON 1 // 1 == Consensus activé , 0 ==  Consensus desactivé

#define MAX_SIGNATURES 3 // Nombre max de signatures dans un message de consensus

#endif