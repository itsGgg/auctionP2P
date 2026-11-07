#include "../include/console.h"
#include "../include/auction_list.h"
#include "../include/debug.h"
#include "../include/message.h"
#include "../include/message_builder.h"
#include "../include/message_serializer.h"
#include "../include/network.h"
#include "../include/network_auction.h"
#include "../include/network_consensus.h"
#include "../include/network_leave.h"
#include "../include/safe_output.h"

#include <ctype.h>
#include <pthread.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

atomic_bool should_exit = false;
static peer_t *prompt_context = NULL;

void set_prompt_context(peer_t *ctx) {
    prompt_context = ctx;
}

peer_t *get_prompt_context(void) {
    return prompt_context;
}

char *get_prompt() {
    return "> ";
}

int tokenize(const char *line, char ***tokens_out) {
    if (line == NULL || tokens_out == NULL) {
        return -1; // Vérification des arguments d'entrée
    }

    const char *delimiters = " ";
    size_t initial_capacity = 10;
    size_t token_count = 0;
    char **tokens = malloc(initial_capacity * sizeof(char *));
    if (tokens == NULL) {
        perror("Erreur d'allocation mémoire");
        return -1;
    }

    char *line_copy = strdup(line); // Créer une copie modifiable de la ligne
    if (line_copy == NULL) {
        perror("Erreur d'allocation mémoire");
        free(tokens);
        return -1;
    }

    char *token = strtok(line_copy, delimiters);
    while (token != NULL) {
        if (token_count >= initial_capacity) {
            initial_capacity *= 2;
            char **new_tokens = realloc(tokens, initial_capacity * sizeof(char *));
            if (new_tokens == NULL) {
                perror("Erreur d'allocation mémoire");
                free(line_copy);
                free(tokens);
                return -1;
            }
            tokens = new_tokens;
        }

        tokens[token_count] = strdup(token); // Allouer pour chaque token
        if (tokens[token_count] == NULL) {
            perror("Erreur d'allocation mémoire");
            free(line_copy);
            for (size_t i = 0; i < token_count; i++) {
                free(tokens[i]);
            }
            free(tokens);
            return -1;
        }

        token_count++;
        token = strtok(NULL, delimiters);
    }

    free(line_copy); // Libérer la copie globale, plus utilisée

    tokens = realloc(tokens, (token_count + 1) * sizeof(char *));
    if (tokens == NULL) {
        perror("Erreur d'allocation mémoire");
        for (size_t i = 0; i < token_count; i++) {
            free(tokens[i]);
        }
        free(tokens);
        return -1;
    }
    tokens[token_count] = NULL; // Ajouter un marqueur de fin

    *tokens_out = tokens;
    return token_count; // Retourne le nombre de tokens trouvés
}

int free_tokens(char **tokens) {
    if (tokens == NULL) {
        return 0;
    }
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
    return 0;
}

int print_tokens(char **tokens) {
    if (tokens == NULL) {
        return 0;
    }
    for (int i = 0; tokens[i] != NULL; i++) {
        safe_printf("Token %d: %s\n", i, tokens[i]);
    }
    return 0;
}

size_t get_tokens_count(char **tokens) {
    if (tokens == NULL) {
        return 0;
    }
    size_t count = 0;
    while (tokens[count] != NULL) {
        count++;
    }
    return count;
}

void print_starting_message() {

    spf_priority_lock();
    spf_priority("\n===================================== AuctionP2P Client "
                 "=====================================\n");
    spf_priority("Bonjour !\n");
    spf_priority("Vous vous trouvez acutellement sur la file d'attente pour rejoindre "
                 "notre système d'enchères.\n");
    spf_priority("Vous allez être bientôt redirigé, veuillez patienter...\n");
    spf_priority(
        "==============================================================================="
        "========="
        "=====\n\n");
    spf_priority_unlock();
}

void print_welcome_help_message() {
    spf_priority_lock();
    spf_priority(
        "\n======================== AuctionP2P Client ========================\n\n");
    spf_priority("Bienvenue sur AuctionP2P, notre système d'enchères sécurisé !\n\n");
    spf_priority("Commencez par taper : \"help\" ou \"ahelp\"\n");
    spf_priority("Les commandes ci-dessus sont disponibles partout où que vous soyez,\n"
                 "  vous pourriez connaître ce que pouvez faire à votre endroit...\n");
    spf_priority(
        "===================================================================\n\n");
    spf_priority_unlock();
}

char *location_to_str(user_location_t location) {
    switch (location) {
        case LOCATION_LOBBY:
            return "LOBBY";
        case LOCATION_AUCTION:
            return "AUCTION";
        default:
            return "UNKNOWN";
    }
}

int str_to_uint32(const char *str, uint32_t *res) {
    if (!str || !res) {
        LOG_ERROR("Arguments invalides");
        return -1;
    }

    // Conversion du prix str en entier
    char *endptr;
    uint32_t converted = strtol(str, &endptr, 10);
    if (*endptr != '\0') {
        safe_printf("Valeur avec un format invalide\n");
        return -1;
    }

    if (converted < 0 || converted > UINT32_MAX) {
        safe_printf("Valeur hors limites\n");
        return -1;
    }

    *res = (prix_t)converted;
    if (*res == 0) {
        safe_printf("Valeur nulle non autorisée\n");
        return -1;
    }

    return 0;
}

int update_location(peer_t *peer, user_location_t location) {
    if (peer == NULL) {
        LOG_INFO("Impossible de mettre à jour la position : peer invalide\n");
        return -1;
    }

    peer_lock(peer, PEER_MUTEX_LOCATION);
    user_location_t old_location = peer->location;
    peer->location = location;
    peer_unlock(peer, PEER_MUTEX_LOCATION);

    safe_printf("Mise à jour de votre position : %s -> %s\n",
                location_to_str(old_location), location_to_str(location));

    return 0;
}

static void print_admin_help_message() {
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Commandes disponibles :\n");
    spf_priority("  fleave                - Forcer la sortie (CODE_SYSTEM_LEAVE)\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void print_overall_help_message() {
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Commandes disponibles partout :\n");
    spf_priority("  me                    - Voir vos informations\n");
    spf_priority("  syst                  - Voir les informations du système\n");
    spf_priority("  create <prix>         - Création d'une nouvelle enchère \n");
    spf_priority("  join <numv>           - Rejoindre l'enchère avec son id\n");
    spf_priority("  alist                 - Afficher la liste des enchères\n");
    spf_priority(
        "  jlist                 - Afficher la liste des enchères auquelles vous "
        "participez\n");
    spf_priority(
        "  help                  - Afficher le manuel d'aide à votre position\n");
    spf_priority("  ahelp                 - Afficher le manuel d'aide global\n");
    spf_priority("  exit                  - Quitter l'application (sans CODE=13)\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void print_auction_help_message() {
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Commandes disponibles qu'à : %s\n", location_to_str(LOCATION_AUCTION));
    spf_priority("  bid <numv> <prix>     - Enchérir avec la valeur spécifiée\n");
    spf_priority("  badd <numv> <p_add>   - Enchérir de <p_add> sur le prix courant\n");
    spf_priority(
        "  status <numv>         - Afficher l'état courant de l'enchère <numv>\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void print_lobby_help_message() {
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Commandes disponibles qu'à : %s\n", location_to_str(LOCATION_LOBBY));
    spf_priority("  leave                 - Se retirer du système\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void execute_me(peer_t *peer) {
    if (peer == NULL) {
        LOG_ERROR("Peer invalide");
        return;
    }
    print_peer(peer);
}

static void execute_syst(peer_t *peer) {
    if (peer == NULL) {
        LOG_ERROR("Peer invalide");
        return;
    }
    print_auctions_system(peer);
}

static void execute_create(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count != 2) {
        LOG_WARNING("Usage : create <prix>");
        return;
    }

    if (send_auction_start(peer, tokens[1]) < 0) {
        safe_printf("Une erreur s'est produite lors de la création de l'enchère.\n");
        return;
    }
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Votre enchère a été créée avec succès !\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();

    LOG_INFO("Surveillance de l'enchère supervisée démarrée...");
    if (start_monitoring_auction(peer) < 0) {
        LOG_ERROR("Échec de la surveillance de l'enchère supervisée");
        return;
    } else {
        LOG_INFO("Surveillance de l'enchère supervisée terminée.");
    }
}

static void execute_join(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count != 2) {
        LOG_WARNING("Usage : join <numv>");
        return;
    }

    numv_t a_id;
    if (str_to_uint32(tokens[1], &a_id) < 0) {
        return;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    bool contains_auc = auction_list_contains(&peer->auction_list, a_id);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!contains_auc) {
        safe_printf("NUMV inconnu ! Vous ne pouvez pas rejoindre cette enchère.\n");
        return;
    }

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    bool is_joined = auc_id_list_contains(&peer->join_list, a_id);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (is_joined) {
        safe_printf("Vous participez déjà à cette enchère !\n");
        return;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    bool is_open = auction_list_find(&peer->auction_list, a_id)->state != AUCTION_OPEN;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (is_open) {
        safe_printf("L'enchère n'est pas ouverte. Vous ne pouvez pas la rejoindre.\n");
        return;
    }

    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    bool added = auc_id_list_add(&peer->join_list, a_id) >= 0;
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
    if (!added) {
        safe_printf(
            "Une erreur s'est produite lors de l'ajout de l'enchère à votre liste.\n");
        return;
    }
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Vous avez rejoint l'enchère avec succès !\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void execute_alist(peer_t *peer) {
    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_list_print(&peer->auction_list);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
}

static void execute_jlist(peer_t *peer) {
    peer_lock(peer, PEER_MUTEX_JOIN_LIST);
    auc_id_list_print(&peer->join_list);
    peer_unlock(peer, PEER_MUTEX_JOIN_LIST);
}

static void execute_ahelp(peer_t *peer) {
    if (peer == NULL) {
        LOG_ERROR("Peer invalide");
        return;
    }
    print_admin_help_message();
    print_overall_help_message();
    print_auction_help_message();
    print_lobby_help_message();
    peer_lock(peer, PEER_MUTEX_LOCATION);
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Votre position actuelle : %s\n", location_to_str(peer->location));
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
    peer_unlock(peer, PEER_MUTEX_LOCATION);
}

/**
 * Quitte l'application de manière brusque, sans prévenir les pairs.
 * Utiliser "leave" pour la version propre.
 * Equivalent à un CTRL+C mais un peu plus propre.
 * @note Simulation crash, utiliser fleave ou leave sinon
 */
static void execute_exit() {
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Fermeture de l'application... (force sans code)");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
    atomic_store(&should_exit, true);
}

static void execute_bid(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count != 3) {
        LOG_WARNING("Usage : bid <numv> <prix>");
        return;
    }
    if (send_auction_bid(peer, tokens[1], tokens[2], false) < 0) {
        safe_printf("Une erreur s'est produite lors de l'enchérissement.\n");
        return;
    }
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Votre enchérissement a été envoyée avec succès !\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void execute_badd(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count != 3) {
        LOG_WARNING("Usage : badd <numv> <p_add>");
        return;
    }
    if (send_auction_bid(peer, tokens[1], tokens[2], true) < 0) {
        safe_printf("Une erreur s'est produite lors de l'enchérissement.\n");
        return;
    }
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Votre enchérissement a été envoyée avec succès !\n");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
}

static void execute_status(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count != 2) {
        LOG_WARNING("Usage : status <numv>");
        return;
    }

    numv_t a_id;
    if (str_to_uint32(tokens[1], &a_id) < 0) {
        return;
    }

    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    auction_t *auction = auction_list_find(&peer->auction_list, a_id);
    bool found = auction != NULL;
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
    if (!found) {
        safe_printf("NUMV d'enchère non valide.\n");
        return;
    }
    peer_lock(peer, PEER_MUTEX_AUCTION_LIST);
    print_auction(auction);
    peer_unlock(peer, PEER_MUTEX_AUCTION_LIST);
}

static void execute_leave(peer_t *peer) {
    if (send_system_leave(peer) < 0) {
        LOG_ERROR("Erreur lors de l'envoi du message de sortie");
        safe_printf("Je n'ai pas pu quitter. Réessayez à nouveau la commande.");
        return;
    }
    spf_priority_lock();
    spf_priority("---------------------------------------------------------------\n");
    spf_priority("Vous allez quitter le système.");
    spf_priority("---------------------------------------------------------------\n");
    spf_priority_unlock();
    atomic_store(&should_exit, true);
}

static void execute_fleave(peer_t *peer) {
    execute_leave(peer);
}

static void handle_admin_command(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count == 0) {
        LOG_WARNING("Aucune action à effectuer : ligne vide");
        return;
    }

    if (strcmp(tokens[0], "fleave") == 0) {
        execute_fleave(peer);

    } else {
        LOG_INFO("Commande inconnue : %s — utilisez \"help\" ou \"ahelp\" !", tokens[0]);
        LOG_WARNING("Commande n'est peut être pas exécutable ici.");
    }
}

/**
 * Commande éxécutable peu importe où le peer se trouve
 */
static void handle_overall_command(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count == 0) {
        LOG_WARNING("Aucune action à effectuer : ligne vide");
        return;
    }

    if (strcmp(tokens[0], "me") == 0) {
        execute_me(peer);

    } else if (strcmp(tokens[0], "syst") == 0) {
        execute_syst(peer);

    } else if (strcmp(tokens[0], "create") == 0) {
        execute_create(peer, tokens, tokens_count);

    } else if (strcmp(tokens[0], "join") == 0) {
        execute_join(peer, tokens, tokens_count);

    } else if (strcmp(tokens[0], "alist") == 0) {
        execute_alist(peer);

    } else if (strcmp(tokens[0], "jlist") == 0) {
        execute_jlist(peer);

    } else if (strcmp(tokens[0], "ahelp") == 0) {
        execute_ahelp(peer);

    } else if (strcmp(tokens[0], "exit") == 0) {
        execute_exit();

    } else {
        handle_admin_command(peer, tokens, tokens_count);
    }
}

/**
 * Commande éxécutable si le peer participe à au moins une enchère en tant que superviseur
 * ou participant
 */
static void handle_auction_command(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count == 0) {
        LOG_WARNING("Aucune action à effectuer : ligne vide");
        return;
    }

    if (strcmp(tokens[0], "help") == 0) {
        print_auction_help_message();

    } else if (strcmp(tokens[0], "bid") == 0) {
        execute_bid(peer, tokens, tokens_count);

    } else if (strcmp(tokens[0], "badd") == 0) {
        execute_badd(peer, tokens, tokens_count);

    } else if (strcmp(tokens[0], "status") == 0) {
        execute_status(peer, tokens, tokens_count);

    } else {
        handle_overall_command(peer, tokens, tokens_count);
    }
}

/**
 * Commande éxécutable si le peer est dans le lobby (ni participant ni superviseur d'une
 * enchère)
 */
static void handle_lobby_command(peer_t *peer, char **tokens, size_t tokens_count) {
    if (tokens == NULL || tokens_count == 0) {
        LOG_WARNING("Aucune action à effectuer : ligne vide");
        return;
    }

    if (strcmp(tokens[0], "help") == 0) {
        print_lobby_help_message();

    } else if (strcmp(tokens[0], "leave") == 0) {
        execute_leave(peer);

    } else {
        handle_overall_command(peer, tokens, tokens_count);
    }
}

void *handle_user_command(void *arg) {
    peer_t *peer = get_prompt_context();
    char *line = (char *)arg;
    if (!peer || !line) {
        return NULL;
    }

    char **tokens = NULL;
    if (tokenize(line, &tokens) < 0) {
        LOG_ERROR("parse_line_to_command : Erreur de tokenisation");
        return NULL;
    }
    size_t tokens_count = get_tokens_count(tokens);
    peer_lock(peer, PEER_MUTEX_LOCATION);
    user_location_t location = peer->location;
    peer_unlock(peer, PEER_MUTEX_LOCATION);
    switch (location) {
        case LOCATION_LOBBY:
            handle_lobby_command(peer, tokens, tokens_count);
            break;

        case LOCATION_AUCTION:
            handle_auction_command(peer, tokens, tokens_count);
            break;

        default:
            LOG_ERROR("Emplacement non valide : LOCATION_LIAISON");
            break;
    }
    free_tokens(tokens);
    tokens = NULL;
    tokens_count = 0;
    free(line);
    LOG_INFO("Fermeture du thread de gestion de la commande utilisateur.\n");
    return NULL;
}

void handle_prompt(char *line) {
    in_console = 1;
    if (line == NULL) {
        // Si nécessaire
    } else if (*line) {
        add_history(line);

        // Copie de la ligne
        char *line_copy = strdup(line);
        if (line_copy == NULL) {
            LOG_ERROR("strdup échoué");

        } else {
            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_user_command, line_copy) != 0) {
                LOG_ERROR("Erreur lors de la création du thread");
                free(line_copy);
            } else {
                LOG_INFO("Thread créé pour gérer la commande utilisateur");
                pthread_detach(thread);
            }
        }

        rl_set_prompt(get_prompt());
        rl_replace_line("", 0);
        rl_redisplay();
        safe_printf("\n");
    }
    free(line);
    in_console = 0;
}
