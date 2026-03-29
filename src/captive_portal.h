#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

class CaptivePortal {
public:
    void begin();           // Start in AP mode (creates WiFi network)
    void beginOnNetwork();  // Start on existing WiFi network (STA mode)
    void update();
    void stop();

    bool isActive() const { return _active; }
    bool isConfigured() const { return _configured; }
    bool isNetworkMode() const { return _networkMode; }

    // Get configured values after portal closes
    String getWifiSsid() const { return _wifiSsid; }
    String getWifiPassword() const { return _wifiPassword; }
    String getWsHost() const { return _wsHost; }
    uint16_t getWsPort() const { return _wsPort; }

private:
    WebServer _server{80};
    DNSServer _dnsServer;
    bool _active = false;
    bool _configured = false;
    bool _networkMode = false;  // true = STA mode, false = AP mode
    unsigned long _startTime = 0;

    String _wifiSsid;
    String _wifiPassword;
    String _wsHost;
    uint16_t _wsPort = 0;

    void setupRoutes();
    void handleRoot();
    void handleSave();
    void handleScan();
    void handleNotFound();
    String generateHtml();
    String urlDecode(const String& str);
};

extern CaptivePortal captivePortal;

#endif
