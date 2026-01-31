#include "serial_cli.h"
#include "config.h"
#include "wifi_manager.h"
#include "websocket_client.h"
#include "joystick.h"

SerialCLI serialCli;

void SerialCLI::begin() {
    loadConfig();
    Serial.println();
    Serial.println("=== Insta360 Link Joystick Controller ===");
    Serial.println("Type 'help' for commands");
    Serial.println();
    printStatus();
}

void SerialCLI::loadConfig() {
    _prefs.begin(NVS_NAMESPACE, true);  // Read-only
    _wifiSsid = _prefs.getString(NVS_KEY_WIFI_SSID, "");
    _wifiPassword = _prefs.getString(NVS_KEY_WIFI_PASS, "");
    _wsHost = _prefs.getString(NVS_KEY_WS_HOST, "");
    _wsPort = _prefs.getUShort(NVS_KEY_WS_PORT, 0);
    _wsToken = _prefs.getString(NVS_KEY_WS_TOKEN, "");
    _invertX = _prefs.getBool(NVS_KEY_INVERT_X, false);
    _invertY = _prefs.getBool(NVS_KEY_INVERT_Y, false);
    _joyCenterX = _prefs.getInt(NVS_KEY_JOY_CENTER_X, 2048);
    _joyCenterY = _prefs.getInt(NVS_KEY_JOY_CENTER_Y, 2048);
    _prefs.end();
}

void SerialCLI::saveConfig() {
    _prefs.begin(NVS_NAMESPACE, false);  // Read-write
    _prefs.putString(NVS_KEY_WIFI_SSID, _wifiSsid);
    _prefs.putString(NVS_KEY_WIFI_PASS, _wifiPassword);
    _prefs.putString(NVS_KEY_WS_HOST, _wsHost);
    _prefs.putUShort(NVS_KEY_WS_PORT, _wsPort);
    _prefs.putString(NVS_KEY_WS_TOKEN, _wsToken);
    _prefs.putBool(NVS_KEY_INVERT_X, _invertX);
    _prefs.putBool(NVS_KEY_INVERT_Y, _invertY);
    _prefs.putInt(NVS_KEY_JOY_CENTER_X, _joyCenterX);
    _prefs.putInt(NVS_KEY_JOY_CENTER_Y, _joyCenterY);
    _prefs.end();
    Serial.println("Config saved");
}

void SerialCLI::setWifiConfig(const String& ssid, const String& password) {
    _wifiSsid = ssid;
    _wifiPassword = password;
    saveConfig();
}

void SerialCLI::setServerConfig(const String& host, uint16_t port, const String& token) {
    _wsHost = host;
    _wsPort = port;
    _wsToken = token;
    saveConfig();
}

void SerialCLI::update() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (_inputBuffer.length() > 0) {
                processCommand(_inputBuffer);
                _inputBuffer = "";
            }
        } else if (c == '\b' || c == 127) {  // Backspace
            if (_inputBuffer.length() > 0) {
                _inputBuffer.remove(_inputBuffer.length() - 1);
            }
        } else {
            _inputBuffer += c;
        }
    }
}

void SerialCLI::parseQrUrl(const String& url) {
    // Parse URL like: insta360link://connect?port=XXXXX&token=XXXXX
    int portIdx = url.indexOf("port=");
    int tokenIdx = url.indexOf("token=");

    if (portIdx >= 0) {
        int portEnd = url.indexOf('&', portIdx);
        String portStr = (portEnd >= 0)
            ? url.substring(portIdx + 5, portEnd)
            : url.substring(portIdx + 5);
        _wsPort = portStr.toInt();
        Serial.print("Port: ");
        Serial.println(_wsPort);
    }

    if (tokenIdx >= 0) {
        int tokenEnd = url.indexOf('&', tokenIdx);
        _wsToken = (tokenEnd >= 0)
            ? url.substring(tokenIdx + 6, tokenEnd)
            : url.substring(tokenIdx + 6);
        Serial.println("Token: (set)");
    }

    if (portIdx >= 0 || tokenIdx >= 0) {
        saveConfig();
    } else {
        Serial.println("Could not parse URL. Expected format: insta360link://connect?port=XXXXX&token=XXXXX");
    }
}

void SerialCLI::calibrateJoystick() {
    Serial.println("Joystick calibration");
    Serial.println("Release the joystick and keep it centered...");
    delay(2000);

    // Read multiple samples and average
    long sumX = 0, sumY = 0;
    const int samples = 50;
    for (int i = 0; i < samples; i++) {
        sumX += analogRead(JOYSTICK_X_PIN);
        sumY += analogRead(JOYSTICK_Y_PIN);
        delay(20);
    }

    _joyCenterX = sumX / samples;
    _joyCenterY = sumY / samples;

    Serial.print("Center X: ");
    Serial.println(_joyCenterX);
    Serial.print("Center Y: ");
    Serial.println(_joyCenterY);

    saveConfig();
    Serial.println("Calibration complete. Restart to apply.");
}

void SerialCLI::processCommand(const String& line) {
    String cmd = line;
    cmd.trim();

    if (cmd == "help" || cmd == "?") {
        printHelp();
    }
    else if (cmd == "status") {
        printStatus();
    }
    else if (cmd.startsWith("wifi ")) {
        // wifi ssid password
        int spaceIdx = cmd.indexOf(' ', 5);
        if (spaceIdx > 0) {
            _wifiSsid = cmd.substring(5, spaceIdx);
            _wifiPassword = cmd.substring(spaceIdx + 1);
        } else {
            _wifiSsid = cmd.substring(5);
            _wifiPassword = "";
        }
        saveConfig();
        Serial.print("WiFi SSID set to: ");
        Serial.println(_wifiSsid);
    }
    else if (cmd.startsWith("host ")) {
        _wsHost = cmd.substring(5);
        saveConfig();
        Serial.print("WebSocket host set to: ");
        Serial.println(_wsHost);
    }
    else if (cmd.startsWith("port ")) {
        _wsPort = cmd.substring(5).toInt();
        saveConfig();
        Serial.print("WebSocket port set to: ");
        Serial.println(_wsPort);
    }
    else if (cmd.startsWith("token ")) {
        _wsToken = cmd.substring(6);
        saveConfig();
        Serial.print("WebSocket token set to: ");
        Serial.println(_wsToken);
    }
    else if (cmd.startsWith("qr ")) {
        // Parse QR code URL directly
        parseQrUrl(cmd.substring(3));
    }
    else if (cmd == "connect") {
        if (!hasWifiConfig()) {
            Serial.println("WiFi not configured. Use: wifi <ssid> <password>");
            return;
        }
        if (!hasServerConfig()) {
            Serial.println("Server not configured. Use: host, port, token (or qr <url>)");
            return;
        }
        wifiManager.connect(_wifiSsid, _wifiPassword);
    }
    else if (cmd == "disconnect") {
        insta360Client.disconnect();
        wifiManager.disconnect();
        Serial.println("Disconnected");
    }
    else if (cmd == "portal") {
        _portalRequested = true;
        Serial.println("Starting captive portal...");
    }
    else if (cmd == "webconfig") {
        if (!wifiManager.isConnected()) {
            Serial.println("WiFi not connected. Use 'connect' or 'devwifi' first.");
            return;
        }
        _webConfigRequested = true;
        Serial.println("Starting web config server...");
    }
#ifdef DEV_WIFI_SSID
    else if (cmd == "devwifi") {
        Serial.println("Connecting to dev WiFi: " DEV_WIFI_SSID);
        wifiManager.connect(DEV_WIFI_SSID, DEV_WIFI_PASS);
    }
#endif
    else if (cmd == "invertx") {
        _invertX = !_invertX;
        joystick.setInvertX(_invertX);
        saveConfig();
        Serial.print("Invert X: ");
        Serial.println(_invertX ? "ON" : "OFF");
    }
    else if (cmd == "inverty") {
        _invertY = !_invertY;
        joystick.setInvertY(_invertY);
        saveConfig();
        Serial.print("Invert Y: ");
        Serial.println(_invertY ? "ON" : "OFF");
    }
    else if (cmd == "calibrate") {
        calibrateJoystick();
    }
    else if (cmd == "joy") {
        // Show live joystick values for 5 seconds
        Serial.println("Joystick test (5 seconds):");
        unsigned long start = millis();
        while (millis() - start < 5000) {
            JoystickReading reading = joystick.read();
            Serial.printf("X: %6.2f  Y: %6.2f\r", reading.x, reading.y);
            delay(100);
        }
        Serial.println("\nDone");
    }
    else if (cmd == "clear") {
        _prefs.begin(NVS_NAMESPACE, false);
        _prefs.clear();
        _prefs.end();
        _wifiSsid = "";
        _wifiPassword = "";
        _wsHost = "";
        _wsPort = 0;
        _wsToken = "";
        _invertX = false;
        _invertY = false;
        _joyCenterX = 2048;
        _joyCenterY = 2048;
        Serial.println("Config cleared");
    }
    else if (cmd == "reboot") {
        Serial.println("Rebooting...");
        delay(100);
        ESP.restart();
    }
    else {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
        Serial.println("Type 'help' for commands");
    }
}

void SerialCLI::printHelp() {
    Serial.println();
    Serial.println("=== Configuration ===");
    Serial.println("  wifi <ssid> [password]  - Set WiFi credentials");
    Serial.println("  host <ip>               - Set Mac IP address");
    Serial.println("  port <port>             - Set WebSocket port");
    Serial.println("  token <token>           - Set auth token");
    Serial.println("  qr <url>                - Parse QR code URL (extracts port & token)");
    Serial.println("  portal                  - Start WiFi config portal (AP mode)");
    Serial.println("  webconfig               - Start web config on current network");
    Serial.println();
    Serial.println("=== Connection ===");
    Serial.println("  connect                 - Connect to WiFi and camera");
    Serial.println("  disconnect              - Disconnect from server");
    Serial.println("  status                  - Show current status");
#ifdef DEV_WIFI_SSID
    Serial.println("  devwifi                 - Connect to dev WiFi (hardcoded)");
#endif
    Serial.println();
    Serial.println("=== Joystick ===");
    Serial.println("  joy                     - Test joystick (5 sec)");
    Serial.println("  calibrate               - Calibrate joystick center");
    Serial.println("  invertx                 - Toggle X-axis inversion");
    Serial.println("  inverty                 - Toggle Y-axis inversion");
    Serial.println();
    Serial.println("=== System ===");
    Serial.println("  clear                   - Clear all saved config");
    Serial.println("  reboot                  - Restart ESP32");
    Serial.println("  help                    - Show this help");
    Serial.println();
}

void SerialCLI::printStatus() {
    Serial.println("--- Status ---");

    Serial.print("WiFi SSID: ");
    Serial.println(_wifiSsid.length() > 0 ? _wifiSsid : "(not set)");

    Serial.print("WiFi State: ");
    switch (wifiManager.getState()) {
        case WiFiState::DISCONNECTED: Serial.println("Disconnected"); break;
        case WiFiState::CONNECTING: Serial.println("Connecting..."); break;
        case WiFiState::CONNECTED:
            Serial.print("Connected (");
            Serial.print(wifiManager.getIP());
            Serial.println(")");
            break;
    }

    Serial.print("Server: ");
    if (_wsHost.length() > 0 && _wsPort > 0) {
        Serial.print(_wsHost);
        Serial.print(":");
        Serial.println(_wsPort);
    } else {
        Serial.println("(not set)");
    }

    Serial.print("Token: ");
    Serial.println(_wsToken.length() > 0 ? "(set)" : "(not set)");

    Serial.print("Camera State: ");
    switch (insta360Client.getState()) {
        case WSState::DISCONNECTED: Serial.println("Disconnected"); break;
        case WSState::CONNECTING: Serial.println("Connecting..."); break;
        case WSState::CONNECTED: Serial.println("Connected (authenticating)"); break;
        case WSState::AUTHENTICATED:
            Serial.print("Authenticated (");
            Serial.print(insta360Client.getDeviceSerial());
            Serial.println(")");
            break;
    }

    Serial.print("Joystick: ");
    Serial.print("InvertX=");
    Serial.print(_invertX ? "ON" : "OFF");
    Serial.print(" InvertY=");
    Serial.print(_invertY ? "ON" : "OFF");
    Serial.print(" Center=(");
    Serial.print(_joyCenterX);
    Serial.print(",");
    Serial.print(_joyCenterY);
    Serial.println(")");

    Serial.println("--------------");
}
