/*
 * linkctl — CLI client for linkctl-daemon
 *
 * Connects to the running daemon via WebSocket and sends camera commands.
 * Usage: linkctl <command> [args]
 */

#include <libwebsockets.h>
#include <cjson/cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>

/* --- Protocol constants (must match daemon) --- */

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9000

#define DIR_POSITIVE 1
#define DIR_NEGATIVE 255
#define DIR_STOP     0

#define PAN_MAX_SPEED  30
#define TILT_MAX_SPEED 20
#define ZOOM_MIN       100
#define ZOOM_MAX       400

/* --- Jog mode constants --- */

#define JOG_DEFAULT_PAN_SPEED  15
#define JOG_DEFAULT_TILT_SPEED 10
#define JOG_SPEED_STEP         3
#define JOG_MIN_SPEED          3
#define JOG_STOP_TIMEOUT_US    150000  /* 150ms */

/* --- Command types --- */

typedef enum {
    CMD_HELP,
    CMD_STATUS,
    CMD_CENTER,
    CMD_ZOOM_SET,
    CMD_ZOOM_QUERY,
    CMD_STOP,
    CMD_JOG,
} command_t;

/* --- Client state --- */

static struct lws_context *ctx = NULL;
static struct lws *client_wsi = NULL;
static bool done = false;
static int exit_code = 0;

/* Parsed command */
static command_t command;
static float zoom_value = 0.0f;

/* Pending messages to send (up to 2 for compound center = center + zoom) */
#define MAX_PENDING 2
static char pending[MAX_PENDING][512];
static int pending_count = 0;
static int pending_sent = 0;

/* Timeout for action commands (ack may not arrive from older daemons) */
#define ACTION_TIMEOUT_SEC 3
static char alarm_msg[128];
static size_t alarm_msg_len = 0;

/* Jog mode state */
static bool jog_active = false;

/* Terminal state for jog mode */
static struct termios orig_termios;
static bool termios_saved = false;

/* --- Terminal helpers --- */

static void restore_terminal(void) {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }
}

/* Immediate exit — flushes output and restores terminal */
static void finish(int code) {
    restore_terminal();
    fflush(NULL);
    _exit(code);
}

static void enter_raw_mode(void) {
    if (!isatty(STDIN_FILENO)) return;

    tcgetattr(STDIN_FILENO, &orig_termios);
    termios_saved = true;

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void handle_signal(int sig) {
    (void)sig;
    restore_terminal();
    done = true;
}

/* SIGALRM handler for action command timeout.
   Prints pre-formatted alarm_msg and exits. */
static void handle_alarm(int sig) {
    (void)sig;
    if (alarm_msg_len > 0) {
        write(STDOUT_FILENO, alarm_msg, alarm_msg_len);
    }
    _exit(0);
}

/* --- JSON command builders --- */

static void queue_cmd(const char *json) {
    if (pending_count < MAX_PENDING) {
        snprintf(pending[pending_count], sizeof(pending[0]), "%s", json);
        pending_count++;
    }
}

static void queue_pan_tilt(uint8_t pan_dir, uint8_t pan_speed,
                           uint8_t tilt_dir, uint8_t tilt_speed) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"pan_tilt\",\"pan_dir\":%u,\"pan_speed\":%u,"
             "\"tilt_dir\":%u,\"tilt_speed\":%u}",
             pan_dir, pan_speed, tilt_dir, tilt_speed);
    queue_cmd(buf);
}

static void queue_zoom(uint16_t value) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"zoom\",\"value\":%u}", value);
    queue_cmd(buf);
}

static void queue_stop(void) {
    queue_cmd("{\"cmd\":\"stop\"}");
}

static void queue_center(void) {
    queue_cmd("{\"cmd\":\"center\"}");
}

static void queue_status(void) {
    queue_cmd("{\"cmd\":\"status\"}");
}

/* --- Send pending command over WebSocket --- */

static void send_next(void) {
    if (!client_wsi || pending_sent >= pending_count) return;

    const char *json = pending[pending_sent];
    size_t len = strlen(json);
    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) return;

    memcpy(buf + LWS_PRE, json, len);
    lws_write(client_wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);
    pending_sent++;

    /* Start alarm timeout when first command is sent.
       If the daemon doesn't send an ack (older versions), SIGALRM fires. */
    if (pending_sent == 1 && command != CMD_STATUS && command != CMD_ZOOM_QUERY) {
        signal(SIGALRM, handle_alarm);
        alarm(ACTION_TIMEOUT_SEC);
    }
}

/* --- Response handling --- */

static void handle_response(const char *json, size_t len) {
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    const char *type_str = type->valuestring;

    if (strcmp(type_str, "ack") == 0) {
        /* Command succeeded. If more commands pending (compound center), send next. */
        if (pending_sent < pending_count) {
            send_next();
            lws_callback_on_writable(client_wsi);
            cJSON_Delete(root);
            return;
        }
        /* All commands done */
        if (command == CMD_CENTER) {
            printf("Centered, zoom reset to 1.0x\n");
        } else if (command == CMD_ZOOM_SET) {
            printf("Zoom set to %.1fx\n", zoom_value);
        } else if (command == CMD_STOP) {
            printf("Stopped\n");
        }
        cJSON_Delete(root);
        finish(0);

    } else if (strcmp(type_str, "status") == 0) {
        cJSON *connected = cJSON_GetObjectItem(root, "camera_connected");
        cJSON *zoom = cJSON_GetObjectItem(root, "zoom");

        bool cam = cJSON_IsTrue(connected);
        int z = cJSON_IsNumber(zoom) ? (int)zoom->valuedouble : 0;

        if (command == CMD_STATUS) {
            printf("Camera: %s\n", cam ? "connected" : "not connected");
            if (cam && z > 0) {
                printf("Zoom:   %.1fx (%d)\n", z / 100.0, z);
            }
            cJSON_Delete(root);
            finish(0);
        } else if (command == CMD_ZOOM_QUERY) {
            if (cam && z > 0) {
                printf("Zoom: %.1fx (%d)\n", z / 100.0, z);
            } else {
                printf("Camera not connected\n");
            }
            cJSON_Delete(root);
            finish(cam ? 0 : 1);
        }
        /* For other commands, the initial status on connect is ignored */

    } else if (strcmp(type_str, "error") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        fprintf(stderr, "Error: %s\n",
                cJSON_IsString(msg) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        finish(1);
    }

    cJSON_Delete(root);
}

/* --- Jog mode --- */

static void run_jog_mode(void) {
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Jog mode requires a terminal\n");
        exit_code = 1;
        done = true;
        return;
    }

    printf("\n");
    printf("=== Jog Mode ===\n");
    printf("  WASD  Pan/tilt      +/- Speed\n");
    printf("  Space Stop          c   Center\n");
    printf("  q/ESC Exit\n");
    printf("\n");

    uint8_t pan_speed = JOG_DEFAULT_PAN_SPEED;
    uint8_t tilt_speed = JOG_DEFAULT_TILT_SPEED;
    printf("Speed: pan=%u tilt=%u\n", pan_speed, tilt_speed);

    enter_raw_mode();
    jog_active = true;

    bool moving = false;
    struct timeval last_input;
    gettimeofday(&last_input, NULL);

    while (!done) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n == 1) {
            gettimeofday(&last_input, NULL);
            c = tolower((unsigned char)c);

            /* Reset pending for each jog key */
            pending_count = 0;
            pending_sent = 0;

            switch (c) {
            case 'w':
                queue_pan_tilt(DIR_STOP, 0, DIR_POSITIVE, tilt_speed);
                moving = true;
                break;
            case 's':
                queue_pan_tilt(DIR_STOP, 0, DIR_NEGATIVE, tilt_speed);
                moving = true;
                break;
            case 'a':
                queue_pan_tilt(DIR_NEGATIVE, pan_speed, DIR_STOP, 0);
                moving = true;
                break;
            case 'd':
                queue_pan_tilt(DIR_POSITIVE, pan_speed, DIR_STOP, 0);
                moving = true;
                break;
            case '+': case '=':
                if (pan_speed + JOG_SPEED_STEP <= PAN_MAX_SPEED)
                    pan_speed += JOG_SPEED_STEP;
                else
                    pan_speed = PAN_MAX_SPEED;
                if (tilt_speed + JOG_SPEED_STEP <= TILT_MAX_SPEED)
                    tilt_speed += JOG_SPEED_STEP;
                else
                    tilt_speed = TILT_MAX_SPEED;
                printf("\rSpeed: pan=%u tilt=%u   \r", pan_speed, tilt_speed);
                fflush(stdout);
                break;
            case '-':
                if (pan_speed > JOG_MIN_SPEED + JOG_SPEED_STEP)
                    pan_speed -= JOG_SPEED_STEP;
                else
                    pan_speed = JOG_MIN_SPEED;
                if (tilt_speed > JOG_MIN_SPEED + JOG_SPEED_STEP)
                    tilt_speed -= JOG_SPEED_STEP;
                else
                    tilt_speed = JOG_MIN_SPEED;
                printf("\rSpeed: pan=%u tilt=%u   \r", pan_speed, tilt_speed);
                fflush(stdout);
                break;
            case ' ':
                queue_stop();
                moving = false;
                break;
            case 'c':
                queue_center();
                moving = false;
                break;
            case 'q':
            case 0x1B:  /* ESC */
            case 0x03:  /* Ctrl-C */
                pending_count = 0;
                pending_sent = 0;
                queue_stop();
                send_next();
                done = true;
                break;
            default:
                break;
            }

            /* Send queued jog command */
            if (pending_count > pending_sent) {
                send_next();
                lws_callback_on_writable(client_wsi);
            }
        }

        /* Auto-stop after timeout with no input */
        if (moving) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed_us = (now.tv_sec - last_input.tv_sec) * 1000000L
                            + (now.tv_usec - last_input.tv_usec);
            if (elapsed_us > JOG_STOP_TIMEOUT_US) {
                pending_count = 0;
                pending_sent = 0;
                queue_stop();
                send_next();
                lws_callback_on_writable(client_wsi);
                moving = false;
            }
        }

        /* Service WebSocket (keep connection alive, process acks) */
        lws_service(ctx, 10);
        usleep(1000);
    }

    restore_terminal();
    jog_active = false;
    printf("\nExited jog mode.\n");
}

/* --- WebSocket client callback --- */

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    (void)user;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        client_wsi = wsi;
        if (command == CMD_JOG) {
            /* Jog mode: don't send a command yet, enter jog loop */
            break;
        }
        /* Queue command(s) and request write */
        lws_callback_on_writable(wsi);
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        if (pending_sent < pending_count) {
            send_next();
            /* If more to send, request another writeable */
            if (pending_sent < pending_count) {
                lws_callback_on_writable(wsi);
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (in && len > 0) {
            if (jog_active) {
                /* In jog mode, ignore responses (acks, status updates) */
                break;
            }

            /* For status/zoom_query: ignore the initial status sent on connect,
               only handle the response to our explicit command */
            static bool initial_status_received = false;
            if (!initial_status_received) {
                initial_status_received = true;
                /* Check if this is the initial status (arrives before we send anything) */
                if (pending_sent == 0) {
                    /* Haven't sent our command yet — this is the connect status, ignore */
                    break;
                }
            }

            handle_response((const char *)in, len);
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        fprintf(stderr, "Error: daemon not active (is linkctl-daemon running?)\n");
        if (command != CMD_JOG) finish(1);
        exit_code = 1;
        done = true;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        client_wsi = NULL;
        if (!done) {
            fprintf(stderr, "Error: connection lost\n");
            if (command != CMD_JOG) finish(1);
            exit_code = 1;
            done = true;
        }
        break;

    default:
        break;
    }

    return 0;
}

static struct lws_protocols protocols[] = {
    {
        .name = "arduino",
        .callback = ws_callback,
        .per_session_data_size = 0,
        .rx_buffer_size = 4096,
    },
    { NULL, NULL, 0, 0 }
};

/* --- Usage --- */

static void print_usage(void) {
    printf("Usage: linkctl <command> [args]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  help              Show this help\n");
    printf("  status            Show camera connection and zoom level\n");
    printf("  center            Center gimbal and reset zoom to 1.0x\n");
    printf("  zoom <1.0-4.0>    Set zoom level\n");
    printf("  zoom              Show current zoom level\n");
    printf("  stop              Stop all gimbal movement\n");
    printf("  jog               Interactive WASD pan/tilt mode\n");
    printf("\n");
    printf("The linkctl-daemon must be running for commands to work.\n");
}

/* --- Argument parsing --- */

static int parse_args(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return -1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
        strcmp(cmd, "-h") == 0) {
        command = CMD_HELP;
        return 0;
    }

    if (strcmp(cmd, "status") == 0) {
        command = CMD_STATUS;
    } else if (strcmp(cmd, "center") == 0) {
        command = CMD_CENTER;
    } else if (strcmp(cmd, "zoom") == 0) {
        if (argc >= 3) {
            command = CMD_ZOOM_SET;
            char *end;
            zoom_value = strtof(argv[2], &end);
            if (*end != '\0' || zoom_value < 1.0f || zoom_value > 4.0f) {
                fprintf(stderr, "Error: zoom must be a number between 1.0 and 4.0\n");
                return -1;
            }
        } else {
            command = CMD_ZOOM_QUERY;
        }
    } else if (strcmp(cmd, "stop") == 0) {
        command = CMD_STOP;
    } else if (strcmp(cmd, "jog") == 0) {
        command = CMD_JOG;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        fprintf(stderr, "Run 'linkctl help' for usage.\n");
        return -1;
    }

    return 0;
}

/* --- Main --- */

int main(int argc, char **argv) {
    if (parse_args(argc, argv) != 0) {
        return 1;
    }

    if (command == CMD_HELP) {
        print_usage();
        return 0;
    }

    /* Install signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Queue commands based on parsed command */
    switch (command) {
    case CMD_STATUS:
    case CMD_ZOOM_QUERY:
        queue_status();
        break;
    case CMD_CENTER:
        queue_center();
        queue_zoom(ZOOM_MIN);  /* Second command: reset zoom to 1x */
        break;
    case CMD_ZOOM_SET: {
        uint16_t raw = (uint16_t)(zoom_value * 100.0f + 0.5f);
        if (raw < ZOOM_MIN) raw = ZOOM_MIN;
        if (raw > ZOOM_MAX) raw = ZOOM_MAX;
        queue_zoom(raw);
        break;
    }
    case CMD_STOP:
        queue_stop();
        break;
    case CMD_JOG:
        /* Commands sent interactively */
        break;
    default:
        break;
    }

    /* Pre-format the alarm timeout message (signal handler can't use printf) */
    switch (command) {
    case CMD_CENTER:
        alarm_msg_len = snprintf(alarm_msg, sizeof(alarm_msg),
                                 "Centered, zoom reset to 1.0x\n");
        break;
    case CMD_ZOOM_SET:
        alarm_msg_len = snprintf(alarm_msg, sizeof(alarm_msg),
                                 "Zoom set to %.1fx\n", zoom_value);
        break;
    case CMD_STOP:
        alarm_msg_len = snprintf(alarm_msg, sizeof(alarm_msg), "Stopped\n");
        break;
    default:
        break;
    }

    /* Suppress libwebsockets logs */
    lws_set_log_level(0, NULL);

    /* Create WebSocket client context */
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;  /* Client only, no server */
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    ctx = lws_create_context(&info);
    if (!ctx) {
        fprintf(stderr, "Error: failed to create WebSocket context\n");
        return 1;
    }

    /* Initiate connection to daemon */
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = ctx;
    ccinfo.address = DEFAULT_HOST;
    ccinfo.port = DEFAULT_PORT;
    ccinfo.path = "/";
    ccinfo.host = DEFAULT_HOST;
    ccinfo.origin = DEFAULT_HOST;
    ccinfo.protocol = "arduino";

    if (!lws_client_connect_via_info(&ccinfo)) {
        fprintf(stderr, "Error: failed to initiate connection\n");
        lws_context_destroy(ctx);
        return 1;
    }

    /* Event loop */
    if (command == CMD_JOG) {
        /* Wait for connection, then enter jog mode */
        while (!done && !client_wsi) {
            lws_service(ctx, 50);
        }
        if (!done) {
            run_jog_mode();
        }
    } else {
        /* One-shot: run until response received.
           SIGALRM provides the timeout for action commands without ack. */
        while (!done) {
            lws_service(ctx, 50);
        }
    }

    finish(exit_code);
}
