#include <Arduino.h>
#include "config.h"
#include "joystick.h"
#include "wifi_manager.h"
#include "websocket_client.h"
#include "serial_cli.h"

// Timing
unsigned long lastJoystickRead = 0;
bool wasWifiConnected = false;

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);  // Give serial time to initialize

    // Initialize modules
    joystick.begin(JOYSTICK_X_PIN, JOYSTICK_Y_PIN);
    joystick.setDeadzone(JOYSTICK_DEADZONE);

    wifiManager.begin();
    insta360Client.begin();
    serialCli.begin();

    // Auto-connect if config exists
    if (serialCli.hasWifiConfig()) {
        Serial.println("[Main] Auto-connecting to WiFi...");
        wifiManager.connect(serialCli.getWifiSsid(), serialCli.getWifiPassword());
    }
}

void loop() {
    // Update all modules
    serialCli.update();
    wifiManager.update();
    insta360Client.update();

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

        // Debug output (uncomment to see joystick values)
        // if (reading.x != 0 || reading.y != 0) {
        //     Serial.printf("Joy: %.2f, %.2f -> Pan: %d/%d, Tilt: %d/%d\n",
        //         reading.x, reading.y,
        //         cmd.panDirection, cmd.panSpeed,
        //         cmd.tiltDirection, cmd.tiltSpeed);
        // }
    }
}
