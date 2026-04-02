#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include "joystick.h"

enum class WSState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED      // Ready to send commands (no auth step needed)
};

class LinkCtlClient {
public:
    void begin();
    void update();

    // Connect using explicit host/port (manual override).
    bool connect(const String& host, uint16_t port);

    // Discover daemon via mDNS and connect.
    bool connectViaMdns();

    void disconnect();

    void sendPanTilt(const PanTiltCommand& cmd);
    void sendZoom(uint16_t zoomValue);
    void sendStop();
    void sendCenter();
    void requestStatus();

    WSState getState() const { return _state; }
    bool isConnected() const { return _state == WSState::CONNECTED; }
    bool isCameraConnected() const { return _cameraConnected; }

    void setHost(const String& host) { _host = host; }
    void setPort(uint16_t port) { _port = port; }
    String getHost() const { return _host; }
    uint16_t getPort() const { return _port; }

private:
    WebSocketsClient _ws;
    String _host;
    uint16_t _port = 0;
    WSState _state = WSState::DISCONNECTED;

    bool _cameraConnected = false;
    uint16_t _cameraZoom = 0;

    PanTiltCommand _lastCmd = {0, 0, 0, 0};

    void onEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleMessage(const char* json, size_t length);
    void sendText(const String& msg);

    static void wsEventCallback(WStype_t type, uint8_t* payload, size_t length);
    static LinkCtlClient* _instance;
};

extern LinkCtlClient linkCtlClient;

#endif
