#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Activer ou désactiver le mode débogage ici
#ifndef DEBUG
// #define DEBUG  // Décommenter pour activer
#endif

typedef enum log_level_t log_level_t;
typedef enum log_trigger_t log_trigger_t;

#define LOG_MAX_BUF_SIZE 512

#define COLOR_RESET "\x1b[0m"
#define COLOR_INFO "\x1b[36m"    // cyan
#define COLOR_WARNING "\x1b[33m" // jaune
#define COLOR_ERROR "\x1b[31m"   // rouge

// Niveaux de log
enum log_level_t {
    NO_LOG,      // 0 (Uniquement DEBUG_CALL) (DEFAUT)
    LOG_INFO,    // 1
    LOG_WARNING, // 2
    LOG_ERROR,   // 3
};

// Niveau de log
#ifndef LOG_LEVEL
// Niveau de log par défaut
#define LOG_LEVEL NO_LOG
#endif

enum log_trigger_t {
    LOG_UNIQ, // 0
    LOG_MAX   // 1
};

// Uniquement un niveau de log précis
#ifndef LOG_TRIGGER
// Niveau de log par défaut
#define LOG_TRIGGER LOG_UNIQ
#endif

// Macro pour gérer les logs selon le niveau
#ifdef DEBUG
#define LOG(level, fmt, ...) log_message(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DEBUG_CALL(func_call)                                                            \
    do {                                                                                 \
        if (LOG_LEVEL == NO_LOG)                                                         \
            func_call;                                                                   \
    } while (0)
#else
#define LOG(level, fmt, ...)                                                             \
    do {                                                                                 \
    } while (0)
#define DEBUG_CALL(func_call) ((void)0)
#endif

// Fonctions spécifiques pour chaque type de log
#define LOG_INFO(fmt, ...) LOG(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) LOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG(LOG_ERROR, fmt, ##__VA_ARGS__)

// Fonction de gestion des messages de log
void log_message(log_level_t level, const char *file, int line, const char *fmt, ...);

#endif
