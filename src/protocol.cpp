#include "protocol.h"

// Wire types
#define WIRE_VARINT 0
#define WIRE_LENGTH_DELIMITED 2

void ProtocolEncoder::writeVarint(std::vector<uint8_t>& buf, uint64_t value) {
    while (value > 0x7F) {
        buf.push_back((value & 0x7F) | 0x80);
        value >>= 7;
    }
    buf.push_back(value & 0x7F);
}

void ProtocolEncoder::writeTag(std::vector<uint8_t>& buf, uint32_t fieldNum, uint8_t wireType) {
    writeVarint(buf, (fieldNum << 3) | wireType);
}

void ProtocolEncoder::writeString(std::vector<uint8_t>& buf, uint32_t fieldNum, const String& value) {
    writeTag(buf, fieldNum, WIRE_LENGTH_DELIMITED);
    writeVarint(buf, value.length());
    for (size_t i = 0; i < value.length(); i++) {
        buf.push_back(value[i]);
    }
}

void ProtocolEncoder::writeInt64(std::vector<uint8_t>& buf, uint32_t fieldNum, int64_t value) {
    writeTag(buf, fieldNum, WIRE_VARINT);
    // Encode as unsigned (zigzag not used in this protocol)
    writeVarint(buf, (uint64_t)value);
}

void ProtocolEncoder::writeUint32(std::vector<uint8_t>& buf, uint32_t fieldNum, uint32_t value) {
    writeTag(buf, fieldNum, WIRE_VARINT);
    writeVarint(buf, value);
}

void ProtocolEncoder::writeBool(std::vector<uint8_t>& buf, uint32_t fieldNum, bool value) {
    writeTag(buf, fieldNum, WIRE_VARINT);
    buf.push_back(value ? 1 : 0);
}

void ProtocolEncoder::writeSubmessage(std::vector<uint8_t>& buf, uint32_t fieldNum, const std::vector<uint8_t>& submsg) {
    writeTag(buf, fieldNum, WIRE_LENGTH_DELIMITED);
    writeVarint(buf, submsg.size());
    buf.insert(buf.end(), submsg.begin(), submsg.end());
}

void ProtocolEncoder::writeRepeatedInt32(std::vector<uint8_t>& buf, uint32_t fieldNum, const std::vector<int32_t>& values) {
    // Non-packed repeated field - each value gets its own tag
    for (int32_t val : values) {
        writeTag(buf, fieldNum, WIRE_VARINT);
        writeVarint(buf, (uint64_t)(uint32_t)val);
    }
}

std::vector<uint8_t> ProtocolEncoder::encodeControlRequest(const String& token) {
    // ControlRequest message (field 13 in Request)
    // message ControlRequest { string token = 1; }
    std::vector<uint8_t> controlRequest;
    writeString(controlRequest, 1, token);

    // Request wrapper
    std::vector<uint8_t> request;
    writeBool(request, 4, true);  // hasControlRequest = true
    writeSubmessage(request, 13, controlRequest);  // controlRequest

    return request;
}

std::vector<uint8_t> ProtocolEncoder::encodeHeartbeatRequest() {
    // HeartbeatRequest message (field 14 in Request) - empty message
    std::vector<uint8_t> heartbeatRequest;

    // Request wrapper
    std::vector<uint8_t> request;
    writeBool(request, 5, true);  // hasHeartbeatRequest = true
    writeSubmessage(request, 14, heartbeatRequest);  // heartbeatRequest

    return request;
}

std::vector<uint8_t> ProtocolEncoder::encodePanTiltRequest(
    const String& serialNum,
    int8_t panDir, uint8_t panSpeed,
    int8_t tiltDir, uint8_t tiltSpeed
) {
    // UVCExtendRequest message (field 11 in Request)
    // message UVCExtendRequest {
    //   string curDeviceSerialNum = 1;
    //   ParamType paramType = 2;
    //   ControlSelector selector = 3;
    //   repeated int32 data = 4;
    //   int32 presetPosIndex = 5;
    // }

    // Convert direction to protocol format: 1=positive, 255=negative, 0=stop
    uint8_t panDirByte = (panDir > 0) ? 1 : (panDir < 0) ? 255 : 0;
    uint8_t tiltDirByte = (tiltDir > 0) ? 1 : (tiltDir < 0) ? 255 : 0;

    std::vector<int32_t> data = {
        (int32_t)panDirByte,
        (int32_t)panSpeed,
        (int32_t)tiltDirByte,
        (int32_t)tiltSpeed
    };

    std::vector<uint8_t> uvcExtendRequest;
    writeString(uvcExtendRequest, 1, serialNum);
    writeUint32(uvcExtendRequest, 2, PARAM_PAN_TILT_RELATIVE);
    writeUint32(uvcExtendRequest, 3, XU_PANTILT_RELATIVE_CONTROL);
    writeRepeatedInt32(uvcExtendRequest, 4, data);
    writeInt64(uvcExtendRequest, 5, -1);  // presetPosIndex = -1

    // Request wrapper
    std::vector<uint8_t> request;
    writeBool(request, 2, true);  // hasUvcExtendRequest = true
    writeSubmessage(request, 11, uvcExtendRequest);  // uvcExtendRequest

    return request;
}

std::vector<uint8_t> ProtocolEncoder::encodeZoomRequest(
    const String& serialNum,
    uint32_t zoomValue
) {
    // UVCRequest message (field 10 in Request)
    // message UVCRequest {
    //   string curDeviceSerialNum = 1;
    //   ParamType paramType = 2;
    //   int64 value = 3;
    // }

    std::vector<uint8_t> uvcRequest;
    writeString(uvcRequest, 1, serialNum);
    writeUint32(uvcRequest, 2, PARAM_ZOOM);
    writeInt64(uvcRequest, 3, zoomValue);

    // Request wrapper
    std::vector<uint8_t> request;
    writeBool(request, 1, true);  // hasUvcRequest = true
    writeSubmessage(request, 10, uvcRequest);  // uvcRequest

    return request;
}
