#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>

// Direction encoding (matches UVC wire format)
#define DIR_POSITIVE 1
#define DIR_NEGATIVE 255
#define DIR_STOP     0

// Build a pan_tilt JSON command string.
String cmdPanTilt(uint8_t panDir, uint8_t panSpeed,
                  uint8_t tiltDir, uint8_t tiltSpeed);

// Build a zoom JSON command string.
String cmdZoom(uint16_t value);

// Build a stop JSON command string.
String cmdStop();

// Build a center JSON command string.
String cmdCenter();

// Build a status query JSON command string.
String cmdStatus();

#endif
