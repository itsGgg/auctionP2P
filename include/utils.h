#ifndef UTILS_H
#define UTILS_H

#include "../lib/config.h"
#include "../lib/format.h"

#include <stdint.h>
#include <stdlib.h>

// ------------------------------ Fonctions de utils ------------------------------ //
int write_output(const char *message);
int write_output_no_ln(const char *message);
int write_err(const char *message);
int write_err_no_ln(const char *message);

void print_hex(const uint8_t *data, size_t len);
void print_ipv6(const uint8_t ip[IPV6_ADDR_LEN]);
void hex_dump(const void *data, size_t len);
const char *public_key_to_str_static(const cle_pub_t *key);
int uint8_to_str_alloc(const uint8_t *data, size_t data_len, char **out_str);
const char *key_without_newline(const uint8_t *key, int key_type);

char *get_current_directory();
char *get_executing_directory();
int create_specific_directory(char *name);
char *build_path(char *path, char *directory_name);
int get_network_interfaces(char **interfaces, int max_count);

// ------------------------------ Fonctions de debug ------------------------------ //

#endif