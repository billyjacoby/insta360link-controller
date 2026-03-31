#include "wifi_manager.h"

WiFiManager wifiManager;

void WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
}

void WiFiManager::update() {
    if (_state == WiFiState::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            _state = WiFiState::CONNECTED;
            Serial.println("[WiFi] Connected!");
            Serial.print("[WiFi] IP: ");
            Serial.println(WiFi.localIP());
        } else if (millis() - _lastConnectAttempt > CONNECT_TIMEOUT_MS) {
            _state = WiFiState::DISCONNECTED;
            Serial.println("[WiFi] Connection timeout");
        }
    } else if (_state == WiFiState::CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            _state = WiFiState::DISCONNECTED;
            Serial.println("[WiFi] Disconnected");
        }
    } else if (_state == WiFiState::DISCONNECTED && _ssid.length() > 0) {
        // Auto-retry with stored credentials
        if (millis() - _lastRetryAttempt >= RETRY_INTERVAL_MS) {
            _lastRetryAttempt = millis();
            Serial.println("[WiFi] Retrying connection...");
            connect(_ssid, _password);
        }
    }
}

bool WiFiManager::connect(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        Serial.println("[WiFi] No SSID configured");
        return false;
    }

    _ssid = ssid;
    _password = password;

    Serial.print("[WiFi] Connecting to ");
    Serial.println(_ssid);

    WiFi.begin(_ssid.c_str(), _password.c_str());
    _state = WiFiState::CONNECTING;
    _lastConnectAttempt = millis();

    return true;
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    _state = WiFiState::DISCONNECTED;
}

String WiFiManager::getIP() const {
    if (_state == WiFiState::CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "";
}
