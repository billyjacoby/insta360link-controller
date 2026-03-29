#include "camera.h"
#include <libuvc/libuvc.h>
#include <stdio.h>
#include <string.h>

static uvc_context_t *ctx = NULL;
static uvc_device_t *dev = NULL;
static uvc_device_handle_t *devh = NULL;

int camera_init(void) {
    uvc_error_t res = uvc_init(&ctx, NULL);
    if (res < 0) {
        fprintf(stderr, "[camera] uvc_init failed: %s\n", uvc_strerror(res));
        return -1;
    }
    printf("[camera] libuvc initialized\n");
    return 0;
}

int camera_open(void) {
    if (!ctx) return -1;
    if (devh) return 0;  // Already open

    uvc_error_t res = uvc_find_device(ctx, &dev, INSTA360_VID, INSTA360_PID, NULL);
    if (res < 0) {
        return -1;  // Not found — caller can retry
    }

    res = uvc_open(dev, &devh);
    if (res < 0) {
        fprintf(stderr, "[camera] uvc_open failed: %s\n", uvc_strerror(res));
        uvc_unref_device(dev);
        dev = NULL;
        return -1;
    }

    printf("[camera] Insta360 Link opened\n");
    return 0;
}

void camera_close(void) {
    if (devh) {
        uvc_close(devh);
        devh = NULL;
    }
    if (dev) {
        uvc_unref_device(dev);
        dev = NULL;
    }
    printf("[camera] Camera closed\n");
}

void camera_destroy(void) {
    camera_close();
    if (ctx) {
        uvc_exit(ctx);
        ctx = NULL;
    }
}

bool camera_is_connected(void) {
    return devh != NULL;
}

int camera_pan_tilt(uint8_t pan_dir, uint8_t pan_speed,
                    uint8_t tilt_dir, uint8_t tilt_speed) {
    if (!devh) return -1;

    uint8_t data[4] = { pan_dir, pan_speed, tilt_dir, tilt_speed };

    uvc_error_t res = uvc_set_ctrl(devh, XU_UNIT_ID, SEL_PAN_TILT_RELATIVE,
                                    data, sizeof(data));
    if (res < 0) {
        fprintf(stderr, "[camera] pan_tilt failed: %s\n", uvc_strerror(res));
        camera_close();
        return -1;
    }
    return 0;
}

int camera_stop(void) {
    return camera_pan_tilt(DIR_STOP, 1, DIR_STOP, 1);
}

int camera_center(void) {
    if (!devh) return -1;

    uint8_t data[8] = {0};

    uvc_error_t res = uvc_set_ctrl(devh, XU_UNIT_ID, SEL_GIMBAL_CENTER,
                                    data, sizeof(data));
    if (res < 0) {
        fprintf(stderr, "[camera] center failed: %s\n", uvc_strerror(res));
        camera_close();
        return -1;
    }
    return 0;
}

int camera_zoom(uint16_t value) {
    if (!devh) return -1;
    if (value < ZOOM_MIN || value > ZOOM_MAX) return -1;

    // Zoom is a 2-byte little-endian value on the Camera Terminal unit
    uint8_t data[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };

    uvc_error_t res = uvc_set_ctrl(devh, CT_UNIT_ID, SEL_ZOOM_ABSOLUTE,
                                    data, sizeof(data));
    if (res < 0) {
        fprintf(stderr, "[camera] zoom failed: %s\n", uvc_strerror(res));
        camera_close();
        return -1;
    }
    return 0;
}

int camera_get_zoom(void) {
    if (!devh) return -1;

    uint8_t data[2] = {0};

    uvc_error_t res = uvc_get_ctrl(devh, CT_UNIT_ID, SEL_ZOOM_ABSOLUTE,
                                    data, sizeof(data), UVC_GET_CUR);
    if (res < 0) {
        fprintf(stderr, "[camera] get_zoom failed: %s\n", uvc_strerror(res));
        return -1;
    }

    return (int)(data[0] | (data[1] << 8));
}

camera_status_t camera_get_status(void) {
    camera_status_t status;
    status.connected = camera_is_connected();
    status.zoom = 0;

    if (status.connected) {
        int z = camera_get_zoom();
        if (z >= 0) status.zoom = (uint16_t)z;
    }

    return status;
}
