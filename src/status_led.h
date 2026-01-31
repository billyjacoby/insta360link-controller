#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>

enum class LedPattern {
    OFF,
    SOLID,
    BLINK_SLOW,    // WiFi connecting
    BLINK_FAST,    // WebSocket connecting
    BLINK_DOUBLE   // Error state
};

class StatusLed {
public:
    void begin(uint8_t pin);
    void update();
    void setPattern(LedPattern pattern);

private:
    uint8_t _pin;
    LedPattern _pattern = LedPattern::OFF;
    bool _ledState = false;
    unsigned long _lastToggle = 0;
    uint8_t _blinkCount = 0;
};

extern StatusLed statusLed;

#endif
