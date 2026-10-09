#include "camera.h"
#include "server.h"
#include "control.h"
#include "mdns.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#define CAMERA_POLL_INTERVAL_SEC 5

static atomic_bool running = true;
static bool use_mdns = true;
static uint16_t port = DEFAULT_PORT;
static bool test_mode = false;
static bool status_mode = false;
static bool settings_mode = false;
static bool link2_pro = false;
static bool auto_camera = false;
static bool use_control = true;
static bool loopback_only = false;

static void handle_signal(int sig) {
    (void)sig;
    running = false;
}

static void print_usage(const char *name) {
    fprintf(stderr, "Usage: %s [options]\n", name);
    fprintf(stderr, "  --port <port>   WebSocket port (default: %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  --no-mdns       Disable mDNS advertisement\n");
    fprintf(stderr, "  --test          Test camera control and exit\n");
    fprintf(stderr, "  --status        Read camera status and exit (no camera writes)\n");
    fprintf(stderr, "  --settings      Read camera settings as JSON and exit (no camera writes)\n");
    fprintf(stderr, "  --link2-pro     Select experimental Link 2 Pro profile (2e1a:4c06)\n");
    fprintf(stderr, "  --auto-camera   Automatically detect one supported Link / Link 2 Pro\n");
    fprintf(stderr, "  --loopback      Listen only on 127.0.0.1 (for the desktop app)\n");
    fprintf(stderr, "  --no-control    Disable the TCP CLI socket (for the desktop app)\n");
    fprintf(stderr, "  --help          Show this help\n");
}

static int parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            const char *value = argv[++i];
            long number = strtol(value, &end, 10);
            if (errno || end == value || *end || number < 1 || number > 65535) {
                fprintf(stderr, "Invalid port: %s (expected 1-65535)\n", value);
                return -1;
            }
            port = (uint16_t)number;
        } else if (strcmp(argv[i], "--no-mdns") == 0) {
            use_mdns = false;
        } else if (strcmp(argv[i], "--test") == 0) {
            test_mode = true;
        } else if (strcmp(argv[i], "--status") == 0) {
            status_mode = true;
        } else if (strcmp(argv[i], "--settings") == 0) {
            settings_mode = true;
        } else if (strcmp(argv[i], "--link2-pro") == 0) {
            link2_pro = true;
        } else if (strcmp(argv[i], "--auto-camera") == 0) {
            auto_camera = true;
        } else if (strcmp(argv[i], "--loopback") == 0) {
            loopback_only = true;
        } else if (strcmp(argv[i], "--no-control") == 0) {
            use_control = false;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    if ((test_mode && (status_mode || settings_mode)) || (status_mode && settings_mode)) {
        fprintf(stderr, "--status, --settings, and --test cannot be combined\n");
        return -1;
    }
    if (link2_pro && auto_camera) {
        fprintf(stderr, "--auto-camera and --link2-pro cannot be combined\n");
        return -1;
    }
    return 0;
}

static int run_test(void) {
    printf("[test] Looking for Insta360 Link...\n");

    if (camera_open() != 0) {
        fprintf(stderr, "[test] Camera not found. Is it connected via USB?\n");
        return 1;
    }

    printf("[test] Camera connected!\n");

    // Query zoom
    int zoom = camera_get_zoom();
    if (zoom >= 0) {
        printf("[test] Current zoom: %d\n", zoom);
    }

    // Test pan right for 1 second
    printf("[test] Panning right...\n");
    camera_pan_tilt(DIR_POSITIVE, 10, DIR_STOP, 0);
    sleep(1);

    printf("[test] Stopping...\n");
    camera_stop();
    sleep(1);

    // Test pan left for 1 second
    printf("[test] Panning left...\n");
    camera_pan_tilt(DIR_NEGATIVE, 10, DIR_STOP, 0);
    sleep(1);

    printf("[test] Stopping...\n");
    camera_stop();

    printf("[test] Test complete\n");
    return 0;
}

static time_t last_camera_poll = 0;

static void poll_camera(void) {
    time_t now = time(NULL);
    if (now - last_camera_poll < CAMERA_POLL_INTERVAL_SEC) return;
    last_camera_poll = now;

    pthread_mutex_lock(&camera_mutex);
    if (!camera_is_connected()) {
        if (camera_open() == 0) {
            printf("[main] Camera connected\n");
            camera_status_t st = camera_get_status();
            server_broadcast_status(st.connected, st.zoom);
        }
    }
    pthread_mutex_unlock(&camera_mutex);
}

static void *control_thread(void *arg) {
    (void)arg;
    unsigned ticks = 0;
    while (running) {
        control_service();
        if (++ticks % 10 == 0) server_wake();
        usleep(5000);  // 5ms = 200Hz
    }
    server_wake();
    return NULL;
}

int main(int argc, char **argv) {
    // Keep app-owned daemon logs visible when stdout is a pipe.
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (parse_args(argc, argv) != 0) {
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // A client may disconnect before a TCP response is written.
    signal(SIGPIPE, SIG_IGN);

    if (link2_pro) camera_select_link2_pro();
    if (auto_camera) camera_select_auto();

    // Initialize camera subsystem
    if (camera_init() != 0) {
        return 1;
    }

    if (settings_mode) {
        camera_open();
        cJSON *result = camera_settings_get();
        cJSON *type = cJSON_GetObjectItemCaseSensitive(result, "type");
        int rc = cJSON_IsString(type) && strcmp(type->valuestring, "settings") == 0 ? 0 : 1;
        char *json = cJSON_PrintUnformatted(result);
        if (json) {
            puts(json);
            free(json);
        } else rc = 1;
        cJSON_Delete(result);
        camera_destroy();
        return rc;
    }

    if (status_mode) {
        camera_open();
        camera_status_t st = camera_get_status();
        printf("{\"camera_connected\":%s,\"zoom\":%u}\n",
               st.connected ? "true" : "false", st.zoom);
        camera_destroy();
        return st.connected ? 0 : 1;
    }

    if (test_mode) {
        int rc = run_test();
        camera_destroy();
        return rc;
    }

    // Try initial camera connection
    if (camera_open() == 0) {
        printf("[main] Camera found on startup\n");
    } else {
        printf("[main] Camera not found — will poll every %ds\n",
               CAMERA_POLL_INTERVAL_SEC);
    }

    // Start WebSocket server
    if (server_init(port, loopback_only) != 0) {
        camera_destroy();
        return 1;
    }

    // Advertise via mDNS
    if (use_mdns) {
        if (mdns_register(port) != 0) {
            fprintf(stderr, "[main] mDNS registration failed (continuing without)\n");
        }
    }

    // Start TCP control socket for CLI (runs in its own thread)
    if (use_control && control_init(DEFAULT_CONTROL_PORT) != 0) {
        fprintf(stderr, "[main] Control socket failed (continuing without)\n");
    }

    // Control socket thread — decoupled from lws event loop
    pthread_t ctrl_thread;
    if (pthread_create(&ctrl_thread, NULL, control_thread, NULL) != 0) {
        fprintf(stderr, "[main] Failed to start control thread\n");
        control_destroy();
        if (use_mdns) mdns_unregister();
        server_destroy();
        camera_destroy();
        return 1;
    }

    // The desktop app waits for this owned-process readiness line before
    // connecting. Emit it only after the WebSocket listener is bound.
    printf("[main] linkctl-daemon running on port %u\n", port);

    // Main event loop (WebSocket + camera only)
    while (running) {
        server_service(50);  // 50ms poll
        poll_camera();
    }

    printf("\n[main] Shutting down...\n");

    pthread_join(ctrl_thread, NULL);
    control_destroy();
    if (use_mdns) mdns_unregister();
    camera_stop();
    server_destroy();
    camera_destroy();

    return 0;
}
