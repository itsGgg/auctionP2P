#ifndef PEER_ID_H
#define PEER_ID_H

#include "../include/peer_list.h"

#define ID_RETRY_MAX 10

bool is_id_in_use(const peer_list_t *list, app_id_t id);
app_id_t generate_id(const peer_list_t *peer_list);

#endif