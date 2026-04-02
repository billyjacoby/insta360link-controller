#ifndef ZOOM_H
#define ZOOM_H

#include <Arduino.h>

class ZoomPot {
public:
    void begin(uint8_t pin);

    // Read pot derivative and update target zoom.
    // Returns true if the target changed since last call.
    // Pass currentCameraZoom from the WebSocket client status.
    bool update(uint16_t currentCameraZoom);

    uint16_t getTargetZoom() const { return _targetZoom; }

    // Reset zoom to default (called on home button press)
    void resetToDefault();

    // Raw ADC read for test/debug
    int readRaw();

private:
    uint8_t _pin = 0;

    // Runtime state
    float _gain = 0.15f;  // Set from ZOOM_DERIVATIVE_GAIN in begin()
    float _targetZoomF = 0.0f;
    uint16_t _targetZoom = 0;
    int _lastRaw = 0;
    bool _initialized = false;
};

extern ZoomPot zoomPot;

#endif
