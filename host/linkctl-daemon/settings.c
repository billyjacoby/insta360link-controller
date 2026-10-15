#include "settings.h"

#include <math.h>
#include <string.h>

enum { SET_CUR = 0x01, GET_CUR = 0x81, GET_MIN, GET_MAX, GET_RES,
       GET_LEN, GET_INFO, GET_DEF };
enum { INTEGER, BOOLEAN, POWER_LINE, EXPOSURE_MODE, AI_MODE, GESTURE };

typedef struct {
    const char *id, *label, *group, *unit;
    uint8_t subtype, bit, selector, width, kind, value_bit;
    bool signed_value;
} definition_t;

// Vendor controls must additionally pass the model and descriptor gates below.
static const definition_t definitions[] = {
    {"brightness", "Brightness", "image", "", 5, 0, 2, 2, INTEGER, 0, true},
    {"contrast", "Contrast", "image", "", 5, 1, 3, 2, INTEGER, 0, false},
    {"hue", "Hue", "image", "degrees", 5, 2, 6, 2, INTEGER, 0, true},
    {"saturation", "Saturation", "image", "", 5, 3, 7, 2, INTEGER, 0, false},
    {"sharpness", "Sharpness", "image", "", 5, 4, 8, 2, INTEGER, 0, false},
    {"gamma", "Gamma", "image", "", 5, 5, 9, 2, INTEGER, 0, false},
    {"gain", "Gain", "image", "", 5, 9, 4, 2, INTEGER, 0, false},
    {"backlight_compensation", "Backlight compensation", "image", "", 5, 8, 1, 2, INTEGER, 0, false},
    {"power_line_frequency", "Power line frequency", "image", "", 5, 10, 5, 1, POWER_LINE, 0, false},
    {"white_balance_temperature", "White balance temperature", "white_balance", "K", 5, 6, 10, 2, INTEGER, 0, false},
    {"white_balance_auto", "Automatic white balance", "white_balance", "", 5, 12, 11, 1, BOOLEAN, 0, false},
    {"focus_absolute", "Focus", "focus", "", 2, 5, 6, 2, INTEGER, 0, false},
    {"focus_auto", "Autofocus", "focus", "", 2, 17, 8, 1, BOOLEAN, 0, false},
    {"exposure_mode", "Exposure mode", "exposure", "", 2, 1, 2, 1, EXPOSURE_MODE, 0, false},
    {"exposure_absolute", "Exposure time", "exposure", "100 us", 2, 3, 4, 4, INTEGER, 0, false},
    {"exposure_auto_priority", "Variable frame rate", "exposure", "", 2, 2, 3, 1, BOOLEAN, 0, false},
    {"ai_mode", "AI mode", "ai", "", 6, 1, 2, SETTINGS_MODE_LENGTH, AI_MODE, 0, false},
    {"gesture_control", "Enable gesture control", "gestures", "", 6, 26, 0x1b, 2, GESTURE, 4, false},
    {"gesture_palm", "Palm gesture", "gestures", "", 6, 4, 5, 1, GESTURE, 1, false},
    {"gesture_zoom", "Zoom gesture", "gestures", "", 6, 4, 5, 1, GESTURE, 2, false},
    {"gesture_whiteboard", "Whiteboard gesture", "gestures", "", 6, 4, 5, 1, GESTURE, 3, false},
};

// Pro-specific mode mapping and read-modify-write protocol, tested upstream
// on PID 4c06 (firmware 0.3.0.8):
// https://github.com/tagawa0525/insta360-link2pro-linux/blob/main/link2pro.py
// https://github.com/tagawa0525/insta360-link2pro-linux/blob/main/references/README.md
// Our read-only probe confirms this XU9 GUID, bitmap, INFO=3 and LEN=61.
// The original Link and Link 2 use different payloads and remain excluded.
// Gesture selectors are established by static inspection of the macOS SDK at commit
// 2dd2c78186b0335a530042ae49accbef249d6a95: EnableAllGesture uses selector
// 0x1b, LE16 length 2 and bit 4; EnableGestureWork uses selector 5, length 1,
// with palm/zoom/whiteboard in bits 1/2/3. This protocol is not hardware tested.
static const uint8_t mode_guid[] = {
    0x2d, 0x67, 0xf1, 0xfa, 0x1b, 0xb7, 0x93, 0x47,
    0x8c, 0x91, 0x7b, 0x1c, 0x9b, 0x7f, 0x95, 0xf8
};
enum { AI_MODE_DEFINITION = 16, GESTURE_MASTER_DEFINITION = 17,
       GESTURE_PALM_DEFINITION = 18, GESTURE_ZOOM_DEFINITION = 19,
       GESTURE_WHITEBOARD_DEFINITION = 20 };

static bool xu_descriptor(const uint8_t *d, size_t n, unsigned selector) {
    if (!selector || n < 24 || d[3] != 9 || memcmp(d + 4, mode_guid, sizeof(mode_guid)))
        return false;
    size_t pins = d[21];
    if (!pins || n < 24 + pins) return false;
    size_t bytes = d[22 + pins];
    unsigned bit = selector - 1;
    return bit / 8 < bytes && n == 24 + pins + bytes &&
           (d[23 + pins + bit / 8] & (1u << (bit % 8)));
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

void settings_init(settings_t *s, settings_io_fn io, void *context) {
    memset(s, 0, sizeof(*s));
    s->io = io;
    s->context = context;
}

int settings_discover(settings_t *s, const uint8_t *data, size_t length) {
    return settings_discover_for_device(s, data, length, 0, 0);
}

int settings_discover_for_device(settings_t *s, const uint8_t *data, size_t length,
                                uint16_t vendor, uint16_t product) {
    s->count = 0;
    s->error = "Invalid UVC settings descriptors";
    if (!data || length < 9 || data[0] < 9 || data[1] != 2 ||
        le16(data + 2) != length) return -1;

    bool in_vc = false, seen_vc = false, seen_ct = false, seen_pu = false;
    uint8_t interface = 0;
    uint16_t version = 0;
    bool vendor_candidate[5] = {false};
    unsigned mode_entities = 0;
    uint8_t mode_interface = 0;
    for (size_t pos = 0; pos < length;) {
        const uint8_t *d = data + pos;
        if (length - pos < 2 || d[0] < 2 || d[0] > length - pos) goto invalid;
        size_t n = d[0];
        if (d[1] == 4) {
            if (n < 9) goto invalid;
            in_vc = d[3] == 0 && d[5] == 0x0e && d[6] == 1;
            if (in_vc) {
                // Ambiguous functions / entities are not safe write targets.
                if (seen_vc) goto invalid;
                seen_vc = true;
                interface = d[2];
            }
        } else if (in_vc && d[1] == 0x24) {
            if (n < 3) goto invalid;
            // A duplicate entity ID makes the vendor address ambiguous. Keep
            // image controls available, but do not expose the vendor setting.
            if (n >= 4 && d[2] >= 2 && d[2] <= 6 && d[3] == 9)
                ++mode_entities;
            if (d[2] == 1) {
                if (n < 12) goto invalid;
                version = le16(d + 3);
            } else if (d[2] == 6) {
                if (vendor == 0x2e1a && product == 0x4c06) {
                    const unsigned selectors[] = {2, 0x1b, 5, 5, 5};
                    for (unsigned i = 0; i < sizeof(selectors) / sizeof(selectors[0]); ++i)
                        vendor_candidate[i] |= xu_descriptor(d, n, selectors[i]);
                    mode_interface = interface;
                }
            } else if (d[2] == 2 || d[2] == 5) {
                size_t offset, bytes;
                if (d[2] == 2) {
                    if (n < 8) goto invalid;
                    if (le16(d + 4) != 0x0201) { pos += n; continue; }
                    if (n < 15 || seen_ct) goto invalid;
                    seen_ct = true;
                    offset = 15;
                    bytes = d[14];
                } else {
                    if (n < 8 || seen_pu) goto invalid;
                    seen_pu = true;
                    offset = 8;
                    bytes = d[7];
                }
                if (!version || !d[3] || bytes > n - offset) goto invalid;
                for (unsigned i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
                    const definition_t *def = &definitions[i];
                    if (def->subtype != d[2] || def->bit / 8 >= bytes ||
                        !(d[offset + def->bit / 8] & (1u << (def->bit % 8)))) continue;
                    if (s->count == SETTINGS_MAX_CONTROLS) goto invalid;
                    s->controls[s->count++] = (setting_control_t){
                        .definition = i, .interface = interface, .unit = d[3], .version = version
                    };
                }
            }
        }
        pos += n;
    }
    if (!seen_vc || !version) goto invalid;
    if (mode_entities == 1) {
        const unsigned vendor_definitions[] = {AI_MODE_DEFINITION, GESTURE_MASTER_DEFINITION,
            GESTURE_PALM_DEFINITION, GESTURE_ZOOM_DEFINITION, GESTURE_WHITEBOARD_DEFINITION};
        for (unsigned i = 0; i < sizeof(vendor_definitions) / sizeof(vendor_definitions[0]); ++i) {
            if (vendor_candidate[i]) {
                if (s->count == SETTINGS_MAX_CONTROLS) goto invalid;
                s->controls[s->count++] = (setting_control_t){.definition = vendor_definitions[i],
                    .interface = mode_interface, .unit = 9, .version = version};
            }
        }
    }
    s->error = NULL;
    return 0;
invalid:
    s->count = 0;
    return -1;
}

static int transfer(settings_t *s, setting_control_t *c, uint8_t request,
                    uint8_t *data, uint16_t length) {
    if (s->disconnected) return -1;
    const definition_t *d = &definitions[c->definition];
    int rc = s->io(s->context, c->interface, c->unit, d->selector, request, data, length);
    if (rc == SETTINGS_IO_DISCONNECTED) s->disconnected = true;
    return rc;
}

static int query(settings_t *s, setting_control_t *c, uint8_t request,
                 int64_t *value) {
    const definition_t *d = &definitions[c->definition];
    uint8_t data[4] = {0};
    unsigned width = request == GET_INFO ? 1 : request == GET_LEN ? 2 : d->width;
    if (width > sizeof(data) || transfer(s, c, request, data, width)) return -1;
    uint32_t raw = 0;
    for (unsigned i = 0; i < width; ++i) raw |= (uint32_t)data[i] << (8 * i);
    *value = request != GET_INFO && d->signed_value ? (int16_t)raw : (int64_t)raw;
    return 0;
}

static bool valid_value(const setting_control_t *c, int64_t value) {
    if (!c->valid_range || value < c->minimum || value > c->maximum) return false;
    if (c->options) return value < 32 && (c->options & (1u << value));
    return c->step > 0 && (value - c->minimum) % c->step == 0;
}

static void refresh_mode(settings_t *s, setting_control_t *c) {
    c->minimum = 0;
    c->maximum = 6;
    c->step = 1;
    c->options = (1u << 0) | (1u << 1) | (1u << 4) | (1u << 5) | (1u << 6);
    c->valid_range = true;
    int64_t info = 0, length = 0;
    if (query(s, c, GET_INFO, &info)) {
        c->error = "AI mode GET_INFO failed";
        return;
    }
    c->readable = (info & 1) != 0;
    if (query(s, c, GET_LEN, &length) || length != SETTINGS_MODE_LENGTH) {
        c->error = "AI mode GET_LEN failed or reported an unsupported length";
        return;
    }
    if (!c->readable || transfer(s, c, GET_CUR, c->payload, sizeof(c->payload))) {
        c->error = "AI mode current value could not be read";
        return;
    }
    c->has_value = true;
    c->value = c->payload[0];
    if (c->value == 255) return; // Expected pending state, not a camera failure.
    if (!valid_value(c, c->value)) {
        c->error = "Camera returned an unsupported AI mode";
        return;
    }
    c->active = (info & 0x24) == 0;
    // Only confirmed synchronous GET/SET capability can use this write path.
    c->writable = info == 3;
    if (!c->writable) c->error = "AI mode is read-only or disabled by camera";
    // byte[1] is meaningful only within its mode; Normal can retain stale flags.
    if (c->value == 4 && c->payload[1] == 3)
        c->error = "Whiteboard not detected; check that the board is visible";
    else if (c->value == 4 && c->payload[1] > 3)
        c->error = "Camera returned an unrecognized whiteboard detection state";
}

static void refresh_gesture(settings_t *s, setting_control_t *c) {
    const definition_t *d = &definitions[c->definition];
    c->minimum = 0;
    c->maximum = 1;
    c->step = 1;
    c->valid_range = true;
    int64_t info = 0, length = 0;
    if (query(s, c, GET_INFO, &info)) {
        c->error = "Gesture GET_INFO failed";
        return;
    }
    c->readable = (info & 1) != 0;
    c->active = (info & 0x24) == 0;
    if (query(s, c, GET_LEN, &length) || length != d->width) {
        c->error = "Gesture GET_LEN failed or reported an unsupported length";
        return;
    }
    if (!c->readable || transfer(s, c, GET_CUR, c->payload, d->width)) {
        c->error = "Gesture current value could not be read";
        return;
    }
    c->has_value = true;
    c->value = (c->payload[d->value_bit / 8] >> (d->value_bit % 8)) & 1;
    // Only an exact synchronous GET/SET capability is safe for RMW writes.
    c->writable = info == 3 && c->active;
    if (!c->writable) c->error = "Gesture control is read-only or disabled by camera";
}

static const char *mode_detail(const setting_control_t *c) {
    if (!c->has_value) return "Link 2 Pro AI modes require an active video stream.";
    if (c->value == 255)
        return "Changing mode. Keep video streaming; AI tracking needs a visible person.";
    if (c->value == 4) {
        if (c->payload[1] == 0) return "Whiteboard: starting automatic detection. Keep video streaming.";
        if (c->payload[1] == 1) return "Whiteboard: searching for a board. Keep video streaming.";
        if (c->payload[1] == 2) return "Whiteboard: board locked; perspective correction active.";
        if (c->payload[1] == 3) return "Whiteboard: detection failed; camera may return to Normal.";
        return "Whiteboard: camera reported an unrecognized detection state.";
    }
    if (c->value == 6)
        return "DeskView rotates the image; aim the camera at your desk with tilt controls. Requires video streaming.";
    if (c->value == 5)
        return "Overhead points the camera straight down and rotates the image. Requires video streaming.";
    if (c->value == 1)
        return "AI tracking needs a visible person. Modes reset to Normal shortly after video streaming stops.";
    if (c->value != 0) return "Camera reported an unsupported AI mode; writes are disabled.";
    return "Requires video streaming. Select Normal before switching between AI modes; modes reset when streaming stops.";
}

static setting_control_t *find(settings_t *s, const char *id) {
    for (size_t i = 0; i < s->count; ++i)
        if (strcmp(definitions[s->controls[i].definition].id, id) == 0) return &s->controls[i];
    return NULL;
}

static void dependency(settings_t *s, const char *manual, const char *automatic,
                       unsigned allowed_values) {
    setting_control_t *m = find(s, manual), *a = find(s, automatic);
    if (m && a && (!a->has_value || a->value < 0 || a->value >= 32 ||
                  !(allowed_values & (1u << a->value)))) {
        m->active = false;
        m->writable = false;
        m->error = "Disabled by automatic control (or its state could not be read)";
    }
}

static int refresh(settings_t *s) {
    if (s->error || s->disconnected || !s->io) return -1;
    for (size_t i = 0; i < s->count; ++i) {
        setting_control_t *c = &s->controls[i];
        const definition_t *d = &definitions[c->definition];
        c->readable = c->writable = c->active = c->has_value = c->has_default = false;
        c->valid_range = false;
        c->minimum = c->maximum = c->step = 0;
        c->options = 0;
        c->error = NULL;
        if (d->kind == AI_MODE) {
            refresh_mode(s, c);
            continue;
        }
        if (d->kind == GESTURE) {
            refresh_gesture(s, c);
            continue;
        }
        int64_t info = 0;
        if (query(s, c, GET_INFO, &info)) {
            c->error = "GET_INFO failed";
            continue;
        }
        c->readable = (info & 1) != 0;
        c->active = (info & 0x24) == 0; // Disabled by auto or commit state.
        if (c->readable) {
            c->has_value = query(s, c, GET_CUR, &c->value) == 0;
            // AE priority has no GET_DEF request in the UVC specification.
            if (!(d->subtype == 2 && d->selector == 3))
                c->has_default = query(s, c, GET_DEF, &c->default_value) == 0;
        }
        if (d->kind == INTEGER) {
            int min_rc = query(s, c, GET_MIN, &c->minimum);
            int max_rc = query(s, c, GET_MAX, &c->maximum);
            int res_rc = query(s, c, GET_RES, &c->step);
            c->valid_range = !min_rc && !max_rc && !res_rc &&
                c->minimum <= c->maximum && c->step > 0;
        } else if (d->kind == EXPOSURE_MODE) {
            int64_t modes = 0;
            if (!query(s, c, GET_RES, &modes) && modes > 0 && (modes & ~15) == 0) {
                c->options = ((modes & 1) ? 1u << 1 : 0) |
                             ((modes & 2) ? 1u << 2 : 0) |
                             ((modes & 4) ? 1u << 4 : 0) |
                             ((modes & 8) ? 1u << 8 : 0);
                c->minimum = (modes & 1) ? 1 : (modes & 2) ? 2 : (modes & 4) ? 4 : 8;
                c->maximum = (modes & 8) ? 8 : (modes & 4) ? 4 : (modes & 2) ? 2 : 1;
                c->step = 1;
                c->valid_range = true;
            }
        } else {
            // UVC defines fixed boolean / power-line domains and does not
            // support GET_MIN/MAX/RES for these controls (nor GET_LEN).
            c->minimum = 0;
            c->maximum = d->kind == BOOLEAN ? 1 : c->version >= 0x0150 ? 3 : 2;
            c->step = 1;
            c->valid_range = true;
            if (d->kind == POWER_LINE) c->options = (1u << (c->maximum + 1)) - 1;
        }
        if (!c->valid_range) {
            // Required numeric fields use zero sentinels, never invented ranges.
            c->minimum = c->maximum = c->step = 0;
            c->error = "Missing or invalid reported range";
        }
        bool value_valid = c->has_value && valid_value(c, c->value);
        if (c->has_value && c->valid_range && !value_valid) {
            // Keep the actual read value visible for diagnostics, but never
            // infer additional writable options from a nonconforming value.
            c->error = "Camera returned a value outside its supported range";
        }
        if (!c->has_value && !c->error) c->error = "Current value could not be read";
        if (c->has_default && !valid_value(c, c->default_value)) c->has_default = false;
        c->writable = (info & 2) && c->active && value_valid;
        if (!c->active && !c->error) c->error = "Disabled by camera";
        if (s->disconnected) return -1;
    }
    dependency(s, "focus_absolute", "focus_auto", 1u << 0);
    dependency(s, "white_balance_temperature", "white_balance_auto", 1u << 0);
    dependency(s, "exposure_absolute", "exposure_mode", (1u << 1) | (1u << 4));
    return s->disconnected ? -1 : 0;
}

cJSON *settings_error(const char *message) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "type", "settings_error");
    cJSON_AddStringToObject(result, "message", message);
    return result;
}

static cJSON *snapshot_json(settings_t *s) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "type", "settings");
    cJSON *controls = cJSON_AddArrayToObject(result, "controls");
    for (size_t i = 0; i < s->count; ++i) {
        setting_control_t *c = &s->controls[i];
        const definition_t *d = &definitions[c->definition];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToArray(controls, item);
        cJSON_AddStringToObject(item, "id", d->id);
        cJSON_AddStringToObject(item, "label", d->label);
        cJSON_AddStringToObject(item, "group", d->group);
        if (d->kind == AI_MODE) {
            cJSON_AddStringToObject(item, "detail", mode_detail(c));
            cJSON_AddBoolToObject(item, "pending", c->has_value &&
                (c->value == 255 || (c->value == 4 && c->payload[1] <= 1)));
        }
        cJSON_AddStringToObject(item, "kind", d->kind == INTEGER ? "integer" :
            (d->kind == BOOLEAN || d->kind == GESTURE) ? "boolean" : "enum");
        if (c->has_value) cJSON_AddNumberToObject(item, "value", (double)c->value);
        else cJSON_AddNullToObject(item, "value");
        cJSON_AddNumberToObject(item, "min", (double)c->minimum);
        cJSON_AddNumberToObject(item, "max", (double)c->maximum);
        cJSON_AddNumberToObject(item, "step", (double)c->step);
        if (c->has_default) cJSON_AddNumberToObject(item, "default", (double)c->default_value);
        else cJSON_AddNullToObject(item, "default");
        cJSON_AddBoolToObject(item, "readable", c->readable);
        cJSON_AddBoolToObject(item, "writable", c->writable);
        cJSON_AddBoolToObject(item, "active", c->active);
        cJSON_AddStringToObject(item, "unit", d->unit);
        cJSON *options = cJSON_AddArrayToObject(item, "options");
        for (unsigned value = 0; value < 16; ++value) {
            if (!(c->options & (1u << value))) continue;
            const char *label = d->kind == AI_MODE ?
                (value == 0 ? "Normal" : value == 1 ? "AI tracking" : value == 4 ? "Whiteboard (auto)" :
                 value == 5 ? "Overhead" : "DeskView") : d->kind == POWER_LINE ?
                (value == 0 ? "Off" : value == 1 ? "50 Hz" : value == 2 ? "60 Hz" : "Auto") :
                (value == 1 ? "Manual" : value == 2 ? "Auto" : value == 4 ? "Shutter priority" : "Aperture priority");
            cJSON *option = cJSON_CreateObject();
            cJSON_AddNumberToObject(option, "value", value);
            cJSON_AddStringToObject(option, "label", label);
            cJSON_AddItemToArray(options, option);
        }
        if (c->error) cJSON_AddStringToObject(item, "error", c->error);
        else cJSON_AddNullToObject(item, "error");
    }
    return result;
}

cJSON *settings_snapshot(settings_t *s) {
    if (refresh(s)) return settings_error(s->error ? s->error : "Camera disconnected");
    return snapshot_json(s);
}

cJSON *settings_set(settings_t *s, const char *id, int64_t value) {
    setting_control_t *c = id ? find(s, id) : NULL;
    if (!c) return settings_error("Unknown or unsupported setting ID");
    const definition_t *d = &definitions[c->definition];
    // No numeric narrowing until all validation has succeeded.
    if (value < INT16_MIN || value > UINT32_MAX) return settings_error("Invalid setting value");
    if (refresh(s)) return settings_error(s->error ? s->error : "Camera disconnected");
    if (d->kind == AI_MODE && c->has_value && c->value == 255)
        return settings_error("Camera is changing mode; wait for the transition to finish");
    if (!c->writable) return settings_error(c->error ? c->error : "Setting is read-only");
    if (!valid_value(c, value)) return settings_error("Setting value is outside its range, step, or options");
    uint8_t data[SETTINGS_MODE_LENGTH] = {0};
    if (d->kind == AI_MODE) {
        // Direct non-Normal mode transitions can get stuck on this firmware.
        if (value && c->value && value != c->value)
            return settings_error("Select Normal before switching between AI modes");
        // Re-entering Whiteboard would restart detection (including its failure
        // state). An already-selected mode needs no write.
        if (value == c->value) return snapshot_json(s);
        memcpy(data, c->payload, sizeof(c->payload));
        data[0] = (uint8_t)value;
        data[1] = value == 5 ? 3 : 0;
    } else if (d->kind == GESTURE) {
        // Preserve every undocumented flag in the selector payload.
        memcpy(data, c->payload, d->width);
        if (value) data[d->value_bit / 8] |= 1u << (d->value_bit % 8);
        else data[d->value_bit / 8] &= ~(1u << (d->value_bit % 8));
    } else {
        for (unsigned i = 0; i < d->width; ++i) data[i] = (uint32_t)value >> (8 * i);
    }
    int rc = transfer(s, c, SET_CUR, data, d->width);
    if (rc) return settings_error("Camera rejected setting write");
    if (refresh(s)) return settings_error("Camera disconnected during readback");
    // A transition is actual camera state, not confirmation of the final mode.
    // Clients can refresh settings while 255 is visible; no writes are retried.
    if (d->kind == AI_MODE && c->has_value && c->value == 255) return snapshot_json(s);
    if (!c->has_value || !valid_value(c, c->value))
        return settings_error("Setting was written but its readback failed or was invalid");
    if (d->kind == GESTURE && c->value != value)
        return settings_error("Gesture setting was written but its readback did not match");
    // Mode changes are asynchronous: even the previous mode can be read before
    // 255 appears. Return actual state; the client tracks its requested target
    // and bounds polling rather than treating this snapshot as completion.
    return snapshot_json(s);
}

bool settings_parse_set(const cJSON *request, const char **id, int64_t *value) {
    const cJSON *id_item = cJSON_GetObjectItemCaseSensitive(request, "id");
    const cJSON *value_item = cJSON_GetObjectItemCaseSensitive(request, "value");
    if (!cJSON_IsString(id_item) || !id_item->valuestring[0] ||
        !cJSON_IsNumber(value_item) || !isfinite(value_item->valuedouble) ||
        value_item->valuedouble < INT16_MIN || value_item->valuedouble > UINT32_MAX ||
        trunc(value_item->valuedouble) != value_item->valuedouble) return false;
    // Reject duplicate keys rather than interpreting a different request to clients.
    unsigned ids = 0, values = 0, commands = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, request) {
        if (!item->string) return false;
        ids += strcmp(item->string, "id") == 0;
        values += strcmp(item->string, "value") == 0;
        commands += strcmp(item->string, "cmd") == 0;
    }
    if (ids != 1 || values != 1 || commands != 1) return false;
    *id = id_item->valuestring;
    *value = (int64_t)value_item->valuedouble;
    return true;
}
