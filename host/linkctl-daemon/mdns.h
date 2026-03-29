#ifndef MDNS_H
#define MDNS_H

#include <stdbool.h>
#include <stdint.h>

// mDNS service type
#define MDNS_SERVICE_TYPE "_insta360ctrl._tcp"

// Register the mDNS service. Returns 0 on success.
int mdns_register(uint16_t port);

// Unregister the mDNS service.
void mdns_unregister(void);

#endif
