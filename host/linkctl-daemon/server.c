#include "server.h"
#include <stdio.h>

// Stub — implemented in Phase 2

int server_init(uint16_t port) {
    (void)port;
    printf("[server] stub init (port %u)\n", port);
    return 0;
}

void server_service(int timeout_ms) {
    (void)timeout_ms;
}

void server_broadcast_status(bool camera_connected, uint16_t zoom) {
    (void)camera_connected;
    (void)zoom;
}

void server_broadcast_error(const char *message) {
    (void)message;
}

void server_destroy(void) {
}
