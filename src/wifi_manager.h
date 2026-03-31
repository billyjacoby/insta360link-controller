#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

enum class WiFiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

class WiFiManager {
public:
    void begin();
    void update();

    bool connect(const String& ssid, const String& password);
    void disconnect();

    WiFiState getState() const { return _state; }
    bool isConnected() const { return _state == WiFiState::CONNECTED; }
    String getIP() const;

    void setSsid(const String& ssid) { _ssid = ssid; }
    void setPassword(const String& password) { _password = password; }
    String getSsid() const { return _ssid; }

private:
    String _ssid;
    String _password;
    WiFiState _state = WiFiState::DISCONNECTED;
    unsigned long _lastConnectAttempt = 0;
    unsigned long _lastRetryAttempt = 0;
    static const unsigned long CONNECT_TIMEOUT_MS = 10000;
    static const unsigned long RETRY_INTERVAL_MS = 15000;
};

extern WiFiManager wifiManager;

#endif
