//
// Created by otavio on 23/03/24.
//

#ifndef NETWORK_LOG_HTTP_H
#define NETWORK_LOG_HTTP_H

#include <pthread.h>
#include "device_stat.h"

//extern pthread_mutex_t http_network_list_lock;

int http_init(unsigned short port, char *http_file_path);
void http_end(void);
void http_update_network_list(struct network_node *nodes, size_t length);

#endif //NETWORK_LOG_HTTP_H
