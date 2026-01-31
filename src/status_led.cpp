#include "status_led.h"
#include "config.h"

StatusLed statusLed;

void StatusLed::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
}

void StatusLed::setPattern(LedPattern pattern) {
    if (_pattern != pattern) {
        _pattern = pattern;
        _blinkCount = 0;
        _lastToggle = millis();

        // Immediate state for solid patterns
        if (pattern == LedPattern::OFF) {
            digitalWrite(_pin, LOW);
            _ledState = false;
        } else if (pattern == LedPattern::SOLID) {
            digitalWrite(_pin, HIGH);
            _ledState = true;
        }
    }
}

void StatusLed::update() {
    unsigned long now = millis();
    unsigned long interval;

    switch (_pattern) {
        case LedPattern::OFF:
        case LedPattern::SOLID:
            // Static states, no update needed
            return;

        case LedPattern::BLINK_SLOW:
            interval = LED_BLINK_INTERVAL_MS;
            break;

        case LedPattern::BLINK_FAST:
            interval = LED_BLINK_INTERVAL_MS / 4;
            break;

        case LedPattern::BLINK_DOUBLE:
            // Double blink pattern: on-off-on-off-pause
            if (_blinkCount < 4) {
                interval = LED_BLINK_INTERVAL_MS / 4;
            } else {
                interval = LED_BLINK_INTERVAL_MS;
            }
            break;

        default:
            return;
    }

    if (now - _lastToggle >= interval) {
        _lastToggle = now;
        _ledState = !_ledState;
        digitalWrite(_pin, _ledState ? HIGH : LOW);

        if (_pattern == LedPattern::BLINK_DOUBLE) {
            _blinkCount++;
            if (_blinkCount >= 6) {
                _blinkCount = 0;
            }
        }
    }
}
