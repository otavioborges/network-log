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
#include "http.h"
#include "hw_use.h"

#define BUFFER_LENGTH     2048
#define POLL_TIMEO_US     100000
#define HTTP_DEFAULT_PORT 2837
#define PID_FILE          "./network-log.pid"
//#define PID_FILE          "/var/run/network-log.pid"

static void sig_handler(int signo);
static int print_help(int rtn, const char *argv0, char *msg, ...);

static int _continue = 1;
static const struct option_with_description _program_args[] = {
        {{"help", no_argument, NULL, 'h'}, NULL, "Show this message"},
        {{"version", no_argument, NULL, 'v'}, NULL, "Show application version"},
        {{"upload-log", required_argument, NULL, 'u'}, "uload file", "Iptables generated logs with outgoing packages"},
        {{"download-log", required_argument, NULL, 'd'}, "download file", "Iptables generated logs with incoming packages"},
        {{"background", no_argument, NULL, 'b'}, NULL, "A daemon will be created at background and parent will return."},
        {{"http-path", required_argument, NULL, 'H'}, "http data", "Path for HTTP server files"},
};
static size_t _args_length = sizeof(_program_args) / sizeof(struct option_with_description);

int main (int argc, char **argv) {
    int idx, lopt, c = 0, background = 0, rtn = 0;
    struct option *_gen_opts = NULL;
    char *upload_file = NULL, *download_file = NULL, *http_path = NULL;
    pid_t pid;
    FILE *h_pid, *h_upload, *h_download;
    char *read_upload = NULL, *read_download = NULL;
    size_t upload_line = 0, download_line = 0;
    ssize_t rtn_length;
    long upload_current_offset = 0, upload_total_size;
    long download_current_offset = 0, download_total_size;
    struct network_node *net_up_devices = NULL, *net_dw_devices = NULL;
    size_t net_up_dev_count = 0, net_dw_dev_count = 0;

    /* Mount long options array */
    _gen_opts = (struct option *) malloc(sizeof(struct option) * _args_length);
    for (idx = 0; idx < _args_length; idx++)
        _gen_opts[idx] = _program_args[idx]._opt;

    while (c >= 0) {
        c = getopt_long(argc, argv, "hvu:d:bH:", _gen_opts, &lopt);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                return print_help(0, argv[0], NULL);
            case 'v':
                printf("%s - v%s\n", PACKAGE_NAME, PACKAGE_VERSION);
                return 0;
            case 'u':
                if (optarg == NULL)
                    return print_help(-1, argv[0],
                                      "Argument \'%s\' requires an argument.\n", _gen_opts[lopt].name);
                else
                    upload_file = strdup(optarg);
                break;
            case 'd':
                if (optarg == NULL)
                    return print_help(-1, argv[0],
                                      "Argument \'%s\' requires an argument.\n", _gen_opts[lopt].name);
                else
                    download_file = strdup(optarg);
                break;
            case 'H':
                if (optarg == NULL)
                    return print_help(-1, argv[0],
                                      "Argument \'%s\' requires an argument.\n", _gen_opts[lopt].name);
                else
                    http_path = strdup(optarg);
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

    if (upload_file == NULL)
        return print_help(-1, argv[0], "Missing mandatory argument \'%s\'\n", _program_args[2]._opt.name);

    if (download_file == NULL)
        return print_help(-1, argv[0], "Missing mandatory argument \'%s\'\n", _program_args[3]._opt.name);

    if (http_path == NULL)
        return print_help(-1, argv[0], "Missing mandatory argument \'%s\'\n", _program_args[5]._opt.name);

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

    if (hw_use_init()) {
        fprintf(stderr, "Error initiating HW overseer. Exiting...\n");
        goto terminate;
    }

    /* We are either foreground or daemon */
    printf("Trying to open log file \'%s\'...\n", upload_file);
    rtn = open(upload_file, O_RDONLY | O_NONBLOCK);
    if (rtn < 0) {
        fprintf(stderr, "Unable to open file \'%s\'. Reason: %s (%d)\n", upload_file, strerror(errno), errno);
        goto terminate;
    }
    h_upload = fdopen(rtn, "r");
    if (h_upload == NULL) {
        fprintf(stderr, "Unable to access \'%s\' as a stream. Reason: %s (%d)\n", upload_file, strerror(errno), errno);
        rtn = -1;
        goto terminate;
    }

    printf("Trying to open log file \'%s\'...\n", download_file);
    rtn = open(download_file, O_RDONLY | O_NONBLOCK);
    if (rtn < 0) {
        fprintf(stderr, "Unable to open file \'%s\'. Reason: %s (%d)\n", download_file, strerror(errno), errno);
        goto terminate;
    }
    h_download = fdopen(rtn, "r");
    if (h_download == NULL) {
        fprintf(stderr, "Unable to access \'%s\' as a stream. Reason: %s (%d)\n", download_file, strerror(errno), errno);
        rtn = -1;
        goto terminate;
    }

    printf("Initating HTTP server at port %u...\n", HTTP_DEFAULT_PORT);
    if (http_init(HTTP_DEFAULT_PORT, http_path)) {
        fprintf(stderr, "Error initiating HTTP server\n");
        rtn = -1;
        goto terminate;
    }

    /* Skip stale data from logs.
     * TODO: in the future we should use the timestamps on the logs
     */
    fseek(h_upload, 0, SEEK_END);
    upload_current_offset = ftell(h_upload);
    fseek(h_download, 0, SEEK_END);
    download_current_offset = ftell(h_download);

    signal(SIGINT, sig_handler);
    while(_continue) {
        fseek(h_upload, 0, SEEK_END);
        upload_total_size = ftell(h_upload);
        fseek(h_upload, upload_current_offset, SEEK_SET);

        if (upload_current_offset < upload_total_size) {
            while ((rtn_length = getline(&read_upload, &upload_line, h_upload)) >= 0) {
                upload_current_offset += rtn_length;
                /* Process a new line on file */
                rtn = device_stat_parse_line(&net_up_devices, &net_up_dev_count, read_upload, DIR_UPLOAD);
                if (rtn <= -3) {
                    fprintf(stderr, "Corrupted network upload device list. Terminating...\n");
                    _continue = 0;
                    break;
                }
                http_update_upload_list(net_up_devices, net_up_dev_count);
            }
        } else {
            usleep(POLL_TIMEO_US);
        }

        /* Check for the download side of things */
        fseek(h_download, 0, SEEK_END);
        download_total_size = ftell(h_download);
        fseek(h_download, download_current_offset, SEEK_SET);

        if (download_current_offset < download_total_size) {
            while ((rtn_length = getline(&read_download, &download_line, h_download)) >= 0) {
                download_current_offset += rtn_length;
                /* Process a new line on file */
                rtn = device_stat_parse_line(&net_dw_devices, &net_dw_dev_count, read_download, DIR_DOWNLOAD);
                if (rtn <= -3) {
                    fprintf(stderr, "Corrupted network download device list. Terminating...\n");
                    _continue = 0;
                    break;
                }
                http_update_download_list(net_dw_devices, net_dw_dev_count);
            }
        } else {
            usleep(POLL_TIMEO_US);
        }
    }

//    int jdx;
//    if (net_up_devices) {
//        for (idx = 0; idx < net_up_dev_count; idx++) {
//            printf("Network dev.: %s (total data %ldB) (Avg. Speed: %.3fBps)\n",
//                   inet_ntoa(net_up_devices[idx].own.ip), net_up_devices[idx].own.total_data, net_up_devices[idx].avg_speed);
//
//            for (jdx = 0; jdx < net_up_devices[idx].peers_length; jdx++) {
//                printf("\tPeer: %s (total data: %ldB)\n",
//                       inet_ntoa(net_up_devices[idx].peers[jdx].ip), net_up_devices[idx].peers[jdx].total_data);
//            }
//        }
//    }

    printf("Shutting down...\n");
    fclose(h_upload);
    http_end();
    hw_use_terminate();

    if (read_upload) {
        free(read_upload);
        read_upload = NULL;
        upload_line = 0;
    }
    if (read_download) {
        free(read_download);
        read_download = NULL;
        download_line = 0;
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
