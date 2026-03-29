#include "mdns.h"

#include <dns_sd.h>
#include <stdio.h>
#include <string.h>

static DNSServiceRef service_ref = NULL;

static void register_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                               DNSServiceErrorType errorCode,
                               const char *name, const char *regtype,
                               const char *domain, void *context) {
    (void)sdRef;
    (void)flags;
    (void)context;

    if (errorCode == kDNSServiceErr_NoError) {
        printf("[mdns] Registered as '%s' (%s%s)\n", name, regtype, domain);
    } else {
        fprintf(stderr, "[mdns] Registration failed: %d\n", errorCode);
    }
}

int mdns_register(uint16_t port) {
    DNSServiceErrorType err;

    err = DNSServiceRegister(
        &service_ref,
        0,                          // flags
        kDNSServiceInterfaceIndexAny,
        NULL,                       // name (NULL = use hostname)
        MDNS_SERVICE_TYPE,
        NULL,                       // domain (NULL = default)
        NULL,                       // host (NULL = this machine)
        htons(port),
        0,                          // TXT record length
        NULL,                       // TXT record data
        register_callback,
        NULL                        // context
    );

    if (err != kDNSServiceErr_NoError) {
        fprintf(stderr, "[mdns] DNSServiceRegister failed: %d\n", err);
        return -1;
    }

    // Process the registration callback immediately
    DNSServiceProcessResult(service_ref);

    return 0;
}

void mdns_unregister(void) {
    if (service_ref) {
        DNSServiceRefDeallocate(service_ref);
        service_ref = NULL;
        printf("[mdns] Service unregistered\n");
    }
}
