#include "serial_cli.h"
#include "config.h"
#include "wifi_manager.h"
#include "websocket_client.h"

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
    _prefs.end();
}

void SerialCLI::saveConfig() {
    _prefs.begin(NVS_NAMESPACE, false);  // Read-write
    _prefs.putString(NVS_KEY_WIFI_SSID, _wifiSsid);
    _prefs.putString(NVS_KEY_WIFI_PASS, _wifiPassword);
    _prefs.putString(NVS_KEY_WS_HOST, _wsHost);
    _prefs.putUShort(NVS_KEY_WS_PORT, _wsPort);
    _prefs.putString(NVS_KEY_WS_TOKEN, _wsToken);
    _prefs.end();
    Serial.println("Config saved");
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
    else if (cmd == "connect") {
        if (!hasWifiConfig()) {
            Serial.println("WiFi not configured. Use: wifi <ssid> <password>");
            return;
        }
        if (!hasServerConfig()) {
            Serial.println("Server not configured. Use: host, port, token commands");
            return;
        }
        wifiManager.connect(_wifiSsid, _wifiPassword);
    }
    else if (cmd == "disconnect") {
        insta360Client.disconnect();
        wifiManager.disconnect();
        Serial.println("Disconnected");
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
    Serial.println("Commands:");
    Serial.println("  wifi <ssid> [password]  - Set WiFi credentials");
    Serial.println("  host <ip>               - Set WebSocket server IP");
    Serial.println("  port <port>             - Set WebSocket server port");
    Serial.println("  token <token>           - Set WebSocket auth token");
    Serial.println("  connect                 - Connect to WiFi and server");
    Serial.println("  disconnect              - Disconnect from server");
    Serial.println("  status                  - Show current status");
    Serial.println("  clear                   - Clear saved config");
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

    Serial.println("--------------");
}
