#include "test_settings_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON *control(cJSON *snapshot, const char *id) {
    cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(snapshot, "controls"))
        if (strcmp(cJSON_GetObjectItem(item, "id")->valuestring, id) == 0) return item;
    return NULL;
}

static void type(cJSON *result, const char *expected) {
    assert(strcmp(cJSON_GetObjectItem(result, "type")->valuestring, expected) == 0);
}

static double number(cJSON *object, const char *key) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    assert(cJSON_IsNumber(item));
    return item->valuedouble;
}

static void rejected(fake_camera_t *f, const char *id, int64_t value) {
    unsigned writes = f->writes;
    cJSON *result = settings_set(&f->settings, id, value);
    type(result, "settings_error");
    assert(f->writes == writes);
    cJSON_Delete(result);
}

static void discovery_and_signed_values(void) {
    fake_camera_t f;
    fake_camera_init(&f, false, 0x110);
    cJSON *result = settings_snapshot(&f.settings);
    type(result, "settings");
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(result, "controls")) == 10);
    assert(!control(result, "exposure_mode") && !control(result, "gamma"));
    assert(number(control(result, "brightness"), "min") == -10);
    assert(number(control(result, "hue"), "value") == -5);
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(control(result, "focus_absolute"), "active")));
    assert(number(control(result, "power_line_frequency"), "max") == 2);
    assert(f.writes == 0);
    cJSON_Delete(result);
    rejected(&f, "brightness", -3); // Off-step, despite being in range.
    rejected(&f, "brightness", 11);
    rejected(&f, "brightness", INT64_MAX);
    unsigned reads = f.reads;
    rejected(&f, "extension_22", 0);
    assert(f.reads == reads);
    rejected(&f, "exposure_absolute", 100);
    rejected(&f, "power_line_frequency", 3);
    rejected(&f, "focus_auto", 2);
    rejected(&f, "focus_absolute", 30);
    rejected(&f, "white_balance_temperature", 4000);
    result = settings_set(&f.settings, "brightness", -6);
    type(result, "settings");
    assert(f.writes == 1 && f.last_width == 2 && f.last_unit == 9 && f.last_selector == 2);
    assert(f.last_data[0] == 0xfa && f.last_data[1] == 0xff);
    assert(number(control(result, "brightness"), "value") == -6);
    cJSON_Delete(result);
    f.clamp_write = true;
    result = settings_set(&f.settings, "brightness", 4);
    assert(number(control(result, "brightness"), "value") == -10); // Readback, not echo.
    cJSON_Delete(result);
}

static void auto_dependencies_and_exposure(void) {
    fake_camera_t f;
    fake_camera_init(&f, true, 0x150);
    cJSON *result = settings_set(&f.settings, "focus_auto", 0);
    assert(f.last_width == 1);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(control(result, "focus_absolute"), "writable")));
    cJSON_Delete(result);
    result = settings_set(&f.settings, "focus_absolute", 30);
    assert(f.last_width == 2 && f.last_unit == 7);
    cJSON_Delete(result);
    result = settings_set(&f.settings, "white_balance_auto", 0);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(control(result, "white_balance_temperature"), "writable")));
    cJSON_Delete(result);
    rejected(&f, "white_balance_temperature", 4001);
    result = settings_set(&f.settings, "white_balance_temperature", 4000);
    assert(f.last_width == 2 && f.last_data[0] == 0xa0 && f.last_data[1] == 0x0f);
    cJSON_Delete(result);
    rejected(&f, "exposure_absolute", 100);
    rejected(&f, "exposure_mode", 3);
    result = settings_set(&f.settings, "exposure_mode", 1);
    assert(f.last_width == 1);
    cJSON_Delete(result);
    result = settings_set(&f.settings, "exposure_absolute", UINT32_MAX);
    type(result, "settings");
    assert(f.last_width == 4 && f.last_data[3] == 0xff);
    assert(number(control(result, "exposure_absolute"), "value") == UINT32_MAX);
    cJSON_Delete(result);
    fake_control(&f, 7, 2)->step = 3; // Only manual + auto are supported.
    rejected(&f, "exposure_mode", 4);
    result = settings_set(&f.settings, "power_line_frequency", 3);
    type(result, "settings");
    cJSON_Delete(result);
}

static void failures_stay_local(void) {
    fake_camera_t f;
    fake_camera_init(&f, false, 0x110);
    fake_control_t *brightness = fake_control(&f, 9, 2);
    brightness->info = 1;
    rejected(&f, "brightness", 0);
    brightness->info = 7;
    rejected(&f, "brightness", 0);
    brightness->info = 0x23;
    rejected(&f, "brightness", 0);
    brightness->info = 3;
    brightness->step = 0;
    rejected(&f, "brightness", 0);
    brightness->step = 2;
    brightness->minimum = 30;
    rejected(&f, "brightness", 0);
    brightness->minimum = -10;
    const uint8_t failures[] = {0x81, 0x82, 0x83, 0x84, 0x86};
    for (unsigned i = 0; i < sizeof(failures); ++i) {
        brightness->fail_request = failures[i];
        rejected(&f, "brightness", 0);
        assert(!f.settings.disconnected);
        cJSON *result = settings_snapshot(&f.settings);
        type(result, "settings");
        assert(cJSON_IsTrue(cJSON_GetObjectItem(control(result, "contrast"), "writable")));
        cJSON_Delete(result);
    }
    brightness->fail_request = 0x87; // Default is optional.
    cJSON *result = settings_set(&f.settings, "brightness", 0);
    type(result, "settings");
    assert(cJSON_IsNull(cJSON_GetObjectItem(control(result, "brightness"), "default")));
    cJSON_Delete(result);
    brightness->fail_request = 1;
    result = settings_set(&f.settings, "brightness", 2);
    type(result, "settings_error");
    assert(!f.settings.disconnected);
    cJSON_Delete(result);
    brightness->fail_request = 0;
    f.fail_readback = true;
    result = settings_set(&f.settings, "brightness", 0);
    type(result, "settings_error");
    assert(!f.settings.disconnected);
    cJSON_Delete(result);
    f.disconnected = true;
    result = settings_snapshot(&f.settings);
    type(result, "settings_error");
    assert(f.settings.disconnected);
    cJSON_Delete(result);
    fake_camera_init(&f, false, 0x110);
    fake_control(&f, 7, 8)->fail_request = 0x81;
    rejected(&f, "focus_absolute", 30);
    fake_control(&f, 9, 11)->fail_request = 0x86;
    rejected(&f, "white_balance_temperature", 4000);
    fake_control(&f, 9, 5)->value = 3; // Nonconforming UVC 1.1 device value.
    result = settings_snapshot(&f.settings);
    cJSON *power_line = control(result, "power_line_frequency");
    assert(number(power_line, "value") == 3);
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(power_line, "writable")));
    assert(number(power_line, "max") == 2);
    cJSON_Delete(result);
    rejected(&f, "power_line_frequency", 2);
}

static void malformed_descriptors_and_requests(void) {
    fake_camera_t f;
    fake_camera_init(&f, false, 0x110);
    const uint8_t broken[] = {9, 2, 11, 0, 1, 1, 0, 0, 0, 0, 4};
    assert(settings_discover(&f.settings, broken, sizeof(broken)) == -1);
    assert(f.settings.count == 0 && f.writes == 0 && f.reads == 0);
    const char *bad[] = {
        "{}", "{\"cmd\":\"setting_set\",\"id\":\"brightness\"}",
        "{\"cmd\":\"setting_set\",\"id\":\"brightness\",\"value\":true}",
        "{\"cmd\":\"setting_set\",\"id\":\"brightness\",\"value\":1.5}",
        "{\"cmd\":\"setting_set\",\"id\":\"brightness\",\"value\":1e100}",
        "{\"cmd\":\"setting_set\",\"id\":\"brightness\",\"value\":\"4\"}",
        "{\"cmd\":\"setting_set\",\"id\":{},\"value\":4}",
        "{\"cmd\":\"setting_set\",\"id\":\"brightness\",\"value\":4,\"value\":6}",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        cJSON *request = cJSON_Parse(bad[i]);
        const char *id;
        int64_t value;
        assert(!settings_parse_set(request, &id, &value));
        cJSON_Delete(request);
    }
}

static void mode_read_modify_write(void) {
    fake_camera_t f;
    fake_camera_init_modes(&f);
    cJSON *result = settings_snapshot(&f.settings);
    type(result, "settings");
    cJSON *mode = control(result, "ai_mode");
    assert(mode && !control(result, "ai_tracking"));
    assert(strcmp(cJSON_GetObjectItem(mode, "kind")->valuestring, "enum") == 0);
    assert(strcmp(cJSON_GetObjectItem(mode, "group")->valuestring, "ai") == 0);
    assert(number(mode, "value") == 0);
    assert(cJSON_IsNull(cJSON_GetObjectItem(mode, "default")));
    assert(cJSON_IsNull(cJSON_GetObjectItem(mode, "error"))); // Stale flag 3 is not failure in Normal.
    assert(cJSON_IsFalse(cJSON_GetObjectItem(mode, "pending")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(mode, "writable")));
    const unsigned values[] = {0, 1, 4, 5, 6};
    cJSON *options = cJSON_GetObjectItem(mode, "options");
    assert(cJSON_GetArraySize(options) == 5);
    for (unsigned i = 0; i < 5; ++i)
        assert(number(cJSON_GetArrayItem(options, i), "value") == values[i]);
    assert(f.writes == 0 && f.mode_reads == 3); // INFO, LEN, full CUR only.
    cJSON_Delete(result);
    const int invalid[] = {-1, 2, 3, 7, 255};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        rejected(&f, "ai_mode", invalid[i]);
    rejected(&f, "ai_tracking", 1);

    for (unsigned i = 0; i < 5; ++i) {
        fake_camera_init_modes(&f);
        // Normal writes must also clear byte[1], while stale flags on reads are fine.
        f.mode_payload[0] = values[i] == 0 ? 1 : 0;
        uint8_t before[SETTINGS_MODE_LENGTH];
        memcpy(before, f.mode_payload, sizeof(before));
        result = settings_set(&f.settings, "ai_mode", values[i]);
        type(result, "settings");
        assert(f.writes == 1 && f.last_width == 61);
        assert(f.last_interface == 3 && f.last_unit == 9 && f.last_selector == 2);
        assert(f.last_data[0] == values[i]);
        assert(f.last_data[1] == (values[i] == 5 ? 3 : 0));
        assert(memcmp(f.last_data + 2, before + 2, 59) == 0);
        assert(f.mode_reads == 6); // Fresh INFO/LEN/CUR both before and after SET.
        mode = control(result, "ai_mode");
        assert(number(mode, "value") == values[i]);
        assert(cJSON_IsTrue(cJSON_GetObjectItem(mode, "pending")) == (values[i] == 4));
        cJSON_Delete(result);
    }
    fake_camera_init_modes(&f);
    f.mode_payload[0] = 1;
    rejected(&f, "ai_mode", 6); // Must pass through Normal first.
    result = settings_set(&f.settings, "ai_mode", 1); // Already selected: no restart.
    type(result, "settings");
    assert(f.writes == 0);
    cJSON_Delete(result);
}

static void mode_pending_and_authoritative_readback(void) {
    fake_camera_t f;
    fake_camera_init_modes(&f);
    f.transition_mode_write = true;
    cJSON *result = settings_set(&f.settings, "ai_mode", 1);
    type(result, "settings");
    cJSON *mode = control(result, "ai_mode");
    assert(number(mode, "value") == 255); // Never echo requested 1.
    assert(cJSON_IsTrue(cJSON_GetObjectItem(mode, "pending")));
    assert(cJSON_IsNull(cJSON_GetObjectItem(mode, "error"))); // A normal transition is not failure.
    assert(cJSON_IsFalse(cJSON_GetObjectItem(mode, "writable")));
    assert(strstr(cJSON_GetObjectItem(mode, "detail")->valuestring, "Changing mode"));
    cJSON_Delete(result);
    rejected(&f, "ai_mode", 0); // Camera ignores writes during transition.

    fake_camera_init_modes(&f);
    f.ignore_mode_write = true;
    result = settings_set(&f.settings, "ai_mode", 1);
    type(result, "settings");
    mode = control(result, "ai_mode");
    assert(number(mode, "value") == 0); // May still be old state before transition starts.
    assert(cJSON_IsFalse(cJSON_GetObjectItem(mode, "pending")));
    assert(f.writes == 1);
    cJSON_Delete(result);
    f.mode_payload[0] = 255;
    result = settings_snapshot(&f.settings);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(control(result, "ai_mode"), "pending")));
    cJSON_Delete(result);
    f.mode_payload[0] = 1;
    result = settings_snapshot(&f.settings);
    assert(number(control(result, "ai_mode"), "value") == 1);
    assert(f.writes == 1); // Polling only reads; no hidden retries.
    cJSON_Delete(result);

    for (unsigned flag = 0; flag <= 4; ++flag) {
        f.mode_payload[0] = 4;
        f.mode_payload[1] = flag;
        result = settings_snapshot(&f.settings);
        mode = control(result, "ai_mode");
        assert(cJSON_IsTrue(cJSON_GetObjectItem(mode, "pending")) == (flag < 2));
        if (flag == 3)
            assert(strstr(cJSON_GetObjectItem(mode, "error")->valuestring, "Whiteboard not detected"));
        else if (flag > 3)
            assert(strstr(cJSON_GetObjectItem(mode, "error")->valuestring, "unrecognized whiteboard"));
        else assert(cJSON_IsNull(cJSON_GetObjectItem(mode, "error")));
        cJSON_Delete(result);
    }
    f.ignore_mode_write = false;
    f.mode_payload[1] = 1;
    result = settings_set(&f.settings, "ai_mode", 0); // Cancel board detection.
    assert(number(control(result, "ai_mode"), "value") == 0);
    cJSON_Delete(result);
}

static void mode_failures_stay_local(void) {
    fake_camera_t f;
    fake_camera_init_modes(&f);
    fake_control_t *mode = fake_control(&f, 9, 2);
    const uint8_t failures[] = {0x86, 0x85, 0x81};
    for (unsigned i = 0; i < sizeof(failures); ++i) {
        mode->fail_request = failures[i]; // Includes short-transfer transport errors.
        rejected(&f, "ai_mode", 1);
        assert(!f.settings.disconnected);
        cJSON *result = settings_snapshot(&f.settings);
        type(result, "settings");
        assert(cJSON_IsNull(cJSON_GetObjectItem(control(result, "ai_mode"), "value")));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(control(result, "brightness"), "writable")));
        cJSON_Delete(result);
    }
    mode->fail_request = 0;
    const uint16_t bad_lengths[] = {0, 1, 52, 60, 62, 256};
    for (unsigned i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++i) {
        f.mode_length = bad_lengths[i];
        unsigned reads = f.mode_reads;
        rejected(&f, "ai_mode", 1);
        assert(f.mode_reads - reads == 2); // Do not GET_CUR with an unvalidated length.
    }
    f.mode_length = 61;
    const uint8_t bad_info[] = {0, 1, 2, 7, 0x0b, 0x13, 0x23, 0xff};
    for (unsigned i = 0; i < sizeof(bad_info); ++i) {
        mode->info = bad_info[i];
        rejected(&f, "ai_mode", 1);
    }
    mode->info = 3;
    f.mode_payload[0] = 2;
    rejected(&f, "ai_mode", 1);
    cJSON *result = settings_snapshot(&f.settings);
    assert(number(control(result, "ai_mode"), "value") == 2); // Diagnostic actual value.
    cJSON_Delete(result);
    f.mode_payload[0] = 0;
    mode->fail_request = 1;
    result = settings_set(&f.settings, "ai_mode", 1);
    type(result, "settings_error");
    assert(f.writes == 1 && !f.settings.disconnected);
    cJSON_Delete(result);
    mode->fail_request = 0;
    f.fail_readback = true;
    result = settings_set(&f.settings, "ai_mode", 1);
    type(result, "settings_error");
    cJSON_Delete(result);
    f.disconnected = true;
    result = settings_snapshot(&f.settings);
    type(result, "settings_error");
    assert(f.settings.disconnected);
    cJSON_Delete(result);
}

static void mode_descriptor_gates(void) {
    fake_camera_t f;
    const uint16_t identities[][2] = {{0x2e1a, 0x4c01}, {0x2e1a, 0x4c04}, {0x1234, 0x4c06}, {0, 0}};
    for (unsigned i = 0; i < sizeof(identities) / sizeof(identities[0]); ++i) {
        fake_camera_init_modes(&f);
        assert(settings_discover_for_device(&f.settings, f.descriptors, f.descriptor_length,
                                           identities[i][0], identities[i][1]) == 0);
        cJSON *result = settings_snapshot(&f.settings);
        assert(!control(result, "ai_mode") && !control(result, "ai_tracking") &&
               !control(result, "gesture_control") && !control(result, "gesture_palm"));
        assert(control(result, "brightness"));
        assert(f.mode_reads == 0);
        cJSON_Delete(result);
        rejected(&f, "ai_mode", 1);
    }
    // Unit ID, every GUID byte, pin count, bitmap size, and advertised selector.
    const unsigned offsets[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 23, 24};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        fake_camera_init_modes(&f);
        uint8_t *xu = f.descriptors + f.descriptor_length - 29;
        xu[offsets[i]] ^= offsets[i] == 24 ? 2 : 1;
        assert(settings_discover_for_device(&f.settings, f.descriptors, f.descriptor_length,
                                           0x2e1a, 0x4c06) == 0);
        cJSON *result = settings_snapshot(&f.settings);
        assert(!control(result, "ai_mode") && control(result, "brightness"));
        assert(f.mode_reads == 0);
        cJSON_Delete(result);
        rejected(&f, "ai_mode", 1);
    }
    fake_camera_init_modes(&f);
    memcpy(f.descriptors + f.descriptor_length, f.descriptors + f.descriptor_length - 29, 29);
    f.descriptor_length += 29;
    f.descriptors[2] = f.descriptor_length;
    assert(settings_discover_for_device(&f.settings, f.descriptors, f.descriptor_length, 0x2e1a, 0x4c06) == 0);
    rejected(&f, "ai_mode", 1); // Duplicate unit IDs are ambiguous.
    assert(f.mode_reads == 0);

    fake_camera_init_modes(&f);
    const uint8_t streaming_interface[] = {9, 4, 4, 0, 0, 0x0e, 2, 0, 0};
    size_t xu_offset = f.descriptor_length - 29;
    memmove(f.descriptors + xu_offset + 9, f.descriptors + xu_offset, 29);
    memcpy(f.descriptors + xu_offset, streaming_interface, sizeof(streaming_interface));
    f.descriptor_length += 9;
    f.descriptors[2] = f.descriptor_length;
    assert(settings_discover_for_device(&f.settings, f.descriptors, f.descriptor_length, 0x2e1a, 0x4c06) == 0);
    cJSON *result = settings_snapshot(&f.settings);
    assert(!control(result, "ai_mode") && control(result, "brightness"));
    assert(f.mode_reads == 0); // Matching GUID outside VideoControl is not a target.
    cJSON_Delete(result);
}

static void gesture_controls(void) {
    fake_camera_t f;
    fake_camera_init_modes(&f);
    cJSON *result = settings_snapshot(&f.settings);
    const char *ids[] = {"gesture_control", "gesture_palm", "gesture_zoom", "gesture_whiteboard"};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        cJSON *gesture = control(result, ids[i]);
        assert(gesture && strcmp(cJSON_GetObjectItem(gesture, "group")->valuestring, "gestures") == 0);
        assert(strcmp(cJSON_GetObjectItem(gesture, "kind")->valuestring, "boolean") == 0);
    }
    cJSON_Delete(result);
    rejected(&f, "gesture_palm", -1);
    rejected(&f, "gesture_palm", 2);

    uint8_t master_before[2];
    memcpy(master_before, f.gesture_payload, sizeof(master_before));
    result = settings_set(&f.settings, "gesture_control", 1);
    type(result, "settings");
    assert(f.last_selector == 0x1b && f.last_width == 2);
    assert(f.last_data[0] == (uint8_t)(master_before[0] | 0x10));
    assert(f.last_data[1] == master_before[1]);
    cJSON_Delete(result);

    uint8_t feature_before = f.gesture_flags;
    result = settings_set(&f.settings, "gesture_zoom", 1);
    type(result, "settings");
    assert(f.last_selector == 5 && f.last_width == 1);
    assert(f.last_data[0] == (uint8_t)(feature_before | 0x04));
    cJSON_Delete(result);

    // The user's primary case: master OFF preserves all other feature bits
    // and leaves the independent per-gesture register untouched.
    uint8_t flags_before = f.gesture_flags;
    result = settings_set(&f.settings, "gesture_control", 0);
    type(result, "settings");
    assert(f.last_selector == 0x1b && f.last_width == 2);
    assert(f.last_data[0] == (uint8_t)(master_before[0] & ~0x10));
    assert(f.last_data[1] == master_before[1] && f.gesture_flags == flags_before);
    cJSON_Delete(result);
    for (unsigned i = 1; i < 4; ++i) {
        unsigned mask = 1u << i;
        result = settings_set(&f.settings, ids[i], 1);
        type(result, "settings");
        assert(f.gesture_flags == (uint8_t)(flags_before | mask));
        cJSON_Delete(result);
        result = settings_set(&f.settings, ids[i], 0);
        type(result, "settings");
        flags_before &= (uint8_t)~mask;
        assert(f.gesture_flags == flags_before);
        assert(f.gesture_payload[0] == (uint8_t)(master_before[0] & ~0x10));
        assert(f.gesture_payload[1] == master_before[1]);
        cJSON_Delete(result);
    }

    fake_control(&f, 9, 5)->info = 1;
    rejected(&f, "gesture_palm", 1);
    fake_control(&f, 9, 5)->info = 3;
    f.gesture_length = 2;
    rejected(&f, "gesture_palm", 1);
    f.gesture_length = 1;
    fake_control(&f, 9, 5)->fail_request = 0x81;
    rejected(&f, "gesture_palm", 1);
    fake_control(&f, 9, 5)->fail_request = 0;
    // Simulate a camera accepting SET_CUR but leaving the requested bit unchanged.
    uint8_t held = f.gesture_flags;
    f.ignore_gesture_write = true;
    unsigned writes = f.writes;
    result = settings_set(&f.settings, "gesture_palm", 1);
    type(result, "settings_error");
    assert(f.writes == writes + 1);
    cJSON_Delete(result);
    assert(f.gesture_flags == held);
    f.ignore_gesture_write = false;

    f.gesture_master_length = 1;
    rejected(&f, "gesture_control", 0);
    f.gesture_master_length = 2;
    const uint8_t unsafe_info[] = {0, 1, 2, 7, 0x0b, 0x13, 0x23};
    for (unsigned i = 0; i < sizeof(unsafe_info); ++i) {
        fake_control(&f, 9, 0x1b)->info = unsafe_info[i];
        rejected(&f, "gesture_control", 0);
    }
    fake_control(&f, 9, 0x1b)->info = 3;
    fake_control(&f, 9, 0x1b)->fail_request = 0x86;
    rejected(&f, "gesture_control", 0);
    fake_control(&f, 9, 0x1b)->fail_request = 0;
    f.fail_readback = true;
    result = settings_set(&f.settings, "gesture_control", 1);
    type(result, "settings_error");
    cJSON_Delete(result);

    // Selector advertisement gates each setting independently.
    fake_camera_init_modes(&f);
    uint8_t *xu = f.descriptors + f.descriptor_length - 29;
    xu[27] &= (uint8_t)~0x04; // Selector 0x1b, bit 26.
    assert(settings_discover_for_device(&f.settings, f.descriptors, f.descriptor_length,
                                        0x2e1a, 0x4c06) == 0);
    result = settings_snapshot(&f.settings);
    assert(control(result, "ai_mode") && !control(result, "gesture_control"));
    assert(control(result, "gesture_palm"));
    cJSON_Delete(result);
    xu[24] &= (uint8_t)~0x10; // Selector 5, bit 4.
    assert(settings_discover_for_device(&f.settings, f.descriptors, f.descriptor_length,
                                        0x2e1a, 0x4c06) == 0);
    result = settings_snapshot(&f.settings);
    assert(!control(result, "gesture_palm") && !control(result, "gesture_zoom") &&
           !control(result, "gesture_whiteboard"));
    cJSON_Delete(result);
}

int main(void) {
    discovery_and_signed_values();
    auto_dependencies_and_exposure();
    failures_stay_local();
    malformed_descriptors_and_requests();
    mode_read_modify_write();
    mode_pending_and_authoritative_readback();
    mode_failures_stay_local();
    mode_descriptor_gates();
    gesture_controls();
    puts("settings tests passed (fake USB only)");
    return 0;
}
