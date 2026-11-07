#ifndef MESSAGE_SERIALIZER_H
#define MESSAGE_SERIALIZER_H

#include "../include/message.h"
#include "../lib/format.h"

#include <stdbool.h>
#include <stdint.h>

#define MAX_MESSAGE_SIZE 1024 // Taille maximale d'un message sérialisé

typedef enum serialize_result_t serialize_result_t;
typedef enum deserialize_result_t deserialize_result_t;

// Résultats de sérialisation
enum serialize_result_t {
    SERIALIZE_SUCCESS,
    SERIALIZE_INVALID_ARGS,
    SERIALIZE_BUFFER_TOO_SMALL
};

// Résultats de desérialisation
enum deserialize_result_t {
    DESERIALIZE_SUCCESS,
    DESERIALIZE_INVALID_DATA
};

/**
 * Sérialisation d'un message
 * @param msg Le message à sérialiser
 * @param buffer Le tampon de sortie pour la sérialisation
 * @param buffer_size La taille du tampon de sortie
 * @param out_length La longueur du message sérialisé
 */
serialize_result_t msg_serialize(const message_t *msg, uint8_t *buffer,
                                 size_t buffer_size, size_t *out_length);

/**
 * Desérialisation d'un message
 * @param buffer Le tampon d'entrée contenant le message sérialisé
 * @param buffer_size La taille du tampon d'entrée
 * @param out_msg Le message désérialisé
 * @param skip_code Indique si on souhaite passer l'étape de remplissage de code et
 * remplir le champ code par skip_code_val
 * @param skip_code_val La valeur à utiliser si skip_code est vrai, sinon ignorée,
 * mettre 0 par convention
 * @warning out_msg doit être alloué avant l'appel de cette fonction
 */
deserialize_result_t msg_deserialize(const uint8_t *buffer, size_t buffer_size,
                                     message_t *out_msg, bool skip_code,
                                     code_t skip_code_val);

// Fonctions blocs spécifiques
serialize_result_t bloc_info_serialize(const info_t *info, uint8_t *buffer,
                                       size_t buffer_size, size_t *out_length);

deserialize_result_t bloc_info_deserialize(const uint8_t *buffer, size_t buffer_size,
                                           info_t *out_info);
#endif