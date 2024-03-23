//
// Created by otavio on 23/03/24.
//

#ifndef NETWORK_LOG_OPTIONS_H
#define NETWORK_LOG_OPTIONS_H

#include <getopt.h>

struct option_with_description {
    struct option _opt;
    const char *arg_name;
    const char *description;
};

#endif //NETWORK_LOG_OPTIONS_H
