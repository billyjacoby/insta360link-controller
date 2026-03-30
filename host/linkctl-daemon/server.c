#include "server.h"
#include "camera.h"

#include <libwebsockets.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Per-session data for each WebSocket client
struct per_session_data {
    struct per_session_data *next;
    struct lws *wsi;
    // Ring buffer for outgoing messages
    unsigned char *pending_msg;
    size_t pending_len;
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
    { NULL, NULL, 0, 0 }  // Terminator
};

int server_init(uint16_t port) {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;

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
    pss->pending_msg = NULL;
    pss->pending_len = 0;
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
    free(pss->pending_msg);
    pss->pending_msg = NULL;
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
            camera_status_t st = camera_get_status();
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
        if (in && len > 0) {
            handle_command(wsi, (const char *)in, len);
        }
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
        if (pss->pending_msg && pss->pending_len > 0) {
            lws_write(wsi, pss->pending_msg + LWS_PRE,
                      pss->pending_len, LWS_WRITE_TEXT);
            free(pss->pending_msg);
            pss->pending_msg = NULL;
            pss->pending_len = 0;
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

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"invalid JSON\"}");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd)) {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"missing cmd field\"}");
        cJSON_Delete(root);
        return;
    }

    const char *cmd_str = cmd->valuestring;
    int rc = 0;

    if (strcmp(cmd_str, "pan_tilt") == 0) {
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
    } else {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"unknown command\"}");
    }

    if (rc != 0) {
        send_json(wsi, "{\"type\":\"error\",\"message\":\"camera not connected\"}");
    }

    cJSON_Delete(root);
}

static void send_json(struct lws *wsi, const char *json) {
    size_t len = strlen(json);
    // libwebsockets requires LWS_PRE bytes of padding before the data
    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) return;

    memcpy(buf + LWS_PRE, json, len);
    lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);
}

static void broadcast_json(const char *json) {
    size_t len = strlen(json);

    for (struct per_session_data *pss = clients; pss; pss = pss->next) {
        // Queue the message and request a writeable callback
        free(pss->pending_msg);
        pss->pending_msg = malloc(LWS_PRE + len);
        if (pss->pending_msg) {
            memcpy(pss->pending_msg + LWS_PRE, json, len);
            pss->pending_len = len;
            lws_callback_on_writable(pss->wsi);
        }
    }
}
