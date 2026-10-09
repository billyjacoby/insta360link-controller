#include "server.h"
#include "camera.h"

#include <libwebsockets.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RESPONSE_QUEUE_SIZE 32
#define MAX_COMMAND_SIZE 4096

// Per-session data for each WebSocket client
struct per_session_data {
    struct per_session_data *next;
    struct lws *wsi;
    unsigned char *messages[RESPONSE_QUEUE_SIZE];
    size_t lengths[RESPONSE_QUEUE_SIZE];
    unsigned head;
    unsigned count;
    char incoming[MAX_COMMAND_SIZE];
    size_t incoming_length;
    bool incoming_overflow;
};

static struct lws_context *ws_ctx = NULL;
static struct per_session_data *clients = NULL;

// Forward declarations
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);
static void handle_command(struct lws *wsi, const char *json, size_t len);
static void send_json(struct lws *wsi, const char *json);
static void broadcast_json(const char *json);

static struct lws_protocols protocols[] = {
    {
        .name = "arduino",  // Match ESP32 WebSocketsClient's default subprotocol
        .callback = ws_callback,
        .per_session_data_size = sizeof(struct per_session_data),
        .rx_buffer_size = 4096,
    },
    {0}  // Terminator
};

int server_init(uint16_t port, bool loopback_only) {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    if (loopback_only) {
        info.iface = "127.0.0.1";
        info.options |= LWS_SERVER_OPTION_DISABLE_IPV6;
    }

    // Suppress noisy libwebsockets logs (only show errors + warnings)
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

    ws_ctx = lws_create_context(&info);
    if (!ws_ctx) {
        fprintf(stderr, "[server] Failed to create WebSocket context\n");
        return -1;
    }

    printf("[server] WebSocket server listening on port %u\n", port);
    return 0;
}

void server_service(int timeout_ms) {
    if (!ws_ctx) return;
    lws_service(ws_ctx, timeout_ms);
}

void server_wake(void) {
    if (ws_ctx) lws_cancel_service(ws_ctx);
}

void server_destroy(void) {
    if (ws_ctx) {
        lws_context_destroy(ws_ctx);
        ws_ctx = NULL;
    }
    clients = NULL;
}

void server_broadcast_status(bool camera_connected, uint16_t zoom) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "status");
    cJSON_AddBoolToObject(msg, "camera_connected", camera_connected);
    cJSON_AddNumberToObject(msg, "zoom", zoom);

    char *json = cJSON_PrintUnformatted(msg);
    if (json) {
        broadcast_json(json);
        free(json);
    }
    cJSON_Delete(msg);
}

void server_broadcast_error(const char *message) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "error");
    cJSON_AddStringToObject(msg, "message", message);

    char *json = cJSON_PrintUnformatted(msg);
    if (json) {
        broadcast_json(json);
        free(json);
    }
    cJSON_Delete(msg);
}

// --- Internal ---

static void add_client(struct per_session_data *pss, struct lws *wsi) {
    pss->wsi = wsi;
    pss->next = clients;
    pss->head = 0;
    pss->count = 0;
    pss->incoming_length = 0;
    pss->incoming_overflow = false;
    clients = pss;
}

static void remove_client(struct per_session_data *pss) {
    struct per_session_data **pp = &clients;
    while (*pp) {
        if (*pp == pss) {
            *pp = pss->next;
            break;
        }
        pp = &(*pp)->next;
    }
    while (pss->count) {
        free(pss->messages[pss->head]);
        pss->head = (pss->head + 1) % RESPONSE_QUEUE_SIZE;
        pss->count--;
    }
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    struct per_session_data *pss = (struct per_session_data *)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        printf("[server] Client connected\n");
        add_client(pss, wsi);
        // Send initial status
        {
            pthread_mutex_lock(&camera_mutex);
            camera_status_t st = camera_get_status();
            pthread_mutex_unlock(&camera_mutex);
            cJSON *msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "type", "status");
            cJSON_AddBoolToObject(msg, "camera_connected", st.connected);
            cJSON_AddNumberToObject(msg, "zoom", st.zoom);
            char *json = cJSON_PrintUnformatted(msg);
            if (json) {
                send_json(wsi, json);
                free(json);
            }
            cJSON_Delete(msg);
        }
        break;

    case LWS_CALLBACK_CLOSED:
        printf("[server] Client disconnected\n");
        remove_client(pss);
        break;

    case LWS_CALLBACK_RECEIVE:
        // A WebSocket command may span callbacks or continuation frames. Never
        // act on a valid JSON prefix before the entire message has arrived.
        if (in && !pss->incoming_overflow) {
            if (len > MAX_COMMAND_SIZE - pss->incoming_length) {
                pss->incoming_overflow = true;
            } else {
                memcpy(pss->incoming + pss->incoming_length, in, len);
                pss->incoming_length += len;
            }
        }
        if (lws_is_final_fragment(wsi) && lws_remaining_packet_payload(wsi) == 0) {
            pthread_mutex_lock(&camera_mutex);
            if (pss->incoming_overflow)
                send_json(wsi, "{\"type\":\"error\",\"message\":\"command too large\"}");
            else
                handle_command(wsi, pss->incoming, pss->incoming_length);
            pthread_mutex_unlock(&camera_mutex);
            pss->incoming_length = 0;
            pss->incoming_overflow = false;
        }
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
        if (pss->count) {
            unsigned index = pss->head;
            size_t length = pss->lengths[index];
            int written = lws_write(wsi, pss->messages[index] + LWS_PRE,
                                    length, LWS_WRITE_TEXT);
            free(pss->messages[index]);
            pss->head = (index + 1) % RESPONSE_QUEUE_SIZE;
            pss->count--;
            if (written < 0 || (size_t)written != length) return -1;
            if (pss->count) lws_callback_on_writable(wsi);
        }
        break;

    default:
        break;
    }

    return 0;
}

static void handle_command(struct lws *wsi, const char *json, size_t len) {
    // Null-terminate for cJSON (input may not be)
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, json, len);
    buf[len] = '\0';

    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(buf, len + 1, &end, false);
    if (!root) {
        free(buf);
        send_json(wsi, "{\"type\":\"error\",\"message\":\"invalid JSON\"}");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    bool settings_command = cJSON_IsString(cmd) &&
        (strcmp(cmd->valuestring, "settings_get") == 0 || strcmp(cmd->valuestring, "setting_set") == 0);
    while (end < buf + len && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    // cJSON accepts trailing input and decodes embedded NULs into truncated C
    // strings. Neither may turn a malformed setting ID/value into a USB write.
    bool invalid = !cJSON_IsObject(root) || end != buf + len || memchr(buf, '\0', len) ||
        (settings_command && strstr(buf, "\\u0000"));
    free(buf);
    if (invalid) {
        send_json(wsi, settings_command ?
            "{\"type\":\"settings_error\",\"message\":\"invalid settings JSON\"}" :
            "{\"type\":\"error\",\"message\":\"invalid JSON\"}");
        cJSON_Delete(root);
        return;
    }

    if (!cJSON_IsString(cmd)) {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"missing cmd field\"}");
        cJSON_Delete(root);
        return;
    }

    const char *cmd_str = cmd->valuestring;
    int rc = 0;

    if (strcmp(cmd_str, "settings_get") == 0 || strcmp(cmd_str, "setting_set") == 0) {
        cJSON *response;
        if (strcmp(cmd_str, "settings_get") == 0) {
            response = camera_settings_get();
        } else {
            const char *id;
            int64_t value;
            response = settings_parse_set(root, &id, &value) ?
                camera_setting_set(id, value) :
                settings_error("setting_set requires an ID and an integer value");
        }
        char *response_json = cJSON_PrintUnformatted(response);
        if (response_json) {
            send_json(wsi, response_json);
            free(response_json);
        }
        cJSON_Delete(response);
        cJSON_Delete(root);
        return;
    } else if (strcmp(cmd_str, "pan_tilt") == 0) {
        int pan_dir = 0, pan_speed = 0, tilt_dir = 0, tilt_speed = 0;
        cJSON *item;
        if ((item = cJSON_GetObjectItem(root, "pan_dir")))   pan_dir = item->valueint;
        if ((item = cJSON_GetObjectItem(root, "pan_speed")))  pan_speed = item->valueint;
        if ((item = cJSON_GetObjectItem(root, "tilt_dir")))   tilt_dir = item->valueint;
        if ((item = cJSON_GetObjectItem(root, "tilt_speed"))) tilt_speed = item->valueint;

        // Speed 0 on both axes means stop — the camera requires the
        // explicit stop payload [0,1,0,1], not [0,0,0,0].
        if (pan_speed == 0 && tilt_speed == 0) {
            rc = camera_stop();
        } else {
            rc = camera_pan_tilt((uint8_t)pan_dir, (uint8_t)pan_speed,
                                 (uint8_t)tilt_dir, (uint8_t)tilt_speed);
        }
    } else if (strcmp(cmd_str, "zoom") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "value");
        if (cJSON_IsNumber(val)) {
            rc = camera_zoom((uint16_t)val->valueint);
        } else {
            send_json(wsi, "{\"type\":\"error\",\"message\":\"missing zoom value\"}");
            cJSON_Delete(root);
            return;
        }
    } else if (strcmp(cmd_str, "stop") == 0) {
        rc = camera_stop();
    } else if (strcmp(cmd_str, "center") == 0) {
        rc = camera_center();
    } else if (strcmp(cmd_str, "status") == 0) {
        camera_status_t st = camera_get_status();
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "status");
        cJSON_AddBoolToObject(resp, "camera_connected", st.connected);
        cJSON_AddNumberToObject(resp, "zoom", st.zoom);
        char *resp_json = cJSON_PrintUnformatted(resp);
        if (resp_json) {
            send_json(wsi, resp_json);
            free(resp_json);
        }
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    } else {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"unknown command\"}");
        cJSON_Delete(root);
        return;
    }

    if (rc != 0) {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"camera not connected\"}");
    } else {
        send_json(wsi, "{\"type\":\"ack\"}");
    }

    cJSON_Delete(root);
}

static void send_json(struct lws *wsi, const char *json) {
    struct per_session_data *pss = lws_wsi_user(wsi);
    if (!pss) return;
    if (pss->count == RESPONSE_QUEUE_SIZE) {
        lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
        return;
    }
    size_t len = strlen(json);
    // libwebsockets requires LWS_PRE bytes of padding before the data
    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
        return;
    }

    memcpy(buf + LWS_PRE, json, len);
    unsigned index = (pss->head + pss->count) % RESPONSE_QUEUE_SIZE;
    pss->messages[index] = buf;
    pss->lengths[index] = len;
    pss->count++;
    lws_callback_on_writable(wsi);
}

static void broadcast_json(const char *json) {
    for (struct per_session_data *pss = clients; pss; pss = pss->next) {
        send_json(pss->wsi, json);
    }
}
