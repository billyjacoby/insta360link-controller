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

    bool hasWifiConfig() const { return _wifiSsid.length() > 0; }
    bool hasServerConfig() const { return _wsHost.length() > 0 && _wsPort > 0 && _wsToken.length() > 0; }

private:
    Preferences _prefs;
    String _inputBuffer;

    String _wifiSsid;
    String _wifiPassword;
    String _wsHost;
    uint16_t _wsPort = 0;
    String _wsToken;

    void loadConfig();
    void saveConfig();
    void processCommand(const String& line);
    void printHelp();
    void printStatus();
};

extern SerialCLI serialCli;

#endif
