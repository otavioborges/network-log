//
// Created by otavio on 23/03/24.
//

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "device_stat.h"

#define SECONDS_FOR_SPEED_CALC   1

static uint64_t _total_data_traffic = 0;
static struct timespec _total_ellapsed = {0};
static float _total_speed = 0;

static struct network_node *search_list(struct network_node *nodes, size_t length, struct in_addr target_ip);
static struct device_stat *search_device(struct device_stat *devs, size_t length, struct in_addr target_ip);

int device_stat_parse_line(struct network_node **nodes, size_t *length, char *line) {
    int rtn = 0;
    struct in_addr sender, rcv;
    size_t pkt_length = 0;
    char *token = NULL, *cline = NULL;
    struct network_node *own_node = NULL;
    struct device_stat *destination = NULL;
    struct timespec now;
    float delta_s;

    // 2024-03-23T16:17:32.028470+00:00 mr-fishoeder kernel: [316721.158546] [IPTABLES]:IN=enp6s0f1 OUT=enp6s0f0 MAC=a0:36:9f:09:4b:45:16:47:f7:c4:19:ed:08:00 SRC=10.20.0.32 DST=74.125.195.188 LEN=52 TOS=0x00 PREC=0x00 TTL=63 ID=53887 DF PROTO=TCP SPT=59428 DPT=5228 WINDOW=661 RES=0x00 ACK URGP=0
    clock_gettime(CLOCK_MONOTONIC, &now);
    cline = strdup(line);
    token = strtok(cline, " ");
    while (token) {
        if (strncmp(token, "SRC=", 4) == 0) {
            if (inet_aton((token + 4), &sender) == 0) {
                fprintf(stderr, "Unable to parse \'%s\' as an IP address.\n", token);
                rtn = -1;
                goto terminate;
            }
        } else if (strncmp(token, "DST=", 4) == 0) {
            if (inet_aton((token + 4), &rcv) == 0) {
                fprintf(stderr, "Unable to parse \'%s\' as an IP address.\n", token);
                rtn = -2;
                goto terminate;
            }
        } else if (strncmp(token, "LEN=", 4) == 0) {
            if (pkt_length == 0)
                pkt_length = (size_t)atol((token + 4));
        }

        token = strtok(NULL, " ");
    }

    if (_total_ellapsed.tv_sec == 0) {
        memcpy(&_total_ellapsed, &now, sizeof(struct timespec));
        _total_data_traffic = pkt_length;
    } else {
        _total_data_traffic += pkt_length;
        if ((now.tv_sec - _total_ellapsed.tv_sec) >= SECONDS_FOR_SPEED_CALC) {
            delta_s = (float)(now.tv_sec - _total_ellapsed.tv_sec);
            delta_s += ((float)(now.tv_nsec - _total_ellapsed.tv_nsec)) / 1000000000.0f;
            _total_speed = (float)_total_data_traffic / delta_s;
            _total_data_traffic = 0;
            memcpy(&_total_ellapsed, &now, sizeof(struct timespec));

            printf("Avg. Total Speed: %.3f\n", _total_speed);
        }
    }

    own_node = search_list(*nodes, *length, sender);
    if (own_node == NULL) {
        /* flag that list is updated */
        rtn = 1;
        *nodes = (struct network_node *) realloc(*nodes, sizeof(struct network_node) * ((*length) + 1));
        own_node = (*nodes) + (*length);
        (*length)++;

        if ((*nodes) == NULL) {
            fprintf(stderr, "Error appending new Network node \'%s\'. Reason: %s (%d)\n",
                    inet_ntoa(sender), strerror(errno), errno);

            rtn = -3;
            goto terminate;
        }

        own_node->own.ip.s_addr = sender.s_addr;
        own_node->own.total_data = pkt_length;
        own_node->peers = (struct device_stat *) malloc(sizeof(struct device_stat));
        own_node->peers_length = 1;
        own_node->data_accumulator = pkt_length;
        memcpy(&own_node->accu_start, &now, sizeof(struct timespec));

        destination = own_node->peers;
        destination->total_data = 0;
    } else {
        own_node->own.total_data += pkt_length;
        own_node->data_accumulator += pkt_length;
        destination = search_device(own_node->peers, own_node->peers_length, rcv);
        if (destination == NULL) {
            /* flag that list is updated */
            rtn = 1;
            own_node->peers = (struct device_stat *) realloc(own_node->peers, sizeof(struct device_stat) * (own_node->peers_length + 1));
            if ((own_node->peers) == NULL) {
                fprintf(stderr, "Error appending new Destination \'%s\' to node \'%s\'. Reason: %s (%d)\n",
                        inet_ntoa(rcv), inet_ntoa(sender), strerror(errno), errno);

                rtn = -4;
                goto terminate;
            }
            destination = (own_node->peers + own_node->peers_length);
            destination->total_data = 0;
            own_node->peers_length++;
        }

        if ((now.tv_sec - own_node->accu_start.tv_sec) >= SECONDS_FOR_SPEED_CALC) {
            delta_s = (float)(now.tv_sec - own_node->accu_start.tv_sec);
            delta_s += ((float)(now.tv_nsec - own_node->accu_start.tv_nsec)) / 1000000000.0f;
            own_node->avg_speed = (float)own_node->data_accumulator / delta_s;
            own_node->data_accumulator = 0;
            memcpy(&own_node->accu_start, &now, sizeof(struct timespec));
        }
    }

    destination->total_data += pkt_length;
    destination->ip = rcv;

terminate:
    if (cline)
        free(cline);

    return rtn;
}

float device_stat_net_speed(void) {
    return _total_speed;
}

static struct network_node *search_list(struct network_node *nodes, size_t length, struct in_addr target_ip) {
    int idx;

    for (idx = 0; idx < length; idx++) {
        if (nodes[idx].own.ip.s_addr == target_ip.s_addr)
            return (nodes + idx);
    }

    /* Not on list */
    return NULL;
}

static struct device_stat *search_device(struct device_stat *devs, size_t length, struct in_addr target_ip) {
    int idx;

    for (idx = 0; idx < length; idx++) {
        if (devs[idx].ip.s_addr == target_ip.s_addr)
            return (devs + idx);
    }

    /* Not on list */
    return NULL;
}
