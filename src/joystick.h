#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>

struct JoystickReading {
    float x;  // -1.0 to 1.0
    float y;  // -1.0 to 1.0
};

struct PanTiltCommand {
    int8_t panDirection;   // 1=right, -1=left, 0=stop
    uint8_t panSpeed;      // 0-30
    int8_t tiltDirection;  // 1=up, -1=down, 0=stop
    uint8_t tiltSpeed;     // 0-20
};

class Joystick {
public:
    void begin(uint8_t xPin, uint8_t yPin);
    JoystickReading read();
    PanTiltCommand toPanTilt(const JoystickReading& reading);
    void beginButton(uint8_t btnPin);
    bool buttonPressed();
    bool calibrate(uint16_t numSamples = 32, uint16_t sampleIntervalMs = 10);
    int getCenterX() const { return _centerX; }
    int getCenterY() const { return _centerY; }

    void setDeadzone(float deadzone) { _deadzone = deadzone; }
    void setInvertX(bool invert) { _invertX = invert; }
    void setInvertY(bool invert) { _invertY = invert; }
    void setCenterX(int center) { _centerX = center; }
    void setCenterY(int center) { _centerY = center; }

private:
    uint8_t _xPin;
    uint8_t _yPin;
    float _deadzone = 0.1f;
    bool _invertX = false;
    bool _invertY = false;
    int _centerX = 2048;
    int _centerY = 2048;

    uint8_t _btnPin = 0;
    bool _btnEnabled = false;
    bool _lastBtnState = HIGH;
    unsigned long _lastBtnChangeMs = 0;

    float normalizeAxis(int rawValue, int center);
    float applyDeadzone(float value);
};

extern Joystick joystick;

#endif
