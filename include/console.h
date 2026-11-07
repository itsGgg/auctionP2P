#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stdlib.h>

#include <signal.h>
#include <stdatomic.h>

extern atomic_bool should_exit;

// Forward declaration
typedef struct peer_t peer_t;

// #define MAX_CMD_LENGTH 256

typedef enum user_location_t user_location_t;
typedef enum lobby_command_t lobby_command_t;
typedef enum edit_auction_command_t edit_auction_command_t;
typedef enum auction_command_t auction_command_t;

enum user_location_t {
    LOCATION_LIAISON,
    LOCATION_LOBBY,
    LOCATION_AUCTION
};

void set_prompt_context(peer_t *ctx);
peer_t *get_prompt_context();
char *get_prompt();

int tokenize(const char *line, char ***tokens_out);
int free_tokens(char **tokens);
int print_tokens(char **tokens);
size_t get_tokens_count(char **tokens);

void print_starting_message();
void print_welcome_help_message();

char *location_to_str(user_location_t location);
int str_to_uint32(const char *str, uint32_t *res);

int update_location(peer_t *peer, user_location_t location);

void handle_prompt(char *line);

#endif