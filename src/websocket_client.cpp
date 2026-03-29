#include "websocket_client.h"
#include "commands.h"
#include "config.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>

LinkCtlClient linkCtlClient;
LinkCtlClient* LinkCtlClient::_instance = nullptr;

void LinkCtlClient::wsEventCallback(WStype_t type, uint8_t* payload, size_t length) {
    if (_instance) {
        _instance->onEvent(type, payload, length);
    }
}

void LinkCtlClient::begin() {
    _instance = this;
}

void LinkCtlClient::update() {
    _ws.loop();
}

bool LinkCtlClient::connect(const String& host, uint16_t port) {
    if (host.length() == 0 || port == 0) {
        Serial.println("[WS] Missing host or port");
        return false;
    }

    _host = host;
    _port = port;

    Serial.printf("[WS] Connecting to ws://%s:%d\n", _host.c_str(), _port);

    _ws.begin(_host.c_str(), _port, "/");
    _ws.onEvent(wsEventCallback);
    _ws.setReconnectInterval(WEBSOCKET_RECONNECT_INTERVAL_MS);

    _state = WSState::CONNECTING;
    return true;
}

bool LinkCtlClient::connectViaMdns() {
    Serial.println("[WS] Searching for daemon via mDNS...");

    int n = MDNS.queryService(MDNS_SERVICE_NAME, "tcp");
    if (n == 0) {
        Serial.println("[WS] No daemon found via mDNS");
        return false;
    }

    String host = MDNS.IP(0).toString();
    uint16_t port = MDNS.port(0);

    Serial.printf("[WS] Found daemon at %s:%d via mDNS\n", host.c_str(), port);
    return connect(host, port);
}

void LinkCtlClient::disconnect() {
    _ws.disconnect();
    _state = WSState::DISCONNECTED;
    _cameraConnected = false;
    _cameraZoom = 0;
}

void LinkCtlClient::onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected");
            _state = WSState::DISCONNECTED;
            _cameraConnected = false;
            break;

        case WStype_CONNECTED:
            Serial.println("[WS] Connected");
            _state = WSState::CONNECTED;
            break;

        case WStype_TEXT:
            handleMessage((const char*)payload, length);
            break;

        case WStype_ERROR:
            Serial.println("[WS] Error");
            break;

        default:
            break;
    }
}

void LinkCtlClient::handleMessage(const char* json, size_t length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, length);
    if (err) {
        Serial.printf("[WS] JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "status") == 0) {
        _cameraConnected = doc["camera_connected"] | false;
        _cameraZoom = doc["zoom"] | 0;
        Serial.printf("[WS] Status: camera=%s zoom=%d\n",
                      _cameraConnected ? "connected" : "disconnected",
                      _cameraZoom);
    } else if (strcmp(type, "error") == 0) {
        const char* msg = doc["message"] | "unknown error";
        Serial.printf("[WS] Error: %s\n", msg);
    }
}

void LinkCtlClient::sendText(const String& msg) {
    if (_state != WSState::CONNECTED) return;
    _ws.sendTXT(msg.c_str(), msg.length());
}

void LinkCtlClient::sendPanTilt(const PanTiltCommand& cmd) {
    // Only send if command changed
    if (cmd.panDirection == _lastCmd.panDirection &&
        cmd.panSpeed == _lastCmd.panSpeed &&
        cmd.tiltDirection == _lastCmd.tiltDirection &&
        cmd.tiltSpeed == _lastCmd.tiltSpeed) {
        return;
    }
    _lastCmd = cmd;

    if (_state != WSState::CONNECTED) return;

    sendText(cmdPanTilt(cmd.panDirection, cmd.panSpeed,
                        cmd.tiltDirection, cmd.tiltSpeed));
}

void LinkCtlClient::sendZoom(uint16_t zoomValue) {
    if (_state != WSState::CONNECTED) return;
    sendText(cmdZoom(zoomValue));
}

void LinkCtlClient::sendStop() {
    if (_state != WSState::CONNECTED) return;
    sendText(cmdStop());
}

void LinkCtlClient::sendCenter() {
    if (_state != WSState::CONNECTED) return;
    sendText(cmdCenter());
}

void LinkCtlClient::requestStatus() {
    if (_state != WSState::CONNECTED) return;
    sendText(cmdStatus());
}
