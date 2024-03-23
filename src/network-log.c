#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include "options.h"
#include "config.h"

#define PID_FILE      "./network-log.pid"
//#define PID_FILE      "/var/run/network-log.pid"

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
    int idx, lopt, c = 0, background = 0;
    struct option *_gen_opts = NULL;
    char *input_file = NULL;
    pid_t pid;
    FILE *h_pid, h_log;

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


terminate:
    if (background) {
        /* termination on a daemon. do a clean job */
        if (access(PID_FILE, F_OK) == 0) {
            unlink(PID_FILE);
        }
    }

    printf("Gracefully terminated application.");
	return 0;
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
