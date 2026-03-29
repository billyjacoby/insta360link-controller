#include "commands.h"
#include <ArduinoJson.h>

String cmdPanTilt(uint8_t panDir, uint8_t panSpeed,
                  uint8_t tiltDir, uint8_t tiltSpeed) {
    JsonDocument doc;
    doc["cmd"] = "pan_tilt";
    doc["pan_dir"] = panDir;
    doc["pan_speed"] = panSpeed;
    doc["tilt_dir"] = tiltDir;
    doc["tilt_speed"] = tiltSpeed;

    String out;
    serializeJson(doc, out);
    return out;
}

String cmdZoom(uint16_t value) {
    JsonDocument doc;
    doc["cmd"] = "zoom";
    doc["value"] = value;

    String out;
    serializeJson(doc, out);
    return out;
}

String cmdStop() {
    return "{\"cmd\":\"stop\"}";
}

String cmdCenter() {
    return "{\"cmd\":\"center\"}";
}

String cmdStatus() {
    return "{\"cmd\":\"status\"}";
}
