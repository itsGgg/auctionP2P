#include "../include/console.h"
#include "../include/debug.h"
#include "../include/message.h"
#include "../include/network.h"
#include "../include/network_handler.h"
#include "../include/network_join.h"
#include "../include/network_sockets.h"
#include "../include/peer.h"
#include "../include/safe_output.h"
#include "../lib/config.h"

#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    srand(time(NULL));

    peer_t *myself;
    if ((myself = malloc(sizeof(peer_t))) == NULL) {
        LOG_ERROR("Échec de l'allocation mémoire pour le pair.");
        exit(EXIT_FAILURE);
    }

    safe_output_init();
    rl_callback_handler_install(get_prompt(), handle_prompt);
    set_prompt_context(myself);

    print_starting_message();

    if (network_init(myself) < 0) {
        LOG_ERROR("Échec de l'initialisation du réseau et de vos informations.");
        goto cleanup;
    }

    int found = network_discover_peers(myself);

    if (found < 0) {
        LOG_ERROR("Échec lors de la découverte réseau.");
        network_sockets_close(&myself->sockets);
        goto cleanup;
    } else if (found == 0) {
        safe_printf("Aucun système existant détecté — création du système...\n");
        if (network_create_system(myself) < 0) {
            goto cleanup;
        }
    } else {
        safe_printf("\nPair existant détecté — jointure en cours ...\n");
        if (network_join_existing(myself) < 0) {
            goto cleanup;
        }
    }

    print_welcome_help_message();

    network_event_loop(myself);

cleanup:
    safe_printf("Nettoyage et fermeture...\n");

    network_sockets_close(&myself->sockets);
    peer_free(myself);
    rl_callback_handler_remove();

    safe_printf("Au revoir !\n");
    safe_output_destroy();

    exit(0);
}