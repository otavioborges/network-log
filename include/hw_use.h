//
// Created by otavio on 25/03/24.
//

#ifndef NETWORK_LOG_HW_USE_H
#define NETWORK_LOG_HW_USE_H

#include <stdint.h>

int hw_use_init(void);
void hw_use_terminate(void);
float hw_use_current_cpu_usage(void);
void hw_use_system_ram(int64_t *total, int64_t *in_use);

#endif //NETWORK_LOG_HW_USE_H
