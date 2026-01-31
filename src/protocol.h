#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include <vector>

// Protobuf field numbers and wire types
// Wire type 0 = Varint, 2 = Length-delimited

// ParamType enum values
enum ParamType {
    PARAM_ZOOM = 1,
    PARAM_PAN_TILT_RELATIVE = 12
};

// ControlSelector enum values
enum ControlSelector {
    XU_PANTILT_RELATIVE_CONTROL = 22
};

class ProtocolEncoder {
public:
    // Encode a ControlRequest message
    static std::vector<uint8_t> encodeControlRequest(const String& token);

    // Encode a HeartbeatRequest message
    static std::vector<uint8_t> encodeHeartbeatRequest();

    // Encode a UVCExtendRequest for pan/tilt
    static std::vector<uint8_t> encodePanTiltRequest(
        const String& serialNum,
        int8_t panDir, uint8_t panSpeed,
        int8_t tiltDir, uint8_t tiltSpeed
    );

    // Encode a UVCRequest for zoom
    static std::vector<uint8_t> encodeZoomRequest(
        const String& serialNum,
        uint32_t zoomValue
    );

private:
    // Protobuf encoding helpers
    static void writeVarint(std::vector<uint8_t>& buf, uint64_t value);
    static void writeTag(std::vector<uint8_t>& buf, uint32_t fieldNum, uint8_t wireType);
    static void writeString(std::vector<uint8_t>& buf, uint32_t fieldNum, const String& value);
    static void writeInt64(std::vector<uint8_t>& buf, uint32_t fieldNum, int64_t value);
    static void writeUint32(std::vector<uint8_t>& buf, uint32_t fieldNum, uint32_t value);
    static void writeBool(std::vector<uint8_t>& buf, uint32_t fieldNum, bool value);
    static void writeBytes(std::vector<uint8_t>& buf, uint32_t fieldNum, const std::vector<uint8_t>& data);
    static void writeRepeatedInt32(std::vector<uint8_t>& buf, uint32_t fieldNum, const std::vector<int32_t>& values);
    static void writeSubmessage(std::vector<uint8_t>& buf, uint32_t fieldNum, const std::vector<uint8_t>& submsg);
};

#endif
