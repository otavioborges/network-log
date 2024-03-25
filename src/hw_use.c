//
// Created by otavio on 25/03/24.
//
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define CPU_USAGE_AVG_LENGTH   8

static pthread_t hw_use_task;
static int _continue = 0;
static float _cpu_usage[CPU_USAGE_AVG_LENGTH];
static int _cpu_usage_idx = 0;

static void *hw_use_thread(void *arg);

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

static void *hw_use_thread(void *arg) {
    int *cont = (int *)arg;
    FILE *fd;
    char *line = NULL;
    size_t line_length = 0;
    long long user, nice, system, idle, iowait, irq, softirq;
    long long total;

    while (*cont) {
        sleep(1);
        fd = fopen("/proc/stat", "r");
        if (fd == NULL) {
            fprintf(stderr, "Unable to open CPU status file. Reason: %s (%d)\n", strerror(errno), errno);
            continue;
        }

        if (getline(&line, &line_length, fd) < 0) {
            fprintf(stderr, "Error parsing stat file line. Reason: %s (%d)\n",
                    strerror(errno), errno);
            continue;
        }
        fclose(fd);

        sscanf(line, "%*s %lld %lld %lld %lld %lld %lld %lld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
        total = (user + nice + system + idle + iowait + irq + softirq);
        _cpu_usage[_cpu_usage_idx] = 100.0f - ((idle * 100.0f) / (float)total);
        if ((++_cpu_usage_idx) > CPU_USAGE_AVG_LENGTH)
            _cpu_usage_idx = 0;
    }

    pthread_exit(NULL);
}