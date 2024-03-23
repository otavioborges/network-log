#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "options.h"
#include "config.h"
#include "device_stat.h"

#define BUFFER_LENGTH    2048
#define POLL_TIMEO_US    100000
#define PID_FILE         "./network-log.pid"
//#define PID_FILE         "/var/run/network-log.pid"

static void sig_handler(int signo);
static int print_help(int rtn, const char *argv0, char *msg, ...);

static int _continue = 1;
static const struct option_with_description _program_args[] = {
        {{"help", no_argument, NULL, 'h'}, NULL, "Show this message"},
        {{"version", no_argument, NULL, 'v'}, NULL, "Show application version"},
        {{"input_log", required_argument, NULL, 'i'}, "log file", "Iptables generated logs with package details"},
        {{"background", no_argument, NULL, 'b'}, NULL, "A daemon will be created at background and parent will return."},
};
static size_t _args_length = sizeof(_program_args) / sizeof(struct option_with_description);

int main (int argc, char **argv) {
    int idx, lopt, c = 0, background = 0, rtn = 0;
    struct option *_gen_opts = NULL;
    char *input_file = NULL;
    pid_t pid;
    FILE *h_pid, *h_log;
    char *read_buffer = NULL;
    size_t line_length = 0;
    ssize_t rtn_length;
    long log_current_offset = 0, log_total_size;
    struct network_node *net_devices = NULL;
    size_t net_dev_count = 0;

    /* Mount long options array */
    _gen_opts = (struct option *) malloc(sizeof(struct option) * _args_length);
    for (idx = 0; idx < _args_length; idx++)
        _gen_opts[idx] = _program_args[idx]._opt;

    while (c >= 0) {
        c = getopt_long(argc, argv, "hvi:", _gen_opts, &lopt);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                return print_help(0, argv[0], NULL);
            case 'v':
                printf("%s - v%s\n", PACKAGE_NAME, PACKAGE_VERSION);
                return 0;
            case 'i':
                if (optarg == NULL)
                    return print_help(-1, argv[0],
                                      "Argument \'%s\' requires an argument.\n", _gen_opts[lopt].name);
                else
                    input_file = strdup(optarg);
                break;
            case 'b':
                background = 1;
                break;
            case '?':
                break;
            default:
                return print_help(-1, argv[0], "Unknown argument\n");
        }
    }

    free(_gen_opts);
    _gen_opts = NULL;

    if (input_file == NULL)
        return print_help(-1, argv[0], "Missing mandatory argument \'%s\'\n", _program_args[2]._opt.name);

    if (background) {
        printf("Instantiating daemon...\n");
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Error creating child process. Reason: %s (%d)\n", strerror(errno), errno);
            return -1;
        } else if (pid) {
            /* parent */
            h_pid = fopen(PID_FILE, "w");
            if (h_pid == NULL) {
                fprintf(stderr, "Unable to create PID file, killing child and returning. Reason: %s (%d).\n",
                        strerror(errno), errno);

                kill(pid, SIGKILL);
                return -1;
            }

            fprintf(h_pid, "%d", pid);
            fclose(h_pid);

            printf("Daemon created successfully with PID \'%d\'\n", pid);
            return 0;
        }
    }

    /* We are either foreground or daemon */
    printf("Trying to open log file \'%s\'...\n", input_file);
    rtn = open(input_file, O_RDONLY | O_NONBLOCK);
    if (rtn < 0) {
        fprintf(stderr, "Unable to open file \'%s\'. Reason: %s (%d)\n", input_file, strerror(errno), errno);
        goto terminate;
    }

    h_log = fdopen(rtn, "r");
    if (h_log == NULL) {
        fprintf(stderr, "Unable to access \'%s\' as a stream. Reason: %s (%d)\n", input_file, strerror(errno), errno);
        rtn = -1;
        goto terminate;
    }

    signal(SIGINT, sig_handler);
    while(_continue) {
        fseek(h_log, 0, SEEK_END);
        log_total_size = ftell(h_log);
        fseek(h_log, log_current_offset, SEEK_SET);

        if (log_current_offset < log_total_size) {
            while ((rtn_length = getline(&read_buffer, &line_length, h_log)) >= 0) {
                log_current_offset += rtn_length;
                /* Process a new line on file */
                rtn = device_stat_parse_line(&net_devices, &net_dev_count, read_buffer);
                if (rtn <= -3) {
                    fprintf(stderr, "Corrupted network device list. Terminating...\n");
                    _continue = 0;
                    break;
                }
            }
        } else {
            usleep(POLL_TIMEO_US);
        }
    }

    int jdx;
    if (net_devices) {
        for (idx = 0; idx < net_dev_count; idx++) {
            printf("Network dev.: %s (total data %ldB):\n",
                   inet_ntoa(net_devices[idx].own.ip), net_devices[idx].own.total_data);

            for (jdx = 0; jdx < net_devices[idx].peers_length; jdx++) {
                printf("\tPeer: %s (total data: %ldB)\n",
                       inet_ntoa(net_devices[idx].peers[jdx].ip), net_devices[idx].peers[jdx].total_data);
            }
        }
    }

    printf("Shutting down...\n");
    fclose(h_log);

    if (read_buffer) {
        free(read_buffer);
        read_buffer = NULL;
        line_length = 0;
    }

terminate:
    if (background) {
        /* termination on a daemon. do a clean job */
        if (access(PID_FILE, F_OK) == 0) {
            unlink(PID_FILE);
        }
    }

    printf("Gracefully terminated application.\n");
	return rtn;
}

static void sig_handler(int signo) {
    if (signo == SIGINT) {
        if (_continue == 0) {
            fprintf(stderr, "User requested forced termination...\n");
            exit(-1);
        } else {
            printf("Ctrl+C pressed. Gracefully terminating...\n");
            _continue = 0;
        }
    }
}

static int print_help(int rtn, const char *argv0, char *msg, ...) {
    va_list va;
    int idx;

    if (msg) {
        fprintf(stderr, "Error: ");
        va_start(va, msg);
        vfprintf(stderr, msg, va);
        va_end(va);
    }

    printf("%s - v%s\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Usage: %s ", argv0);
    for (idx = 0; idx < _args_length; idx++) {
        printf("[-%c|--%s", _program_args[idx]._opt.val, _program_args[idx]._opt.name);
        if (_program_args[idx]._opt.has_arg == required_argument)
            printf(" \'%s\'] ", _program_args[idx].arg_name);
        else
            printf("] ");
    }

    printf("\n");
    for (idx = 0; idx < _args_length; idx++) {
        printf("\t-%c|--%s: %s\n",
               _program_args[idx]._opt.val, _program_args[idx]._opt.name, _program_args[idx].description);
    }

    return rtn;
}
