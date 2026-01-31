#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include "joystick.h"

enum class WSState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AUTHENTICATED
};

class Insta360Client {
public:
    void begin();
    void update();

    bool connect(const String& host, uint16_t port, const String& token);
    void disconnect();

    void sendPanTilt(const PanTiltCommand& cmd);
    void sendZoom(uint32_t zoomValue);

    WSState getState() const { return _state; }
    bool isConnected() const { return _state == WSState::AUTHENTICATED; }

    void setHost(const String& host) { _host = host; }
    void setPort(uint16_t port) { _port = port; }
    void setToken(const String& token) { _token = token; }
    String getHost() const { return _host; }
    uint16_t getPort() const { return _port; }

    String getDeviceSerial() const { return _deviceSerial; }

private:
    WebSocketsClient _ws;
    String _host;
    uint16_t _port = 0;
    String _token;
    String _deviceSerial;
    WSState _state = WSState::DISCONNECTED;

    unsigned long _lastHeartbeat = 0;
    unsigned long _lastReconnectAttempt = 0;

    PanTiltCommand _lastCmd = {0, 0, 0, 0};

    void onEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleMessage(uint8_t* payload, size_t length);
    void sendHeartbeat();
    void sendControlRequest();

    static void wsEventCallback(WStype_t type, uint8_t* payload, size_t length);
    static Insta360Client* _instance;
};

extern Insta360Client insta360Client;

#endif
