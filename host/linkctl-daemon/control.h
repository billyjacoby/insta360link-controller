#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

// Default TCP control port (separate from WebSocket on 9000)
#define DEFAULT_CONTROL_PORT 9001

// Initialize the TCP control socket. Returns 0 on success.
int control_init(uint16_t port);

// Non-blocking: accept new connections, read commands, dispatch.
void control_service(void);

// Shut down the control socket.
void control_destroy(void);

#endif
