#include "../include/crypto.h"
#include "../include/debug.h"
#include "../include/message.h"
#include "../include/peer.h"
#include "../include/safe_output.h"
#include "../include/utils.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

/**
 * Méthode permettant de génèrer une paire de clés ED25519 au format PEM
 *
 * @param[out] public_key  Pointeur qui recevra la clé publique générée
 * @param[out] private_key Pointeur qui recevra la clé privée générée
 */
int generate_ED25519_keys(uint8_t **public_key, uint8_t **private_key, size_t *pub_len,
                          size_t *priv_len) {
    LOG_INFO("Initialisation génération des clés ED25519");
    if (public_key == NULL || private_key == NULL) {
        LOG_ERROR("Paramètres invalides: pointeurs NULL fournis");
        return -1;
    }

    int ret = 0;
    long bio_buffer_data_length = 0;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;

    // Création des BIO pour stocker les clés
    BIO *bio_pub = BIO_new(BIO_s_mem()), *bio_priv = BIO_new(BIO_s_mem());

    if (bio_pub == NULL || bio_priv == NULL) {
        LOG_ERROR("Échec de création des tampons BIO pour les clés");
        ret = -1;
        goto cleanup_return;
    }

    OpenSSL_add_all_algorithms();

    // Création du contexte pour la génération de clés ED25519
    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (pctx == NULL) {
        LOG_ERROR("Échec de création du contexte pour ED25519");
        ret = -1;
        goto cleanup_return;
    }

    // Initialisation du contexte pour la génération de clés
    if (EVP_PKEY_keygen_init(pctx) <= 0) {
        LOG_ERROR("Échec d'initialisation du contexte de génération de clés");
        ret = -1;
        perror("keygen_init : erreur lors de l'initialisation du contexte de "
               "géneration");
        goto cleanup_return;
    }
    // Génération de la paire de clés
    if (EVP_PKEY_generate(pctx, &pkey) <= 0) {
        ret = -1;
        LOG_ERROR("Échec de génération de la paire de clés ED25519");
        perror("keygen : Erreur lors de la géneration de la paire de clés ED25519.");
        goto cleanup_return;
    }
    LOG_INFO("Paire de clés ED25519 générée avec succès");

    // Écriture de la clé privée au format PEM
    if (PEM_write_bio_PrivateKey(bio_priv, pkey, NULL, NULL, 0, NULL, NULL) != 1) {
        LOG_ERROR("Échec d'écriture de la clé privée au format PEM");
        ret = -1;
        goto cleanup_return;
    }

    // Lecture de la longueur de la clé privée
    bio_buffer_data_length = BIO_ctrl_pending(bio_priv);
    *priv_len = bio_buffer_data_length;
    LOG_INFO("Taille de la clé privée: %ld octets", bio_buffer_data_length);

    // Allocation de mémoire pour la clé privée
    *private_key = malloc(bio_buffer_data_length);
    if (*private_key == NULL) {
        LOG_ERROR("Échec d'allocation mémoire pour la clé privée");
        ret = -1;
        goto cleanup_return;
    }
    // Lecture de la clé privée depuis le BIO
    if (BIO_read(bio_priv, *private_key, bio_buffer_data_length) !=
        bio_buffer_data_length) {
        LOG_ERROR("Échec de lecture de la clé privée depuis le BIO");
        ret = -1;
        goto cleanup_return;
    }
    LOG_INFO("Clé privée ED25519 extraite avec succès");

    // Écriture de la clé publique au format PEM
    if (PEM_write_bio_PUBKEY(bio_pub, pkey) != 1) {
        LOG_ERROR("Échec d'écriture de la clé publique au format PEM");
        ret = -1;
        goto cleanup_return;
    }

    // Récupération de la taille de la clé publique
    bio_buffer_data_length = BIO_ctrl_pending(bio_pub);
    *pub_len = bio_buffer_data_length;
    LOG_INFO("Taille de la clé publique: %ld octets", bio_buffer_data_length);
    // Allocation de mémoire pour la clé publique
    *public_key = malloc(bio_buffer_data_length);

    if (*public_key == NULL) {
        LOG_ERROR("Échec d'allocation mémoire pour la clé publique");
        ret = -1;
        goto cleanup_return;
    }
    // Lecture de la clé publique depuis le BIO
    if (BIO_read(bio_pub, *public_key, bio_buffer_data_length) !=
        bio_buffer_data_length) {
        LOG_ERROR("Échec de lecture de la clé publique depuis le BIO");
        ret = -1;
        goto cleanup_return;
    }
    LOG_INFO("Clé publique ED25519 extraite avec succès \n");

cleanup_return:
    if (ret != 0) {
        LOG_INFO("Nettoyage des ressources après échec de génération des clés");
        if (*public_key != NULL) {
            free(*public_key);
            *public_key = NULL;
        }

        if (*private_key != NULL) {
            free(*private_key);
            *private_key = NULL;
        }
    }
    // Libération des ressources OpenSSL
    if (pctx != NULL) {
        EVP_PKEY_CTX_free(pctx);
    }
    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }
    if (bio_pub != NULL) {
        BIO_free(bio_pub);
    }
    if (bio_priv != NULL) {
        BIO_free(bio_priv);
    }
    return ret;
}
/**
 * Méthode permettant d'initialise les clés d'un pair.
 * Elle gènere une paire des clés ED25519 et les stocke dans la structure peer_t fournie
 */
int initialise_keys(peer_t *peer) {
    LOG_INFO("Initialisation des clés pour un pair");
    if (peer == NULL) {
        LOG_ERROR("Paramètre invalide: pointeur peer NULL");
        return -1;
    }
    int ret = 0;
    uint8_t *pub_key = NULL;
    uint8_t *priv_key = NULL;
    size_t pub_len, priv_len;

    // Génération d'une nouvelle paire de clés
    if (generate_ED25519_keys(&pub_key, &priv_key, &pub_len, &priv_len) != 0) {
        LOG_ERROR("Paramètre invalide: pointeur peer NULL");
        return -1;
    }

    if (pub_len > ED25519_PUB_KEY_LEN) {
        LOG_ERROR(
            "Taille de la clé publique plus grande que le buffer. Taille clé : %i, %s \n",
            pub_len, pub_key);
        ret = -1;
        goto cleanup_return;
    }

    if (priv_len > ED25519_PRIV_KEY_LEN) {
        LOG_ERROR("Taille de la clé privée plus grande que le buffer. Taille clé : %i \n",
                  priv_len);
        ret = -1;
        goto cleanup_return;
    }
    // Création de EVP_PKEY du paire contenant les clés à la fois publiques et privés
    if (init_peer_pkey(peer, priv_key, priv_len)) {
        LOG_INFO("EVP_PKEY pour le pair est null");
        ret = -1;
        goto cleanup_return;
    }

    LOG_INFO("Clé publique : %s ", key_without_newline(pub_key, 0));
    LOG_INFO("Cle privée : %s ", key_without_newline(priv_key, 1));

    // Copie en mémoire de la clé
    memcpy(peer->public_key.key, pub_key, pub_len);

cleanup_return:
    if (pub_key != NULL) {
        free(pub_key);
    }
    if (priv_key != NULL) {
        free(priv_key);
    }
    return ret;
}

void print_signature_hex(const uint8_t *data, size_t length) {
    safe_printf("Signature (hex): ");
    for (size_t i = 0; i < length; i++) {
        safe_printf("%02x", data[i]);
    }
    safe_printf("\n");
}

/**
 * Méthode permettant de convertir une clé au format PEM (représentée en chaîne de
 * caractères) en structure EVP_PKEY utilisable par les fonctions cryptographiques
 * d'OpenSSL.
 * @param[in]  key       pointeur vers la chaîne contenant la clé au format PEM
 * @param[in]  key_len       la longueur de la clé
 * @param[in] is_private 1 si il s'agit d'une clé privée, 0 si c'est une clé publique
 * @param[out] pkey     pointeur qui recevra la structure EVP_PKEY
 */
int char_to_evp_pkey(uint8_t **key, size_t key_len, int is_private, EVP_PKEY **pkey) {
    LOG_INFO("Conversion d'une clé PEM en structure EVP_PKEY");

    if (*key == NULL || key == NULL || pkey == NULL) {
        LOG_ERROR("Paramètres invalides: pointeurs NULL fournis");
        if (pkey == NULL) {
            LOG_ERROR("Pkey is null");
        }
        return -1;
    }
    BIO *bio = NULL;

    // Création d'un BIO buffer pour lire la clé de type chaîne
    bio = BIO_new_mem_buf(*key, key_len);
    if (bio == NULL) {
        LOG_ERROR("Échec de création du BIO");
        perror("BIO_new_mem_buf");
        return -1;
    }
    if (is_private == 0) {
        LOG_INFO("Lecture d'une clé publique PEM");
        *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    } else if (is_private == 1) {
        LOG_INFO("Lecture d'une clé privée PEM");
        *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    } else {
        LOG_ERROR(
            "La variable key_type doit être soit 0 pour une clé publique soit 1 pour "
            "une clé privée");
        return -1;
    }

    if (pkey == NULL) {
        LOG_ERROR("Échec de lecture de la clé");
        return -1;
    }

    LOG_INFO("Clé chargée avec succès");

    if (bio != NULL) {
        BIO_free(bio);
    }
    return 0;
}

/**
 * Méthode permettant de signer un message avec la clé priveé ED25519 fournie.
 * @param[in]  private_key Pointeur vers la clé privée ED25519
 * @param[in]  message     Pointeur vers le message à signer
 * @param[out] signature   Pointeur vers la structure qui recevra la signature
 */
int sign_message_with_key(EVP_PKEY *private_key, mess_t *message, sign_t *signature) {
    LOG_INFO("Signature d'un message avec ED25519");
    if (private_key == NULL || message == NULL || signature == NULL) {
        LOG_ERROR("Paramètres invalides: clé privée, message ou signature NULL");
        perror(" clé privée, message ou signature NULL");
        return -1;
    }
    // Validation supplémentaire des données du message
    if (message->data == NULL || message->length == 0) {
        LOG_ERROR("Message invalide: données NULL ou longueur zéro");
        return -1;
    }

    int ret = 0;
    // Initialisation des champs de la signature
    signature->data = NULL;
    signature->length = 0;

    // Création du contexte de digest pour la signature
    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
    if (mdctx == NULL) {
        LOG_ERROR("Échec de création du contexte de signature");
        perror("EVP_MD_CTX_create");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    // Initialisation du contexte avec la clé privée
    if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, private_key) != 1) {
        LOG_ERROR("Échec d'initialisation du contexte de signature");
        ERR_print_errors_fp(stderr);
        ret = -1;
        goto cleanup_return;
    }

    // Détermination de la taille nécessaire pour la signature
    size_t sig_len = 0;
    if (EVP_DigestSign(mdctx, NULL, &sig_len, message->data, message->length) != 1) {
        LOG_ERROR("Échec de détermination de la taille de signature");
        ERR_print_errors_fp(stderr);
        ret = -1;
        goto cleanup_return;
    }
    LOG_INFO("Taille de signature requise: %zu octets", sig_len);
    // Allouer l'espace pour les données de signature
    signature->data = malloc(sig_len);
    if (signature->data == NULL) {
        LOG_ERROR("Échec d'allocation mémoire pour les données de signature");
        perror("malloc");
        ret = -1;
        goto cleanup_return;
    }
    // Génération de la signature
    if (EVP_DigestSign(mdctx, signature->data, &sig_len, message->data,
                       message->length) != 1) {
        LOG_ERROR("Échec de génération de la signature");
        ERR_print_errors_fp(stderr);
        ret = -1;
        goto cleanup_return;
    }

    signature->length = (lsign_t)sig_len;
    LOG_INFO("Signature générée avec succès (taille: %zu octets)", sig_len);
    DEBUG_CALL(print_signature_hex(signature->data, signature->length));

cleanup_return:
    // Libération des ressources
    if (mdctx != NULL)
        EVP_MD_CTX_free(mdctx);
    if (ret == -1 && signature->data != NULL) {
        free(signature->data);
        signature->data = NULL;
        signature->length = 0;
    }
    return ret;
}
/**
 * Méthode permettant de verifier une signature avec une clé publique ED25519
 */
int verify_signature_with_key(EVP_PKEY *public_key, const mess_t *message,
                              const sign_t *signature) {
    LOG_INFO("Vérification d'une signature");
    if (public_key == NULL || message == NULL || signature == NULL) {
        LOG_ERROR("Paramètres invalides: clé publique, message ou signature NULL");
        perror("Au moins un des 3 champs est NULL \n");
        return -1;
    }
    // Validation supplémentaire des données
    if (message->data == NULL || message->length == 0) {
        LOG_ERROR("Message invalide: données NULL ou longueur zéro");
        return -1;
    }

    if (signature->data == NULL || signature->length == 0) {
        LOG_ERROR("Signature invalide: données NULL ou longueur zéro");
        return -1;
    }
    // Création du contexte de digest pour la vérification
    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
    if (mdctx == NULL) {
        LOG_ERROR("Échec de création du contexte de vérification");
        perror("EVP_MD_CTX_create");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    // Initialisation du contexte avec la clé publique
    if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, public_key) != 1) {
        LOG_ERROR("Échec d'initialisation du contexte de vérification");
        EVP_MD_CTX_free(mdctx);
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // Vérification de la signature
    int result = EVP_DigestVerify(mdctx, signature->data, signature->length,
                                  message->data, message->length);
    EVP_MD_CTX_free(mdctx);

    if (result == 1) {
        LOG_INFO("Vérification de signature réussie: signature valide");
        return 0; // Signature valide
    } else if (result == 0) {
        LOG_WARNING("Vérification de signature réussie: signature invalide");
        return 1; // Signature invalide mais processus valide
    } else {
        LOG_ERROR("Erreur technique lors de la vérification de la signature");
        ERR_print_errors_fp(stderr);
        return -1; // Erreur
    }
}

/**
 * Permet d'initialiser EVP_PKEY du paire
 */
int init_peer_pkey(peer_t *peer, uint8_t *priv_key, size_t key_len) {
    // Initialisation de EVP_PKEY
    // uint8_t *priv_key_str = peer->private_key.key;
    if (char_to_evp_pkey(&priv_key, key_len, 1, &peer->tls.pkey) != 0) {
        LOG_ERROR("Could not generate evp_pkey");
        return -1;
    }
    return 0;
}

/**
 * Méthode permettant d'initaliser le contexte tls
 */
int init_tls_context(peer_t *peer, int is_server) {
    const SSL_METHOD *method = is_server ? TLS_server_method() : TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        LOG_ERROR("Échec création SSL_CTX");
        return -1;
    }
    if (is_server) {
        peer->tls.ssl_ctx_server = ctx;
    } else {
        peer->tls.ssl_ctx_client = ctx;
    }

    // Vérifier que le certificat et la clé existent
    if (!peer->tls.certificate || !peer->tls.pkey) {
        LOG_ERROR("Certificat ou clé manquants");
        if (peer->tls.pkey)
            LOG_ERROR("EVPPKEY missig");
        LOG_ERROR("Certificate missing");
        SSL_CTX_free(ctx);
        return -1;
    }

    // Configurer le certificat et la clé pour les deux modes (client et serveur)
    if (!SSL_CTX_use_certificate(ctx, peer->tls.certificate)) {
        LOG_ERROR("Échec chargement certificat");
        SSL_CTX_free(ctx);
        return -1;
    }

    if (!SSL_CTX_use_PrivateKey(ctx, peer->tls.pkey)) {
        LOG_ERROR("Échec chargement clé privée");
        SSL_CTX_free(ctx);
        return -1;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        LOG_ERROR("Clé privée et certificat incompatibles");
        SSL_CTX_free(ctx);
        return -1;
    }

    // Verification de certificat requise
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       verification_callback);

    return 0;
}

/**
 * Callback lors de la vérification d'un certificat TLS afin de permettre les certificats
 * auto-signés
 */
int verification_callback(int preverify_ok, X509_STORE_CTX *ctx) {
    int err = X509_STORE_CTX_get_error(ctx);

    if (preverify_ok)
        return 1;
    // Accepter les certificats auto-signés
    if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
        err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN) {
        LOG_INFO("Certificat auto-signé : accepté");
        return 1;
    }
    LOG_ERROR("Erreur survenue lors de la vérification de certificat : %i ", err);
    return 0;
}

/**
 * Méthode permettant de créer le certificat d'un paire
 */
int create_certificate(peer_t *peer) {
    X509 *certificate = X509_new();
    if (certificate == NULL) {
        LOG_ERROR("Could not generate X509 certificate");
        return -1;
    }
    peer->tls.certificate = certificate;

    // Version X509v3
    X509_set_version(certificate, 2);

    // Numéro de série dérivé de l'horodatage
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), time(NULL));

    // Valable 24h à partir de la creation
    X509_gmtime_adj(X509_get_notBefore(certificate), 0);
    X509_gmtime_adj(X509_get_notAfter(certificate), 60 * 60 * 24);

    // Auto-signer le certificat
    unsigned char raw_pubkey[32];
    size_t rawlen = sizeof(raw_pubkey);
    if (EVP_PKEY_get_raw_public_key(peer->tls.pkey, raw_pubkey, &rawlen) <=
        0) { // Récuperation clé publique brute
        LOG_ERROR("Echec de récupéreration de la clé publique brute");
        return -1;
    }
    // Récuperer 8 octets de la clé publique pour identification plus facile
    char pubkey_hex[17];
    for (int i = 0; i < 8; i++) {
        sprintf(&pubkey_hex[i * 2], "%02x", raw_pubkey[i]);
    }
    pubkey_hex[16] = '\0';

    // Rempli les champs Issuer, Subject
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "AuctionP2P-1-Peer-%s", pubkey_hex);

    X509_NAME *name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)buffer, -1, -1,
                               0);
    X509_set_subject_name(certificate, name);
    X509_set_issuer_name(certificate, name);
    X509_NAME_free(name);

    // Associer clé publique et signer avec clé privée
    if (peer->tls.pkey == NULL) {
        LOG_ERROR("NULL PKEY");
    }
    X509_set_pubkey(certificate, peer->tls.pkey);
    X509_sign(certificate, peer->tls.pkey, NULL);

    return 0;
}

/**
 * Méthode permettant la coversion d'une clé EVP_PKEY en un tableau uint8_t
 */
int evp_pkey_to_char(EVP_PKEY *pkey, int is_private, uint8_t **buffer) {
    if (!pkey || !buffer) {
        LOG_ERROR("Paramètres invalides");
        return -1;
    }
    // Création d'un BIO buffer
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        LOG_ERROR("Erreur de création du BIO");
        return -1;
    }

    // Écrire la clé dans le BIO
    int success = 0;
    if (is_private) {
        success = PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL);
    } else {
        success = PEM_write_bio_PUBKEY(bio, pkey);
    }

    if (!success) {
        LOG_ERROR("Erreur d'écriture de la clé dans le BIO buffer");
        BIO_free(bio);
        return -1;
    }

    // Récupèrer la taille de la clé
    int length = BIO_pending(bio);
    if (length <= 0) {
        LOG_ERROR("Aucune donnée dans le BIO");
        BIO_free(bio);
        return -1;
    }

    // Allouer de la mémoire pour les données
    *buffer = malloc(length);
    if (*buffer == NULL) {
        LOG_ERROR("Erreur d'allocation du buffer uint8_t pour la clé");
        BIO_free(bio);
        return -1;
    }

    // Lecture des donées du BIO buffer dans le buffer uint8_t
    int bytes_read = BIO_read(bio, *buffer, length);
    if (bytes_read != length) {
        LOG_ERROR("Erreur de lecture dans le BIO");
        free(*buffer);
        BIO_free(bio);
        return -1;
    }

    BIO_free(bio);
    return 0;
}

/**
 * Méthode permettant de vérifier un certificat
 */
int verify_certificate(SSL *ssl, cle_pub_t *pub_key) {
    if (ssl == NULL) {
        LOG_ERROR("Échec de vérification SSL : le pointeur SSL est NULL");
        return -1;
    }
    if (pub_key == NULL) {
        LOG_ERROR("pub_key is null");
        return -1;
    }

    // Récuperer le certificat
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (cert == NULL) {
        LOG_ERROR("Erreur lors de la récuperation du certificat");
        return -1;
    }
    // Récuperer la clé publique du certificat
    EVP_PKEY *real_pubkey = X509_get_pubkey(cert);
    if (real_pubkey == NULL) {
        LOG_ERROR("Real pubkey is null from tls ");
        return -1;
    }
    // Conversion clé uint8_t en EVP_PKEY pour la comparaison
    EVP_PKEY *expected_pubkey;
    uint8_t *key_ptr = pub_key->key;
    if (char_to_evp_pkey(&key_ptr, ED25519_PUB_KEY_LEN, 0, &expected_pubkey) != 0) {
        LOG_ERROR("Echec de conversion en EVP_PKEY");
        return -1;
    }

    // Affichage des clés pour debug uniquement
    uint8_t *print_realpub = NULL;
    uint8_t *print_expectedKey = NULL;

    int ret_val = 0;
    int key_comparison = EVP_PKEY_eq(real_pubkey, expected_pubkey);
    if (key_comparison == 0) {
        ret_val = 1;
        if (evp_pkey_to_char(real_pubkey, 0, &print_realpub) != 0) {
            LOG_ERROR("Erreur de conversion en uint8_t de la clé du certificat");
            ret_val = -1;
        }
        LOG_INFO("Clé publique certificat : %s \n", print_realpub);
        LOG_INFO("Clé publique paire supérviseur enchère : %s", print_expectedKey);
        goto cleanup_return;
    } else if (key_comparison < 0) {
        LOG_ERROR("Erreur lors de la comparaison des clés");
        ret_val = -1;
        if (real_pubkey == NULL) {
            LOG_ERROR("Clé réele du certificat est null");
        }
        if (expected_pubkey == NULL) {
            LOG_ERROR("Clé attendue est null");
        }
        goto cleanup_return;
    }
    LOG_INFO("Certificat vérifié avec succès");

cleanup_return:
    if (print_realpub != NULL) {
        free(print_realpub);
    }
    if (print_expectedKey != NULL) {
        free(print_expectedKey);
    }
    if (real_pubkey != NULL) {
        EVP_PKEY_free(real_pubkey);
    }
    if (expected_pubkey != NULL) {
        EVP_PKEY_free(expected_pubkey);
    }
    return ret_val;
}

//------------------------- PRINT -----------------------------//

int print_x509_certificate(X509 *certificate) {
    BIO *bio = BIO_new(BIO_s_mem());
    X509_print(bio, certificate);

    char *data;
    long len = BIO_get_mem_data(bio, &data);
    char *tmp = strndup(data, len);

    safe_printf("Certificat :\n%s", tmp);

    free(tmp);
    BIO_free(bio);
    return 0;
}

char *x509_to_human_readable(X509 *cert) {
    BIO *bio_out = BIO_new(BIO_s_mem());
    if (!bio_out) {
        return NULL;
    }
    // Mettre les données dans un BIO buffer
    X509_print(bio_out, cert);

    // Récupérer les données du BIO
    char *data = NULL;
    long data_len = BIO_get_mem_data(bio_out, &data);

    // Copier les données dans un buffer avec terminaison nulle
    char *result = malloc(data_len + 1);
    if (result) {
        memcpy(result, data, data_len);
        result[data_len] = '\0';
    }

    BIO_free(bio_out);
    return result;
}
