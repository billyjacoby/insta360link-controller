#include "zoom.h"
#include "config.h"

ZoomPot zoomPot;

void ZoomPot::begin(uint8_t pin) {
    _pin = pin;
    _gain = ZOOM_DERIVATIVE_GAIN;
    pinMode(_pin, INPUT);
}

bool ZoomPot::update(uint16_t currentCameraZoom) {
    int raw = analogRead(_pin);

    // Initialize on first valid status from camera
    if (!_initialized) {
        if (currentCameraZoom == 0) return false;  // No status yet
        _targetZoom = currentCameraZoom;
        _targetZoomF = (float)currentCameraZoom;
        _lastRaw = raw;
        _initialized = true;
        return false;
    }

    // Compute derivative (ADC change since last cycle)
    int delta = raw - _lastRaw;
    _lastRaw = raw;

    // Deadzone: ignore small changes (ADC noise / stationary pot)
    if (abs(delta) < ZOOM_ADC_DEADZONE) return false;

    // Map ADC delta to zoom delta
    float zoomDelta = (float)delta * _gain;
    _targetZoomF += zoomDelta;
    _targetZoomF = constrain(_targetZoomF, (float)ZOOM_MIN, (float)ZOOM_MAX);

    uint16_t newTarget = (uint16_t)(_targetZoomF + 0.5f);
    if (newTarget == _targetZoom) return false;

    _targetZoom = newTarget;
    return true;
}

void ZoomPot::resetToDefault() {
    _targetZoom = ZOOM_DEFAULT;
    _targetZoomF = (float)ZOOM_DEFAULT;
}

int ZoomPot::readRaw() {
    return analogRead(_pin);
}
