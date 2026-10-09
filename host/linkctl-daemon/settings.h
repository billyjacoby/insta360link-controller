#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cjson/cJSON.h>

#define SETTINGS_MAX_CONTROLS 17
#define SETTINGS_IO_DISCONNECTED (-2)
#define SETTINGS_MODE_LENGTH 61

// Transport returns 0 only for an exact-length transfer, -2 on physical removal.
typedef int (*settings_io_fn)(void *context, uint8_t interface, uint8_t unit,
                              uint8_t selector, uint8_t request,
                              uint8_t *data, uint16_t length);

typedef struct {
    unsigned definition;
    uint8_t interface, unit;
    uint16_t version;
    bool readable, writable, active, has_value, has_default, valid_range;
    int64_t value, minimum, maximum, step, default_value;
    unsigned options;
    const char *error;
    // Full vendor mode state for read-modify-write; image controls do not use it.
    uint8_t payload[SETTINGS_MODE_LENGTH];
} setting_control_t;

typedef struct {
    settings_io_fn io;
    void *context;
    setting_control_t controls[SETTINGS_MAX_CONTROLS];
    size_t count;
    bool disconnected;
    const char *error;
} settings_t;

void settings_init(settings_t *settings, settings_io_fn io, void *context);
// Parse the active configuration supplied by the camera; never performs I/O.
// Without an exact device identity, only standard UVC settings are discovered.
int settings_discover(settings_t *settings, const uint8_t *data, size_t length);
int settings_discover_for_device(settings_t *settings, const uint8_t *data, size_t length,
                                uint16_t vendor, uint16_t product);
// Caller holds camera_mutex throughout refresh / validation / SET / readback.
cJSON *settings_snapshot(settings_t *settings);
cJSON *settings_set(settings_t *settings, const char *id, int64_t value);
cJSON *settings_error(const char *message);
// Validate JSON types and bounds before dispatching a camera operation.
bool settings_parse_set(const cJSON *request, const char **id, int64_t *value);

#endif
