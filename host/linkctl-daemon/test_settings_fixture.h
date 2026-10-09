#ifndef TEST_SETTINGS_FIXTURE_H
#define TEST_SETTINGS_FIXTURE_H

#include "settings.h"

typedef struct {
    uint8_t unit, selector, width, info, fail_request;
    int64_t value, minimum, maximum, step, default_value;
} fake_control_t;

typedef struct {
    settings_t settings;
    fake_control_t controls[16];
    unsigned count, reads, writes, mode_reads;
    uint8_t last_data[SETTINGS_MODE_LENGTH], last_width, last_interface, last_unit, last_selector;
    bool disconnected, fail_readback, clamp_write, ignore_mode_write, transition_mode_write;
    uint16_t mode_length;
    uint8_t mode_payload[SETTINGS_MODE_LENGTH];
    uint8_t descriptors[256];
    size_t descriptor_length;
} fake_camera_t;

void fake_camera_init(fake_camera_t *camera, bool exposure, uint16_t version);
void fake_camera_init_modes(fake_camera_t *camera);
fake_control_t *fake_control(fake_camera_t *camera, uint8_t unit, uint8_t selector);

#endif
