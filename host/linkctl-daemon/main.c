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

#define CAMERA_POLL_INTERVAL_SEC 5

static atomic_bool running = true;
static bool use_mdns = true;
static uint16_t port = DEFAULT_PORT;
static bool test_mode = false;

static void handle_signal(int sig) {
    (void)sig;
    running = false;
}

static void print_usage(const char *name) {
    fprintf(stderr, "Usage: %s [options]\n", name);
    fprintf(stderr, "  --port <port>   WebSocket port (default: %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  --no-mdns       Disable mDNS advertisement\n");
    fprintf(stderr, "  --test          Test camera control and exit\n");
    fprintf(stderr, "  --help          Show this help\n");
}

static int parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-mdns") == 0) {
            use_mdns = false;
        } else if (strcmp(argv[i], "--test") == 0) {
            test_mode = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
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

    if (!camera_is_connected()) {
        if (camera_open() == 0) {
            printf("[main] Camera connected\n");
            camera_status_t st = camera_get_status();
            server_broadcast_status(st.connected, st.zoom);
        }
    }
}

static void *control_thread(void *arg) {
    (void)arg;
    while (running) {
        control_service();
        usleep(5000);  // 5ms = 200Hz
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (parse_args(argc, argv) != 0) {
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Initialize camera subsystem
    if (camera_init() != 0) {
        return 1;
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
    if (server_init(port) != 0) {
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
    if (control_init(DEFAULT_CONTROL_PORT) != 0) {
        fprintf(stderr, "[main] Control socket failed (continuing without)\n");
    }

    // Control socket thread — decoupled from lws event loop
    pthread_t ctrl_thread;
    pthread_create(&ctrl_thread, NULL, control_thread, NULL);

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
