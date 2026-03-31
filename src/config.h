#ifndef CONFIG_H
#define CONFIG_H

// Joystick pins (ADC1 only - ADC2 conflicts with WiFi)
// Pin assignments are set per-board via platformio.ini build_flags.
// Defaults here are for the original ESP32 dev board.
#ifndef JOYSTICK_X_PIN
#define JOYSTICK_X_PIN 34
#endif
#ifndef JOYSTICK_Y_PIN
#define JOYSTICK_Y_PIN 35
#endif
#ifndef JOYSTICK_BTN_PIN
#define JOYSTICK_BTN_PIN 32
#endif

// Status LED
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif

// Joystick configuration
#define JOYSTICK_DEADZONE 0.1f
#define JOYSTICK_CENTER 2048      // 12-bit ADC midpoint
#define JOYSTICK_MAX 4095

// Pan/Tilt speed limits (from Insta360 protocol)
#define PAN_MAX_SPEED 30
#define TILT_MAX_SPEED 20

// Timing
#define JOYSTICK_READ_INTERVAL_MS 50
#define BUTTON_DEBOUNCE_MS 50
#define WEBSOCKET_RECONNECT_INTERVAL_MS 5000
#define MDNS_RETRY_INTERVAL_MS 10000
#define LED_BLINK_INTERVAL_MS 500

// mDNS service discovery
#define MDNS_SERVICE_NAME "insta360ctrl"

// Serial CLI
#define SERIAL_BAUD 115200

// Captive portal (AP mode)
#define AP_SSID "Insta360-Controller"
#define AP_PASSWORD ""  // Open network for easy setup
#define PORTAL_TIMEOUT_MS 180000  // 3 minutes

// Development WiFi (uncomment and set for hardcoded WiFi)
// #define DEV_WIFI_SSID "YourNetworkName"
// #define DEV_WIFI_PASS "YourPassword"

// Config storage keys (NVS)
#define NVS_NAMESPACE "insta360"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_WS_HOST "ws_host"
#define NVS_KEY_WS_PORT "ws_port"
// NVS_KEY_WS_TOKEN removed — no auth needed with direct UVC control
#define NVS_KEY_INVERT_X "invert_x"
#define NVS_KEY_INVERT_Y "invert_y"
#define NVS_KEY_JOY_CENTER_X "joy_cx"
#define NVS_KEY_JOY_CENTER_Y "joy_cy"

#endif
