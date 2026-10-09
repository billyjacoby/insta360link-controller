#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <stdint.h>

// Default WebSocket server port
#define DEFAULT_PORT 9000

// Initialize the WebSocket server. loopback_only binds IPv4 127.0.0.1 for the app.
// Returns 0 on success.
int server_init(uint16_t port, bool loopback_only);

// Run one iteration of the server event loop.
// timeout_ms: max time to wait for events (0 = non-blocking).
void server_service(int timeout_ms);

// Thread-safe wakeup so camera polling and shutdown run even without clients.
void server_wake(void);

// Send a status update to all connected clients.
void server_broadcast_status(bool camera_connected, uint16_t zoom);

// Send an error message to all connected clients.
void server_broadcast_error(const char *message);

// Shut down the server.
void server_destroy(void);

#endif
