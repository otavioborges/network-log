//
// Created by otavio on 23/03/24.
//
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <microhttpd.h>
#include <json.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "http.h"

#define MIME_HTTP                     "text/html"
#define MIME_JSON                     "text/json"
#define MIME_JAVASCRIPT               "text/javascript"
#define MIME_IMAGE_PNG                "image/png"
#define MIME_IMAGE_JPG                "image/jpeg"
#define MIME_IMAGE_GIF                "image/gif"
#define MIME_BINARY                   "application/octet-stream"

#define RESP_INT_BUFFER_LENGTH        2048

static const char *http_resp_404 = "<html><head><title>Not Found</title></head><body>{\"error\":404}</body></html>";
static const char *http_resp_400 = "<html><head><title>Bad Request</title></head><body>{\"error\":400}</body></html>";
static const char *http_resp_401 = "<html><head><title>Unauthorized</title></head><body>{\"error\":401}</body></html>";

static pthread_mutex_t http_network_list_lock = PTHREAD_MUTEX_INITIALIZER;
static struct network_node *_internal_nodes = NULL;
static size_t _int_node_count = 0;
static struct MHD_Daemon *_daemon = NULL;
static char *_http_file_path = NULL;

static enum MHD_Result ahc_echo (void *cls,
                                 struct MHD_Connection *connection,
                                 const char *url,
                                 const char *method,
                                 const char *version,
                                 const char *upload_data, size_t *upload_data_size, void **ptr);

int http_init(unsigned short port, char *http_file_path) {
    if (_daemon)
        return 0;

    if (http_file_path == NULL) {
        fprintf(stderr, "Invalid HTTP file path.\n");
        return -1;
    }
    _http_file_path = http_file_path;

    _daemon = MHD_start_daemon (// MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG | MHD_USE_POLL,
            MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG,
            // MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG | MHD_USE_POLL,
            // MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG,
            port,
            NULL, NULL, &ahc_echo, NULL,
            MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 120,
            MHD_OPTION_END);

    if (_daemon == NULL) {
        fprintf(stderr, "Error initiating HTTP server.\n");
        return -1;
    }

    return 0;
}

void http_end(void) {
    if (_daemon)
        MHD_stop_daemon(_daemon);
    _daemon = NULL;
}

void http_update_network_list(struct network_node *nodes, size_t length) {
    pthread_mutex_lock(&http_network_list_lock);
    if (_internal_nodes) {
        free(_internal_nodes);
        _internal_nodes = NULL;
    }

    _internal_nodes = (struct network_node *) malloc(sizeof(struct network_node) * length);
    memcpy(_internal_nodes, nodes, sizeof(struct network_node) * length);
    _int_node_count = length;
    pthread_mutex_unlock(&http_network_list_lock);
}

#define JSON_KEY_DEVICE    "device"
#define JSON_KEY_SPEED     "speed"

static enum MHD_Result ahc_echo (void *cls,
          struct MHD_Connection *connection,
          const char *url,
          const char *method,
          const char *version,
          const char *upload_data, size_t *upload_data_size, void **ptr) {
    char *resp_str = NULL, *content_type = MIME_JSON, *ext = NULL;
    char generated_resp[RESP_INT_BUFFER_LENGTH];
    char resp_file[RESP_INT_BUFFER_LENGTH];
    int resp_length;
    struct MHD_Response *response;
    enum MHD_Result  res;
    int resp_code, idx, fd;
    struct json_object *jarray, *jobj;

    if (strcmp(method, "GET") != 0) {
        resp_str = (char *)http_resp_401;
        resp_code = MHD_HTTP_UNAUTHORIZED;
        resp_length = strlen(resp_str);
    } else {
        if (strcmp(url,"/api/devices") == 0) {
            jarray = json_object_new_array();

            pthread_mutex_lock(&http_network_list_lock);
            for (idx = 0; idx < _int_node_count; idx++) {
                jobj = json_object_new_object();
                json_object_object_add(jobj, JSON_KEY_DEVICE,
                                       json_object_new_string(inet_ntoa(_internal_nodes[idx].own.ip)));

                json_object_object_add(jobj, JSON_KEY_SPEED,
                                       json_object_new_double((double) _internal_nodes[idx].avg_speed));

                json_object_array_add(jarray, jobj);
            }

            strcpy(generated_resp, json_object_to_json_string(jarray));
            pthread_mutex_unlock(&http_network_list_lock);

            json_object_put(jarray);

            resp_str = generated_resp;
            resp_code = MHD_HTTP_OK;
            resp_length = strlen(resp_str);
        } else if (strcmp(url,"/api/speed") == 0) {
            jobj = json_object_new_object();
            json_object_object_add(jobj, JSON_KEY_SPEED, json_object_new_double((double)device_stat_net_speed()));

            strcpy(generated_resp, json_object_to_json_string(jobj));
            json_object_put(jobj);

            resp_str = generated_resp;
            resp_code = MHD_HTTP_OK;
            resp_length = strlen(resp_str);
        } else {
            if (strcmp(url,"/") == 0)
                snprintf(resp_file, RESP_INT_BUFFER_LENGTH, "%s/index.htm", _http_file_path);
            else
                snprintf(resp_file, RESP_INT_BUFFER_LENGTH, "%s%s", _http_file_path, url);

            if (access(resp_file, F_OK) != 0) {
                resp_str = (char *) http_resp_404;
                resp_code = MHD_HTTP_NOT_FOUND;
            } else {
                fd = open(resp_file, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "Error opening HTTP file \'%s\'. Reason: %s (%d)\n",
                            resp_file, strerror(errno), errno);

                    resp_str = (char *) http_resp_404;
                    resp_code = MHD_HTTP_NOT_FOUND;
                } else {
                    ext = strrchr(resp_file, '.');
                    if (ext == NULL) {
                        content_type = MIME_BINARY;
                    } else if ((strcmp((ext + 1), "htm") == 0) || (strcmp((ext + 1), "html") == 0)) {
                        content_type = MIME_HTTP;
                    } else if ((strcmp((ext + 1), "js") == 0)) {
                        content_type = MIME_JAVASCRIPT;
                    } else if ((strcmp((ext + 1), "png") == 0)) {
                        content_type = MIME_IMAGE_PNG;
                    } else if ((strcmp((ext + 1), "jpg") == 0) || (strcmp((ext + 1), "jpeg") == 0)) {
                        content_type = MIME_IMAGE_JPG;
                    } else if ((strcmp((ext + 1), "gif") == 0)) {
                        content_type = MIME_IMAGE_GIF;
                    }

                    resp_length = (int)read(fd, generated_resp, RESP_INT_BUFFER_LENGTH);
                    resp_str = generated_resp;
                    resp_code = MHD_HTTP_OK;
                }
            }
        }
    }

    response = MHD_create_response_from_buffer (resp_length,
                                                (void *) resp_str,
                                                MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(response, "Content-Type", content_type);
    res = MHD_queue_response (connection, resp_code, response);
    MHD_destroy_response (response);
    return res;
}