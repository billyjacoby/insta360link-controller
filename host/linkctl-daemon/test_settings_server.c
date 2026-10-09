// Link the real WebSocket server to a fake camera. Never links IOKit or camera.c.
#include "camera.h"
#include "server.h"
#include "test_settings_fixture.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

pthread_mutex_t camera_mutex = PTHREAD_MUTEX_INITIALIZER;
static fake_camera_t fake;
static bool connected = true;
static unsigned ptz_writes;
static atomic_bool running = true;

camera_status_t camera_get_status(void) {
    return (camera_status_t){.connected = connected, .zoom = connected ? 100 : 0};
}
cJSON *camera_settings_get(void) {
    return connected ? settings_snapshot(&fake.settings) : settings_error("Camera not connected");
}
cJSON *camera_setting_set(const char *id, int64_t value) {
    return connected ? settings_set(&fake.settings, id, value) : settings_error("Camera not connected");
}
int camera_pan_tilt(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    (void)a; (void)b; (void)c; (void)d; ++ptz_writes; return 0;
}
int camera_stop(void) { ++ptz_writes; return 0; }
int camera_center(void) { ++ptz_writes; return 0; }
int camera_zoom(uint16_t value) { (void)value; ++ptz_writes; return 0; }

static void stop(int sig) { (void)sig; running = false; }
static void *wake(void *arg) {
    (void)arg;
    while (running) { usleep(10000); server_wake(); }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    fake_camera_init(&fake, false, 0x110);
    if (argc > 2) {
        if (strcmp(argv[2], "disconnected") == 0) connected = false;
        else {
            fake_camera_init_modes(&fake);
            fake.ignore_mode_write = strcmp(argv[2], "mode-unchanged") == 0;
            fake.transition_mode_write = strcmp(argv[2], "mode-transition") == 0;
        }
    }
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    if (server_init((uint16_t)atoi(argv[1]), true)) return 1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, wake, NULL)) return 1;
    while (running) server_service(10);
    pthread_join(thread, NULL);
    server_destroy();
    printf("USB writes: %u; PTZ writes: %u\n", fake.writes, ptz_writes);
    return 0;
}
