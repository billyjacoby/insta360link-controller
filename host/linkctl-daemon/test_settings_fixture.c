#include "test_settings_fixture.h"

#include <assert.h>
#include <string.h>

fake_control_t *fake_control(fake_camera_t *f, uint8_t unit, uint8_t selector) {
    for (unsigned i = 0; i < f->count; ++i)
        if (f->controls[i].unit == unit && f->controls[i].selector == selector) return &f->controls[i];
    return NULL;
}

static int fake_io(void *context, uint8_t interface, uint8_t unit,
                    uint8_t selector, uint8_t request, uint8_t *data, uint16_t width) {
    fake_camera_t *f = context;
    assert(interface == 3); // Must use the descriptor, not interface zero.
    fake_control_t *c = fake_control(f, unit, selector);
    assert(c); // Never address unadvertised controls.
    assert(width == (request == 0x86 ? 1 : request == 0x85 ? 2 : c->width));
    bool mode = c->width == SETTINGS_MODE_LENGTH;
    if (mode) {
        assert(unit == 9);
        assert(selector == 2);
        assert(request == 1 || request == 0x81 || request == 0x85 || request == 0x86);
    } else {
        assert(request != 0x85); // GET_LEN is not a standard-control query.
    }
    if (f->disconnected) return SETTINGS_IO_DISCONNECTED;
    if (request == 1) {
        ++f->writes;
        if (c->fail_request == request) return -1;
        memcpy(f->last_data, data, width);
        f->last_width = width;
        f->last_interface = interface;
        f->last_unit = unit;
        f->last_selector = selector;
        if (mode) {
            if (!f->ignore_mode_write) memcpy(f->mode_payload, data, width);
            if (f->transition_mode_write) f->mode_payload[0] = 255;
            if (f->fail_readback) c->fail_request = 0x81;
            return 0;
        }
        uint32_t raw = 0;
        for (unsigned i = 0; i < width; ++i) raw |= (uint32_t)data[i] << (8 * i);
        c->value = (unit == 9 || unit == 5) && (selector == 2 || selector == 6) ?
            (int16_t)raw : (int64_t)raw;
        if (f->clamp_write) c->value = c->minimum;
        if (f->fail_readback) c->fail_request = 0x81;
        return 0;
    }
    ++f->reads;
    if (mode) ++f->mode_reads;
    if (request == c->fail_request) return -1;
    if (mode && request == 0x81) {
        memcpy(data, f->mode_payload, width);
        return 0;
    }
    int64_t value;
    switch (request) {
    case 0x81: value = c->value; break;
    case 0x82: assert(c->width != 1); value = c->minimum; break;
    case 0x83: assert(c->width != 1); value = c->maximum; break;
    case 0x84: assert(c->width != 1 || (unit == 7 && selector == 2)); value = c->step; break;
    case 0x85: assert(mode); value = f->mode_length; break;
    case 0x86: value = c->info; break;
    case 0x87: assert(!(unit == 7 && selector == 3)); value = c->default_value; break;
    default: assert(0); return -1;
    }
    for (unsigned i = 0; i < width; ++i) data[i] = (uint32_t)value >> (8 * i);
    return 0;
}

void fake_camera_init(fake_camera_t *f, bool exposure, uint16_t version) {
    memset(f, 0, sizeof(*f));
    uint8_t descriptors[] = {
        9, 2, 0, 0, 1, 1, 0, 0x80, 50,
        9, 4, 3, 0, 0, 0x0e, 1, 0, 0,
        12, 0x24, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        18, 0x24, 2, 7, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0x20, 0, 2,
        12, 0x24, 5, 9, 7, 0, 0, 3, 0x5f, 0x14, 0, 0,
        // Extension-unit bits must never become standard settings.
        26, 0x24, 6, 22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        8, 1, 9, 1, 0xff, 0,
    };
    descriptors[2] = sizeof(descriptors);
    descriptors[21] = version & 255;
    descriptors[22] = version >> 8;
    if (exposure) descriptors[45] |= 0x0e;
    const fake_control_t controls[] = {
        {7, 6, 2, 3, 0, 50, 0, 100, 1, 50},
        {7, 8, 1, 3, 0, 1, 0, 1, 1, 1},
        {9, 2, 2, 3, 0, -4, -10, 10, 2, 0},
        {9, 3, 2, 3, 0, 50, 0, 100, 1, 50},
        {9, 6, 2, 3, 0, -5, -15, 15, 1, 0},
        {9, 7, 2, 3, 0, 50, 0, 100, 1, 50},
        {9, 8, 2, 3, 0, 50, 0, 100, 1, 50},
        {9, 5, 1, 3, 0, 2, 0, 2, 1, 2},
        {9, 10, 2, 3, 0, 5000, 2000, 10000, 10, 6400},
        {9, 11, 1, 3, 0, 1, 0, 1, 1, 1},
        {7, 2, 1, 3, 0, 2, 1, 8, 15, 2},
        {7, 4, 4, 3, 0, 100, 1, UINT32_MAX, 1, 100},
        {7, 3, 1, 3, 0, 0, 0, 1, 1, 0},
    };
    f->count = exposure ? 13 : 10;
    memcpy(f->controls, controls, sizeof(controls));
    settings_init(&f->settings, fake_io, f);
    memcpy(f->descriptors, descriptors, sizeof(descriptors));
    f->descriptor_length = sizeof(descriptors);
    assert(settings_discover(&f->settings, f->descriptors, f->descriptor_length) == 0);
    assert(f->settings.count == f->count);
}

void fake_camera_init_modes(fake_camera_t *f) {
    fake_camera_init(f, false, 0x110);
    // Move the fixture's processing unit so unit 9 can be the Pro's XU.
    for (size_t pos = 0; pos < f->descriptor_length; pos += f->descriptors[pos]) {
        uint8_t *d = f->descriptors + pos;
        if (d[1] == 0x24 && d[2] == 5) d[3] = 5;
    }
    for (unsigned i = 0; i < f->count; ++i)
        if (f->controls[i].unit == 9) f->controls[i].unit = 5;
    // Exact Link 2 Pro XU9 descriptor, on fixture VC interface 3 rather than 0.
    const uint8_t xu[] = {
        29, 0x24, 6, 9, 0x2d, 0x67, 0xf1, 0xfa, 0x1b, 0xb7, 0x93, 0x47,
        0x8c, 0x91, 0x7b, 0x1c, 0x9b, 0x7f, 0x95, 0xf8,
        11, 1, 5, 4, 0xff, 0xff, 0xff, 0xff, 0
    };
    memcpy(f->descriptors + f->descriptor_length, xu, sizeof(xu));
    f->descriptor_length += sizeof(xu);
    f->descriptors[2] = f->descriptor_length;
    f->descriptors[3] = f->descriptor_length >> 8;
    f->controls[f->count++] = (fake_control_t){9, 2, SETTINGS_MODE_LENGTH, 3, 0, 0, 0, 6, 1, 0};
    f->mode_length = SETTINGS_MODE_LENGTH;
    // Distinct nonzero bytes catch destructive zero-fill or short mode writes.
    for (unsigned i = 2; i < sizeof(f->mode_payload); ++i) f->mode_payload[i] = 7 * i + 19;
    f->mode_payload[1] = 3; // Normal legitimately retains stale Whiteboard flags.
    assert(settings_discover_for_device(&f->settings, f->descriptors, f->descriptor_length,
                                       0x2e1a, 0x4c06) == 0);
    assert(f->settings.count == f->count);
}
