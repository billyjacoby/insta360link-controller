#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#include <Arduino.h>
#include <Preferences.h>

class SerialCLI {
public:
    void begin();
    void update();

    // Config getters
    String getWifiSsid() const { return _wifiSsid; }
    String getWifiPassword() const { return _wifiPassword; }
    String getWsHost() const { return _wsHost; }
    uint16_t getWsPort() const { return _wsPort; }
    String getWsToken() const { return _wsToken; }
    bool getInvertX() const { return _invertX; }
    bool getInvertY() const { return _invertY; }
    int getJoyCenterX() const { return _joyCenterX; }
    int getJoyCenterY() const { return _joyCenterY; }

    bool hasWifiConfig() const { return _wifiSsid.length() > 0; }
    bool hasServerConfig() const { return _wsHost.length() > 0 && _wsPort > 0 && _wsToken.length() > 0; }

    // Config setters (used by captive portal)
    void setWifiConfig(const String& ssid, const String& password);
    void setServerConfig(const String& host, uint16_t port, const String& token);

    // Request portal start
    bool portalRequested() const { return _portalRequested; }
    void clearPortalRequest() { _portalRequested = false; }

    // Request web config (on network)
    bool webConfigRequested() const { return _webConfigRequested; }
    void clearWebConfigRequest() { _webConfigRequested = false; }

private:
    Preferences _prefs;
    String _inputBuffer;
    bool _portalRequested = false;
    bool _webConfigRequested = false;

    String _wifiSsid;
    String _wifiPassword;
    String _wsHost;
    uint16_t _wsPort = 0;
    String _wsToken;
    bool _invertX = false;
    bool _invertY = false;
    int _joyCenterX = 2048;
    int _joyCenterY = 2048;

    void loadConfig();
    void saveConfig();
    void processCommand(const String& line);
    void printHelp();
    void printStatus();
    void parseQrUrl(const String& url);
    void calibrateJoystick();
};

extern SerialCLI serialCli;

#endif
