#include <Arduino.h>
#include "config.h"
#include "joystick.h"
#include "wifi_manager.h"
#include "websocket_client.h"
#include "serial_cli.h"
#include "status_led.h"
#include "captive_portal.h"

// Timing
unsigned long lastJoystickRead = 0;
bool wasWifiConnected = false;
bool portalMode = false;

void updateStatusLed();

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);  // Give serial time to initialize

    // Initialize status LED first for early feedback
    statusLed.begin(STATUS_LED_PIN);
    statusLed.setPattern(LedPattern::BLINK_SLOW);

    // Initialize modules
    joystick.begin(JOYSTICK_X_PIN, JOYSTICK_Y_PIN);
    joystick.setDeadzone(JOYSTICK_DEADZONE);

    wifiManager.begin();
    insta360Client.begin();
    serialCli.begin();

    // Apply saved joystick settings
    joystick.setInvertX(serialCli.getInvertX());
    joystick.setInvertY(serialCli.getInvertY());
    joystick.setCenterX(serialCli.getJoyCenterX());
    joystick.setCenterY(serialCli.getJoyCenterY());

    // Auto-connect if config exists, otherwise start portal
    if (serialCli.hasWifiConfig()) {
        Serial.println("[Main] Auto-connecting to WiFi...");
        wifiManager.connect(serialCli.getWifiSsid(), serialCli.getWifiPassword());
    } else {
        Serial.println("[Main] No WiFi configured, starting portal...");
        captivePortal.begin();
        portalMode = true;
    }
}

void loop() {
    // Update status LED
    statusLed.update();

    // Handle captive portal mode
    if (portalMode || captivePortal.isActive()) {
        captivePortal.update();
        serialCli.update();  // Still allow serial commands

        // Check if portal finished configuration
        if (captivePortal.isConfigured()) {
            serialCli.setWifiConfig(
                captivePortal.getWifiSsid(),
                captivePortal.getWifiPassword()
            );
            serialCli.setServerConfig(
                captivePortal.getWsHost(),
                captivePortal.getWsPort(),
                captivePortal.getWsToken()
            );
            Serial.println("[Main] Portal config saved, rebooting...");
            delay(500);
            ESP.restart();
        }

        // Check for portal request from serial
        if (serialCli.portalRequested()) {
            serialCli.clearPortalRequest();
            if (!captivePortal.isActive()) {
                wifiManager.disconnect();
                captivePortal.begin();
            }
            portalMode = true;
        }

        statusLed.setPattern(LedPattern::BLINK_DOUBLE);
        return;  // Skip normal loop when in portal mode
    }

    // Check for portal request from serial
    if (serialCli.portalRequested()) {
        serialCli.clearPortalRequest();
        wifiManager.disconnect();
        insta360Client.disconnect();
        captivePortal.begin();
        portalMode = true;
        return;
    }

    // Update all modules
    serialCli.update();
    wifiManager.update();
    insta360Client.update();

    // Update LED based on connection state
    updateStatusLed();

    // Auto-connect to WebSocket when WiFi connects
    if (wifiManager.isConnected() && !wasWifiConnected) {
        wasWifiConnected = true;
        if (serialCli.hasServerConfig()) {
            Serial.println("[Main] WiFi connected, connecting to camera...");
            insta360Client.connect(
                serialCli.getWsHost(),
                serialCli.getWsPort(),
                serialCli.getWsToken()
            );
        }
    } else if (!wifiManager.isConnected() && wasWifiConnected) {
        wasWifiConnected = false;
    }

    // Read joystick and send commands at fixed interval
    if (millis() - lastJoystickRead >= JOYSTICK_READ_INTERVAL_MS) {
        lastJoystickRead = millis();

        JoystickReading reading = joystick.read();
        PanTiltCommand cmd = joystick.toPanTilt(reading);

        // Send to camera if connected
        if (insta360Client.isConnected()) {
            insta360Client.sendPanTilt(cmd);
        }
    }
}

void updateStatusLed() {
    if (insta360Client.isConnected()) {
        statusLed.setPattern(LedPattern::SOLID);
    } else if (insta360Client.getState() == WSState::CONNECTING ||
               insta360Client.getState() == WSState::CONNECTED) {
        statusLed.setPattern(LedPattern::BLINK_FAST);
    } else if (wifiManager.getState() == WiFiState::CONNECTING) {
        statusLed.setPattern(LedPattern::BLINK_SLOW);
    } else if (wifiManager.isConnected()) {
        statusLed.setPattern(LedPattern::BLINK_FAST);
    } else {
        statusLed.setPattern(LedPattern::OFF);
    }
}
