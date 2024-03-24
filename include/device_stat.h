//
// Created by otavio on 23/03/24.
//

#ifndef NETWORK_LOG_DEVICE_STAT_H
#define NETWORK_LOG_DEVICE_STAT_H

#include <stdint.h>
#include <netinet/in.h>
#include <stddef.h>

struct device_stat {
    struct in_addr ip;
    uint64_t total_data;
};

struct network_node {
    struct device_stat own;
    struct device_stat *peers;
    size_t peers_length;
    size_t data_accumulator;
    float avg_speed;
    struct timespec accu_start;
};

int device_stat_init(struct network_node *nodes);
int device_stat_parse_line(struct network_node **nodes, size_t *length, char *line);

#endif //NETWORK_LOG_DEVICE_STAT_H
