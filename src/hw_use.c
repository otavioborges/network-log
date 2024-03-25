//
// Created by otavio on 25/03/24.
//
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "hw_use.h"

#define CPU_USAGE_AVG_LENGTH   8

static pthread_t hw_use_task;
static int _continue = 0;
static float _cpu_usage[CPU_USAGE_AVG_LENGTH];
static int _cpu_usage_idx = 0;

static int64_t _total_ram = 0;
static int64_t _free_ram = 0;

static void *hw_use_thread(void *arg);
static int64_t apply_multiplier(int64_t value, char *multiplier);
static int monitor_cpu_usage(void);
static int monitor_mem_free(void);

int hw_use_init(void) {
    int rtn;

    _continue = 1;
    rtn = pthread_create(&hw_use_task, NULL, hw_use_thread, &_continue);
    if (rtn) {
        fprintf(stderr, "Error creating HW Usage Thread. Reason: %s (%d)\n", strerror(errno), errno);
        _continue = 0;
        return -1;
    }

    return 0;
}

void hw_use_terminate(void) {
    if (_continue) {
        _continue = 0;
        pthread_join(hw_use_task, NULL);
    }
}

float hw_use_current_cpu_usage(void) {
    int idx;
    float sum = 0;

    for (idx = 0; idx < CPU_USAGE_AVG_LENGTH; idx++)
        sum += _cpu_usage[idx];

    return (sum / (float)CPU_USAGE_AVG_LENGTH);
}

void hw_use_system_ram(int64_t *total, int64_t *in_use) {
    *total = _total_ram;
    *in_use = (_total_ram - _free_ram);
}

static void *hw_use_thread(void *arg) {
    int *cont = (int *)arg;
    FILE *fd;
    char *line = NULL;
    size_t line_length = 0;


    while (*cont) {
        sleep(1);
        (void)monitor_cpu_usage();
        (void)monitor_mem_free();
    }

    pthread_exit(NULL);
}

static int64_t apply_multiplier(int64_t value, char *multiplier) {
    if (strcmp(multiplier, "B") == 0) {
        return value;
    } else if (strcmp(multiplier, "kB") == 0) {
        return value * 1024;
    } else if (strcmp(multiplier, "mB") == 0) {
        return value * 1024 * 1024;
    } else {
        return value;
    }
}

static int monitor_cpu_usage(void) {
    FILE *fd;
    static char *line = NULL;
    static size_t line_length = 0;
    long long user, nice, system, idle, iowait, irq, softirq;
    long long total;

    fd = fopen("/proc/stat", "r");
    if (fd == NULL) {
        fprintf(stderr, "Unable to open CPU status file. Reason: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    if (getline(&line, &line_length, fd) < 0) {
        fprintf(stderr, "Error parsing stat file line. Reason: %s (%d)\n",
                strerror(errno), errno);
        return -1;
    }
    fclose(fd);

    sscanf(line, "%*s %lld %lld %lld %lld %lld %lld %lld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    total = (user + nice + system + idle + iowait + irq + softirq);
    _cpu_usage[_cpu_usage_idx] = 100.0f - ((idle * 100.0f) / (float)total);
    if ((++_cpu_usage_idx) > CPU_USAGE_AVG_LENGTH)
        _cpu_usage_idx = 0;

    return 0;
}

static int monitor_mem_free(void) {
    FILE *fd;
    static char *line = NULL;
    static size_t line_length = 0;
    char field[256], value[256], mult[128];

    fd = fopen("/proc/meminfo", "r");
    if (fd == NULL) {
        fprintf(stderr, "Unable to open meminfo file. Reason: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    while (getline(&line, &line_length, fd) > 0) {
        sscanf(line, "%s %s %s", field, value, mult);
        if (strcmp(field, "MemTotal:") == 0) {
            _total_ram = (int64_t)strtol(value, NULL, 10);
            _total_ram = apply_multiplier(_total_ram, mult);
        } else if (strcmp(field, "MemAvailable:") == 0) {
            _free_ram = (int64_t)strtol(value, NULL, 10);
            _free_ram = apply_multiplier(_free_ram, mult);
        }

    }
    fclose(fd);

    return 0;
}