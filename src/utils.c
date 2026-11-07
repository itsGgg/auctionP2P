// Fichier pour les fonctions utilitaires communes

#include "../include/utils.h"
#include "../include/debug.h"
#include "../include/safe_output.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#if defined(__linux__)
#include <linux/limits.h>
#else
#include <limits.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int write_output(const char *message) {
    if (write(STDOUT_FILENO, message, strlen(message)) == -1) {
        perror("write: Erreur d'écriture.");
        return -1;
    }

    if (write(STDOUT_FILENO, "\n", 1) == -1) {
        perror("write: Erreur d'écriture.");
        return 1;
    }
    return 0;
}

int write_output_no_ln(const char *message) {
    if (write(STDOUT_FILENO, message, strlen(message)) == -1) {
        perror("write: Erreur d'écriture.");
        return -1;
    }
    return 0;
}

// Fonction pour écrire une chaîne dans la sortie erreur standard
int write_err(const char *message) {
    if (write(STDERR_FILENO, message, strlen(message)) == -1) {
        perror("write : Erreur d'écriture.");
        return -1;
    }

    if (write(STDOUT_FILENO, "\n", 1) == -1) {
        perror("write: Erreur d'écriture.");
        return 1;
    }
    return 0;
}

int write_err_no_ln(const char *message) {
    if (write(STDERR_FILENO, message, strlen(message)) == -1) {
        perror("write : Erreur d'écriture.");
        return -1;
    }
    return 0;
}

void print_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        LOG_INFO("print_hex: data[%zu] = %02x", i, data[i]);
        safe_printf("%02x ", data[i]);
    }
    safe_printf("\n");
}

void print_ipv6(const uint8_t ip[IPV6_ADDR_LEN]) {
    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, ip, ip_str, sizeof(ip_str));
    safe_printf("%s\n", ip_str);
}

void hex_dump(const void *data, size_t len) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < len; i += 16) {
        safe_printf("%04zx  ", i); // offset

        // hex part
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len)
                safe_printf("%02x ", bytes[i + j]);
            else
                safe_printf("   ");
        }

        safe_printf(" |");

        // ASCII part
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) {
                unsigned char c = bytes[i + j];
                safe_printf("%c", isprint(c) ? c : '.');
            }
        }

        safe_printf("|\n");
    }
}

const char *public_key_to_str_static(const cle_pub_t *key) {
    static char key_str[ED25519_PUB_KEY_LEN + 1];
    memcpy(key_str, key->key, ED25519_PUB_KEY_LEN);
    key_str[ED25519_PUB_KEY_LEN] = '\0';
    return key_str;
}

/**
 * Renvoie le repertoire actuel
 */
char *get_current_directory() {
    LOG_INFO("Récupération du répertoire courant");
    char *cwd = getcwd(NULL, 0); // NULL + 0 = allocation dynamique
    if (cwd == NULL) {
        LOG_ERROR("Échec de récupération du répertoire courant");
        perror("getcwd");
        return NULL;
    }
    LOG_INFO("Répertoire courant récupéré");
    return cwd;
}
/*
 * Méthode permettant de récuperer le répertoire de l'éxecutable
 */
char *get_executing_directory() {
    LOG_INFO("Récupération du répertoire de l'exécutable");
    // Allocation du buffer de taille PATH_MAX - longueur maximale d'un PATH
    char *buffer = malloc(PATH_MAX);
    if (buffer == NULL) {
        LOG_ERROR("Échec d'allocation mémoire pour le chemin de l'exécutable");
        perror("malloc");
        return NULL;
    }
    // Récuperer le chemin absolu pointé par l'exécutable en cours d'exécution
    // (AuctionP2P)
    int size = readlink("/proc/self/exe", buffer, PATH_MAX - 1);
    if (size == -1) {
        LOG_ERROR("Échec de lecture du lien symbolique de l'exécutable");
        perror("readlink @ get_executable_directory");
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';

    // Trouver le dernier '/' du buffer et le remplacere avec '\0'
    char *get_last_slash = strrchr(buffer, '/');
    if (get_last_slash == NULL) {
        LOG_ERROR("Aucun séparateur '/' de chemin trouvé");
        perror("strchr");
        free(buffer);
        return NULL;
    }
    // Remplacer ce dernier '\' par '\0'
    *get_last_slash = '\0';
    LOG_INFO("Répertoire de l'exécutable récupéré");
    return buffer;
}
/**
 * Crée un repertoire d'un nom spécifique
 */
int create_specific_directory(char *name) {
    LOG_INFO("Création du répertoire spécifique");
    if (name == NULL) {
        LOG_ERROR("Directory name cannot be NULL");
        perror("directory name cannot be NULL");
        return -1;
    }
    if (mkdir(name, 0700) != 0) {
        if (errno == EEXIST) {
            LOG_INFO("Le répertoire existe déjà");
            return 0;
        }
        LOG_ERROR("Erreur création du répertoire");
        perror("mkdir : erreur création du répertoire \n");
        return -1;
    }
    LOG_INFO("Répertoire créé avec succès");
    return 0;
}
/**
 * Concatène un chemin et un nom de répertoire en ajoutant un '/' entre les deux.
 * @param path Le chemin de base
 * @param directory_name Le nom du répertoire à ajouter
 * @return Un nouveau chemin alloué dynamiquement ou NULL en cas d'erreur
 */
char *build_path(char *path, char *file_name) {
    LOG_INFO("Construction d'un chemin");
    size_t total_len = strlen(path) + strlen(file_name) + 2; // 1 pour '/', 1 pour '\0'
    char *res = malloc(sizeof(char) * total_len);

    if (res == NULL) {
        LOG_ERROR("Échec d'allocation mémoire pour le chemin");
        perror("build_path");
        return NULL;
    }
    snprintf(res, total_len, "%s/%s", path, file_name);
    LOG_INFO("Chemin construit avec succès");
    return res;
}

/**
 * Méthode permettant de récuperer une liste d'interfaces disponibles
 */
int get_network_interfaces(char **interfaces, int max_count) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    dir = opendir("/sys/class/net/");
    if (dir == NULL) {
        LOG_ERROR("Impossible d'ouvrir /sys/class/net/");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL && count < max_count) {
        // Ignorer . et ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Copier le nom de l'interface
        interfaces[count] = strdup(entry->d_name);
        if (interfaces[count] == NULL) {
            LOG_ERROR("strdup");
            for (int i = 0; i < count; i++) {
                free(interfaces[i]);
            }
            closedir(dir);
            return -1;
        }
        count++;
    }

    closedir(dir);
    return count;
}

int uint8_to_str_alloc(const uint8_t *data, size_t data_len, char **out_str) {
    if (!data || data_len == 0 || !out_str) {
        return -1;
    }

    char *buffer = malloc(data_len + 1);
    if (!buffer) {
        return -1;
    }

    memcpy(buffer, data, data_len);
    buffer[data_len] = '\0';

    *out_str = buffer;
    return 0;
}

/**
 * Renvoie une clé de format pem en remplaçant les '\n' par des espaces
 */
const char *key_without_newline(const uint8_t *key, int key_type) {
    if (key == NULL || (key_type != 0 && key_type != 1)) {
        LOG_ERROR("key NULL ou key_type invalide");
        return NULL;
    }
    int len = (key_type == 0) ? ED25519_PUB_KEY_LEN : ED25519_PRIV_KEY_LEN;

    static char result[ED25519_PRIV_KEY_LEN + 1];
    memcpy(result, key, len);
    result[len] = '\0';

    for (int i = 0; i < len; i++) {
        if (result[i] == '\n') {
            result[i] = ' ';
        }
    }
    return result;
}
