#ifndef ZOOM_H
#define ZOOM_H

#include <Arduino.h>

class ZoomPot {
public:
    void begin(uint8_t pin);

    // Read pot and map to zoom level.
    // Returns true if the zoom level changed since last call.
    bool update();

    uint16_t getTargetZoom() const { return _targetZoom; }

    // Reset zoom to default (called on home button press)
    void resetToDefault();

    // Disable/enable zoom input processing
    void setDisabled(bool disabled);
    bool isDisabled() const;

    // Raw ADC read for test/debug
    int readRaw();

private:
    uint8_t _pin = 0;
    uint16_t _targetZoom = 0;
    bool _disabled = false;
};

extern ZoomPot zoomPot;

#endif
