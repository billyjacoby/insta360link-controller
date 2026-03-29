#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include <stdint.h>

// Insta360 Link USB identifiers
#define INSTA360_VID 0x2e1a
#define INSTA360_PID 0x4c01

// UVC Extension Unit for Insta360 vendor controls
#define XU_UNIT_ID 0x09

// UVC selectors
#define SEL_PAN_TILT_RELATIVE  0x16  // 22
#define SEL_GIMBAL_CENTER      0x1A  // 26
#define SEL_ZOOM_ABSOLUTE      0x0B  // 11

// Camera Terminal unit for standard UVC controls (zoom)
#define CT_UNIT_ID 0x01

// Speed limits
#define PAN_MAX_SPEED  30
#define TILT_MAX_SPEED 20
#define ZOOM_MIN       100
#define ZOOM_MAX       400

// Direction encoding (matches UVC wire format)
#define DIR_POSITIVE 1
#define DIR_NEGATIVE 255
#define DIR_STOP     0

typedef struct {
    bool connected;
    uint16_t zoom;  // Current zoom level (100-400)
} camera_status_t;

// Initialize the camera subsystem. Returns 0 on success.
int camera_init(void);

// Attempt to find the Insta360 Link via IOKit. Returns 0 on success.
int camera_open(void);

// Release the IOKit device handle.
void camera_close(void);

// Shut down the camera subsystem.
void camera_destroy(void);

// Returns true if the camera handle is currently open.
bool camera_is_connected(void);

// Send a relative pan/tilt command.
// pan_dir/tilt_dir: DIR_POSITIVE, DIR_NEGATIVE, or DIR_STOP
// pan_speed: 0-30, tilt_speed: 0-20
int camera_pan_tilt(uint8_t pan_dir, uint8_t pan_speed,
                    uint8_t tilt_dir, uint8_t tilt_speed);

// Stop all gimbal movement.
int camera_stop(void);

// Center the gimbal.
int camera_center(void);

// Set absolute zoom level (100-400).
int camera_zoom(uint16_t value);

// Get current zoom level via GET_CUR. Returns zoom value, or -1 on error.
int camera_get_zoom(void);

// Get overall camera status.
camera_status_t camera_get_status(void);

#endif
