#include "../include/safe_output.h"

#include <pthread.h>
#include <readline/readline.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

volatile sig_atomic_t in_console = 0;
static pthread_mutex_t output_mutex;
static pthread_mutex_t priority_mutex;

void safe_output_init(void) {
    pthread_mutex_init(&output_mutex, NULL);
    pthread_mutex_init(&priority_mutex, NULL);
}

void safe_output_destroy(void) {
    pthread_mutex_destroy(&output_mutex);
    pthread_mutex_destroy(&priority_mutex);
}

/**
 * @warning Il ne doit jamais y avoir de peer_lock ou en peer_unlock
 * en dessous de spf_priority_lock
 */
void spf_priority_lock() {
    pthread_mutex_lock(&priority_mutex);
}

/**
 * @warning Il ne doit jamais y avoir de peer_lock ou en peer_unlock
 * au dessus de spf_priority_unlock
 */
void spf_priority_unlock() {
    pthread_mutex_unlock(&priority_mutex);
}

void safe_printf(const char *format, ...) {
    spf_priority_lock();
    pthread_mutex_lock(&output_mutex);

    // Sauvegarde du buffer courant
    char *buffer = rl_copy_text(0, rl_end);
    int saved_point = rl_point;

    // Déplace le curseur en début de ligne et efface la ligne visuellement
    printf("\33[2K\r"); // Clear entire line + carriage return

    if (!in_console) {
        rl_save_prompt();
        rl_replace_line("", 0); // efface le buffer interne
        rl_redisplay();         // met à jour l'affichage (vide)
    }

    // Affichage protégé
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    fflush(stdout); // force l’affichage

    // Restauration du prompt
    if (!in_console) {
        rl_restore_prompt();
        rl_replace_line(buffer, 0);
        rl_point = saved_point;
        rl_redisplay();
    }

    free(buffer);
    pthread_mutex_unlock(&output_mutex);
    spf_priority_unlock();
}

/**
 * Pour conserver un affichage entier d'un bloc.
 * Appeler spf_priority_lock & spf_priority_unlock avant et après
 * d'utiliser cette fonction.
 */
void spf_priority(const char *format, ...) {
    pthread_mutex_lock(&output_mutex);

    // Sauvegarde du buffer courant
    char *buffer = rl_copy_text(0, rl_end);
    int saved_point = rl_point;

    // Déplace le curseur en début de ligne et efface la ligne visuellement
    printf("\33[2K\r"); // Clear entire line + carriage return

    if (!in_console) {
        rl_save_prompt();
        rl_replace_line("", 0); // efface le buffer interne
        rl_redisplay();         // met à jour l'affichage (vide)
    }

    // Affichage protégé
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    fflush(stdout); // force l’affichage

    // Restauration du prompt
    if (!in_console) {
        rl_restore_prompt();
        rl_replace_line(buffer, 0);
        rl_point = saved_point;
        rl_redisplay();
    }

    free(buffer);
    pthread_mutex_unlock(&output_mutex);
}
