#include "zoom.h"
#include "config.h"

ZoomPot zoomPot;

void ZoomPot::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT);
}

bool ZoomPot::update() {
    int raw = analogRead(_pin);
    uint16_t zoom = map(raw, 0, 4095, ZOOM_MIN, ZOOM_MAX);
    zoom = constrain(zoom, (long)ZOOM_MIN, (long)ZOOM_MAX);

    if (zoom == _targetZoom) return false;
    _targetZoom = zoom;
    return true;
}

void ZoomPot::resetToDefault() {
    _targetZoom = ZOOM_DEFAULT;
}

int ZoomPot::readRaw() {
    return analogRead(_pin);
}
