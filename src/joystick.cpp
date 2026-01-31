#include "joystick.h"
#include "config.h"

Joystick joystick;

void Joystick::begin(uint8_t xPin, uint8_t yPin) {
    _xPin = xPin;
    _yPin = yPin;

    // Configure ADC
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    pinMode(_xPin, INPUT);
    pinMode(_yPin, INPUT);
}

JoystickReading Joystick::read() {
    int rawX = analogRead(_xPin);
    int rawY = analogRead(_yPin);

    float x = normalizeAxis(rawX, _centerX);
    float y = normalizeAxis(rawY, _centerY);

    x = applyDeadzone(x);
    y = applyDeadzone(y);

    if (_invertX) x = -x;
    if (_invertY) y = -y;

    return { x, y };
}

float Joystick::normalizeAxis(int rawValue, int center) {
    // Convert 0-4095 to -1.0 to 1.0 using calibrated center
    float normalized;
    if (rawValue < center) {
        // Below center: map [0, center] to [-1, 0]
        normalized = (rawValue - center) / (float)center;
    } else {
        // Above center: map [center, 4095] to [0, 1]
        normalized = (rawValue - center) / (float)(JOYSTICK_MAX - center);
    }
    return constrain(normalized, -1.0f, 1.0f);
}

float Joystick::applyDeadzone(float value) {
    if (fabs(value) < _deadzone) {
        return 0.0f;
    }
    // Scale remaining range to 0-1
    float sign = value > 0 ? 1.0f : -1.0f;
    float magnitude = fabs(value);
    magnitude = (magnitude - _deadzone) / (1.0f - _deadzone);
    // Apply quadratic curve for finer control at low speeds
    magnitude = magnitude * magnitude;
    return sign * magnitude;
}

PanTiltCommand Joystick::toPanTilt(const JoystickReading& reading) {
    PanTiltCommand cmd;

    // Pan (X axis)
    if (reading.x > 0) {
        cmd.panDirection = 1;
        cmd.panSpeed = (uint8_t)(reading.x * PAN_MAX_SPEED);
    } else if (reading.x < 0) {
        cmd.panDirection = -1;
        cmd.panSpeed = (uint8_t)(-reading.x * PAN_MAX_SPEED);
    } else {
        cmd.panDirection = 0;
        cmd.panSpeed = 0;
    }

    // Tilt (Y axis)
    if (reading.y > 0) {
        cmd.tiltDirection = 1;
        cmd.tiltSpeed = (uint8_t)(reading.y * TILT_MAX_SPEED);
    } else if (reading.y < 0) {
        cmd.tiltDirection = -1;
        cmd.tiltSpeed = (uint8_t)(-reading.y * TILT_MAX_SPEED);
    } else {
        cmd.tiltDirection = 0;
        cmd.tiltSpeed = 0;
    }

    return cmd;
}
