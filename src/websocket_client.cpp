#include "websocket_client.h"
#include "protocol.h"
#include "config.h"

Insta360Client insta360Client;
Insta360Client* Insta360Client::_instance = nullptr;

void Insta360Client::wsEventCallback(WStype_t type, uint8_t* payload, size_t length) {
    if (_instance) {
        _instance->onEvent(type, payload, length);
    }
}

void Insta360Client::begin() {
    _instance = this;
}

void Insta360Client::update() {
    _ws.loop();

    // Send heartbeat every second when authenticated
    if (_state == WSState::AUTHENTICATED) {
        if (millis() - _lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
            sendHeartbeat();
            _lastHeartbeat = millis();
        }
    }
}

bool Insta360Client::connect(const String& host, uint16_t port, const String& token) {
    if (host.length() == 0 || port == 0 || token.length() == 0) {
        Serial.println("[WS] Missing host, port, or token");
        return false;
    }

    _host = host;
    _port = port;
    _token = token;

    Serial.printf("[WS] Connecting to ws://%s:%d/?token=%s\n",
                  _host.c_str(), _port, _token.c_str());

    String path = "/?token=" + _token;
    _ws.begin(_host.c_str(), _port, path.c_str());
    _ws.onEvent(wsEventCallback);
    _ws.setReconnectInterval(WEBSOCKET_RECONNECT_INTERVAL_MS);

    _state = WSState::CONNECTING;
    return true;
}

void Insta360Client::disconnect() {
    _ws.disconnect();
    _state = WSState::DISCONNECTED;
    _deviceSerial = "";
}

void Insta360Client::onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected");
            _state = WSState::DISCONNECTED;
            _deviceSerial = "";
            break;

        case WStype_CONNECTED:
            Serial.println("[WS] Connected");
            _state = WSState::CONNECTED;
            sendControlRequest();
            break;

        case WStype_BIN:
            handleMessage(payload, length);
            break;

        case WStype_ERROR:
            Serial.println("[WS] Error");
            break;

        default:
            break;
    }
}

void Insta360Client::handleMessage(uint8_t* payload, size_t length) {
    // Simple protobuf parsing - look for device serial number
    // DeviceInfoNotification contains the serial we need

    // For now, do basic parsing to extract device serial
    // The serial is in field 1 of DeviceInfoNotification (curDeviceSerialNum)
    // DeviceInfoNotification is in field 10 of Response

    // Look for hasDeviceInfoNotify (field 1) = true
    // This is a simplified parser - production code should use proper protobuf

    size_t pos = 0;
    while (pos < length) {
        if (pos + 1 >= length) break;

        uint8_t tag = payload[pos];
        uint8_t fieldNum = tag >> 3;
        uint8_t wireType = tag & 0x07;
        pos++;

        if (wireType == 0) {  // Varint
            // Read and discard varint
            while (pos < length && (payload[pos] & 0x80)) pos++;
            pos++;

            // If this is hasDeviceInfoNotify (field 1) with value true
            if (fieldNum == 1) {
                _state = WSState::AUTHENTICATED;
                Serial.println("[WS] Authenticated - received device info");
            }
        } else if (wireType == 2) {  // Length-delimited
            // Read length
            uint32_t len = 0;
            uint8_t shift = 0;
            while (pos < length && (payload[pos] & 0x80)) {
                len |= (payload[pos] & 0x7F) << shift;
                shift += 7;
                pos++;
            }
            if (pos < length) {
                len |= payload[pos] << shift;
                pos++;
            }

            // If this is deviceInfoNotify (field 10), parse for serial
            if (fieldNum == 10 && pos + len <= length) {
                // Look for curDeviceSerialNum (field 1) inside
                size_t subPos = pos;
                size_t subEnd = pos + len;
                while (subPos < subEnd) {
                    uint8_t subTag = payload[subPos];
                    uint8_t subFieldNum = subTag >> 3;
                    uint8_t subWireType = subTag & 0x07;
                    subPos++;

                    if (subWireType == 2 && subFieldNum == 1) {
                        // String field - read length
                        uint32_t strLen = 0;
                        shift = 0;
                        while (subPos < subEnd && (payload[subPos] & 0x80)) {
                            strLen |= (payload[subPos] & 0x7F) << shift;
                            shift += 7;
                            subPos++;
                        }
                        if (subPos < subEnd) {
                            strLen |= payload[subPos] << shift;
                            subPos++;
                        }

                        if (subPos + strLen <= subEnd && strLen > 0) {
                            _deviceSerial = String((char*)(payload + subPos), strLen);
                            Serial.print("[WS] Device serial: ");
                            Serial.println(_deviceSerial);
                        }
                        break;
                    } else if (subWireType == 0) {
                        while (subPos < subEnd && (payload[subPos] & 0x80)) subPos++;
                        subPos++;
                    } else if (subWireType == 2) {
                        uint32_t skipLen = 0;
                        shift = 0;
                        while (subPos < subEnd && (payload[subPos] & 0x80)) {
                            skipLen |= (payload[subPos] & 0x7F) << shift;
                            shift += 7;
                            subPos++;
                        }
                        if (subPos < subEnd) {
                            skipLen |= payload[subPos] << shift;
                            subPos++;
                        }
                        subPos += skipLen;
                    }
                }
            }
            pos += len;
        }
    }
}

void Insta360Client::sendControlRequest() {
    auto msg = ProtocolEncoder::encodeControlRequest(_token);
    _ws.sendBIN(msg.data(), msg.size());
    Serial.println("[WS] Sent control request");
}

void Insta360Client::sendHeartbeat() {
    auto msg = ProtocolEncoder::encodeHeartbeatRequest();
    _ws.sendBIN(msg.data(), msg.size());
}

void Insta360Client::sendPanTilt(const PanTiltCommand& cmd) {
    // Only send if command changed
    if (cmd.panDirection == _lastCmd.panDirection &&
        cmd.panSpeed == _lastCmd.panSpeed &&
        cmd.tiltDirection == _lastCmd.tiltDirection &&
        cmd.tiltSpeed == _lastCmd.tiltSpeed) {
        return;
    }
    _lastCmd = cmd;

    if (_state != WSState::AUTHENTICATED || _deviceSerial.length() == 0) {
        return;
    }

    auto msg = ProtocolEncoder::encodePanTiltRequest(
        _deviceSerial,
        cmd.panDirection, cmd.panSpeed,
        cmd.tiltDirection, cmd.tiltSpeed
    );
    _ws.sendBIN(msg.data(), msg.size());
}

void Insta360Client::sendZoom(uint32_t zoomValue) {
    if (_state != WSState::AUTHENTICATED || _deviceSerial.length() == 0) {
        return;
    }

    auto msg = ProtocolEncoder::encodeZoomRequest(_deviceSerial, zoomValue);
    _ws.sendBIN(msg.data(), msg.size());
}
