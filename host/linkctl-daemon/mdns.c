#include "mdns.h"
#include <stdio.h>

// Stub — implemented in Phase 3

int mdns_register(uint16_t port) {
    (void)port;
    printf("[mdns] stub register (port %u)\n", port);
    return 0;
}

void mdns_unregister(void) {
}
