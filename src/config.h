#ifndef CONFIG_H
#define CONFIG_H

// Joystick pins (ADC1 only - ADC2 conflicts with WiFi)
#define JOYSTICK_X_PIN 34
#define JOYSTICK_Y_PIN 35

// Status LED (built-in on most ESP32 dev boards)
#define STATUS_LED_PIN 2

// Joystick configuration
#define JOYSTICK_DEADZONE 0.1f
#define JOYSTICK_CENTER 2048      // 12-bit ADC midpoint
#define JOYSTICK_MAX 4095

// Pan/Tilt speed limits (from Insta360 protocol)
#define PAN_MAX_SPEED 30
#define TILT_MAX_SPEED 20

// Timing
#define JOYSTICK_READ_INTERVAL_MS 50
#define HEARTBEAT_INTERVAL_MS 1000
#define WEBSOCKET_RECONNECT_INTERVAL_MS 5000
#define LED_BLINK_INTERVAL_MS 500

// Serial CLI
#define SERIAL_BAUD 115200

// Captive portal
#define AP_SSID "Insta360-Controller"
#define AP_PASSWORD ""  // Open network for easy setup
#define PORTAL_TIMEOUT_MS 180000  // 3 minutes

// Config storage keys (NVS)
#define NVS_NAMESPACE "insta360"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_WS_HOST "ws_host"
#define NVS_KEY_WS_PORT "ws_port"
#define NVS_KEY_WS_TOKEN "ws_token"
#define NVS_KEY_INVERT_X "invert_x"
#define NVS_KEY_INVERT_Y "invert_y"
#define NVS_KEY_JOY_CENTER_X "joy_cx"
#define NVS_KEY_JOY_CENTER_Y "joy_cy"

#endif
