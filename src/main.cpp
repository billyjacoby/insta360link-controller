#include <Arduino.h>
#include <ESPmDNS.h>
#include "config.h"
#include "joystick.h"
#include "wifi_manager.h"
#include "websocket_client.h"
#include "serial_cli.h"
#include "status_led.h"
#include "captive_portal.h"
#include "zoom.h"

// Timing
unsigned long lastJoystickRead = 0;
unsigned long lastMdnsAttempt = 0;
bool wasWifiConnected = false;
bool portalMode = false;
bool mdnsStarted = false;

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
    joystick.beginButton(JOYSTICK_BTN_PIN);

    wifiManager.begin();
    linkCtlClient.begin();
    serialCli.begin();

    // Apply saved joystick settings
    joystick.setInvertX(serialCli.getInvertX());
    joystick.setInvertY(serialCli.getInvertY());
    joystick.setCenterX(serialCli.getJoyCenterX());
    joystick.setCenterY(serialCli.getJoyCenterY());

    // Initialize zoom potentiometer
    zoomPot.begin(ZOOM_POT_PIN);

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
            if (captivePortal.getWsPort() > 0) {
                serialCli.setServerConfig(
                    captivePortal.getWsHost(),
                    captivePortal.getWsPort()
                );
            }
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

    // Check for portal request from serial (AP mode)
    if (serialCli.portalRequested()) {
        serialCli.clearPortalRequest();
        wifiManager.disconnect();
        linkCtlClient.disconnect();
        captivePortal.begin();
        portalMode = true;
        return;
    }

    // Check for webconfig request from serial (network mode)
    if (serialCli.webConfigRequested()) {
        serialCli.clearWebConfigRequest();
        linkCtlClient.disconnect();
        captivePortal.beginOnNetwork();
        return;
    }

    // Handle network-mode web config (runs alongside normal operation)
    if (captivePortal.isActive() && captivePortal.isNetworkMode()) {
        captivePortal.update();
        // Check if config was saved
        if (captivePortal.isConfigured()) {
            serialCli.setWifiConfig(
                captivePortal.getWifiSsid(),
                captivePortal.getWifiPassword()
            );
            if (captivePortal.getWsPort() > 0) {
                serialCli.setServerConfig(
                    captivePortal.getWsHost(),
                    captivePortal.getWsPort()
                );
            }
            Serial.println("[Main] Config saved via web interface");
        }
    }

    // Update all modules
    serialCli.update();
    wifiManager.update();
    linkCtlClient.update();

    // Update LED based on connection state
    updateStatusLed();

    // When WiFi connects, start mDNS and connect to daemon
    if (wifiManager.isConnected() && !wasWifiConnected) {
        wasWifiConnected = true;

        // Initialize mDNS for service discovery
        if (!mdnsStarted) {
            if (MDNS.begin("insta360-controller")) {
                mdnsStarted = true;
                Serial.println("[Main] mDNS started");
            }
        }

        // Try manual config first, then mDNS discovery
        if (serialCli.hasServerConfig()) {
            Serial.println("[Main] WiFi connected, connecting to daemon (manual config)...");
            linkCtlClient.connect(serialCli.getWsHost(), serialCli.getWsPort());
        } else {
            Serial.println("[Main] WiFi connected, discovering daemon via mDNS...");
            linkCtlClient.connectViaMdns();
        }
    } else if (!wifiManager.isConnected() && wasWifiConnected) {
        wasWifiConnected = false;
        mdnsStarted = false;
    }

    // Retry mDNS discovery if not connected and no manual config
    if (wifiManager.isConnected() &&
        linkCtlClient.getState() == WSState::DISCONNECTED &&
        !serialCli.hasServerConfig() &&
        millis() - lastMdnsAttempt >= MDNS_RETRY_INTERVAL_MS) {
        lastMdnsAttempt = millis();
        linkCtlClient.connectViaMdns();
    }

    // Read joystick and send commands at fixed interval
    if (millis() - lastJoystickRead >= JOYSTICK_READ_INTERVAL_MS) {
        lastJoystickRead = millis();

        JoystickReading reading = joystick.read();
        PanTiltCommand cmd = joystick.toPanTilt(reading);

        // Send to camera if connected
        if (linkCtlClient.isConnected()) {
            linkCtlClient.sendPanTilt(cmd);

            // Zoom: update target from pot, send if changed
            if (zoomPot.update(linkCtlClient.getCameraZoom())) {
                linkCtlClient.sendZoom(zoomPot.getTargetZoom());
            }
        }

        // Check home button — center gimbal and reset zoom
        if (joystick.buttonPressed()) {
            if (linkCtlClient.isConnected()) {
                linkCtlClient.sendCenter();
                zoomPot.resetToDefault();
                linkCtlClient.sendZoom(zoomPot.getTargetZoom());
                Serial.println("[Main] Home button pressed — centered + zoom reset");
            }
        }
    }
}

void updateStatusLed() {
    if (linkCtlClient.isConnected() && linkCtlClient.isCameraConnected()) {
        statusLed.setPattern(LedPattern::SOLID);
    } else if (linkCtlClient.isConnected()) {
        statusLed.setPattern(LedPattern::BLINK_FAST);
    } else if (linkCtlClient.getState() == WSState::CONNECTING) {
        statusLed.setPattern(LedPattern::BLINK_FAST);
    } else if (wifiManager.getState() == WiFiState::CONNECTING) {
        statusLed.setPattern(LedPattern::BLINK_SLOW);
    } else if (wifiManager.isConnected()) {
        statusLed.setPattern(LedPattern::BLINK_SLOW);
    } else {
        statusLed.setPattern(LedPattern::OFF);
    }
}
