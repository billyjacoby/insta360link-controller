#include "camera.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include <stdio.h>
#include <string.h>

// UVC request codes
#define UVC_SET_CUR 0x01
#define UVC_GET_CUR 0x81
#define UVC_GET_MIN 0x82
#define UVC_GET_MAX 0x83
#define UVC_GET_RES 0x84
#define UVC_GET_LEN 0x85
#define UVC_GET_INFO 0x86

typedef struct {
    bool validated;
    uint16_t zoom_min;
    uint16_t zoom_max;
    uint16_t zoom_res;
} link2_pro_capabilities_t;

static IOUSBDeviceInterface320 **device = NULL;
static bool link2_pro_selected = false;
static bool auto_camera_selected = false;
static link2_pro_capabilities_t link2_pro_capabilities;
static settings_t image_settings;
pthread_mutex_t camera_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- IOKit USB helpers ---

static int uvc_set_cur(uint8_t unit, uint8_t selector,
                        void *data, uint16_t length) {
    if (!device) return -1;

    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface);
    req.bRequest = UVC_SET_CUR;
    req.wValue = (selector << 8);
    req.wIndex = (unit << 8);  // unit ID in high byte, interface 0 in low byte
    req.wLength = length;
    req.wLenDone = 0;
    req.pData = data;
    req.noDataTimeout = 1000;
    req.completionTimeout = 1500;

    IOReturn kr = (*device)->DeviceRequestTO(device, &req);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "[camera] SET_CUR failed (unit=0x%02x sel=0x%02x): 0x%08x\n",
                unit, selector, kr);
        return -1;
    }
    return 0;
}

static int uvc_get_cur(uint8_t unit, uint8_t selector,
                        void *data, uint16_t length) {
    if (!device) return -1;

    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface);
    req.bRequest = UVC_GET_CUR;
    req.wValue = (selector << 8);
    req.wIndex = (unit << 8);
    req.wLength = length;
    req.wLenDone = 0;
    req.pData = data;
    req.noDataTimeout = 1000;
    req.completionTimeout = 1500;

    IOReturn kr = (*device)->DeviceRequestTO(device, &req);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "[camera] GET_CUR failed (unit=0x%02x sel=0x%02x): 0x%08x\n",
                unit, selector, kr);
        return -1;
    }
    return (int)req.wLenDone;
}

static bool descriptor_bit(const uint8_t *mask, size_t length, unsigned bit) {
    return bit / 8 < length && (mask[bit / 8] & (1u << (bit % 8)));
}

static uint16_t le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int settings_usb_io(void *context, uint8_t interface, uint8_t unit,
                           uint8_t selector, uint8_t request,
                           uint8_t *data, uint16_t length) {
    (void)context;
    if (!device) return SETTINGS_IO_DISCONNECTED;
    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(
        request == UVC_SET_CUR ? kUSBOut : kUSBIn, kUSBClass, kUSBInterface);
    req.bRequest = request;
    req.wValue = (uint16_t)selector << 8;
    req.wIndex = ((uint16_t)unit << 8) | interface;
    req.wLength = length;
    req.pData = data;
    req.noDataTimeout = 1000;
    req.completionTimeout = 1500;
    IOReturn kr = (*device)->DeviceRequestTO(device, &req);
    if (kr == kIOReturnNoDevice || kr == kIOReturnNotAttached || kr == kIOReturnOffline)
        return SETTINGS_IO_DISCONNECTED;
    // A stalled / unsupported selector is a per-setting failure, not removal.
    return kr == kIOReturnSuccess && req.wLenDone == length ? 0 : -1;
}

static void discover_settings(uint16_t product) {
    settings_init(&image_settings, settings_usb_io, NULL);
    image_settings.error = "Unable to read active UVC configuration";
    UInt8 count = 0, active = 0;
    if ((*device)->GetNumberOfConfigurations(device, &count) != kIOReturnSuccess ||
        (*device)->GetConfiguration(device, &active) != kIOReturnSuccess || !active) return;
    for (UInt8 i = 0; i < count; ++i) {
        IOUSBConfigurationDescriptorPtr config = NULL;
        if ((*device)->GetConfigurationDescriptorPtr(device, i, &config) != kIOReturnSuccess ||
            !config) continue;
        const uint8_t *data = (const uint8_t *)config;
        if (data[0] >= 9 && data[1] == 2 && data[5] == active) {
            settings_discover_for_device(&image_settings, data, le16(data + 2),
                                         INSTA360_VID, product);
            return;
        }
    }
}

/* This helper is deliberately read-only: Link 2 Pro validation never sends a
 * control write. */
static int uvc_get(uint8_t unit, uint8_t selector, uint8_t request,
                   void *data, uint16_t length) {
    if (!device) return -1;

    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface);
    req.bRequest = request;
    req.wValue = (uint16_t)selector << 8;
    req.wIndex = (uint16_t)unit << 8;
    req.wLength = length;
    req.pData = data;
    req.noDataTimeout = 1000;
    req.completionTimeout = 1500;

    IOReturn kr = (*device)->DeviceRequestTO(device, &req);
    if (kr != kIOReturnSuccess || req.wLenDone != length) {
        fprintf(stderr, "[camera] read-only UVC probe failed (unit=0x%02x sel=0x%02x req=0x%02x): 0x%08x bytes=%u/%u\n",
                unit, selector, request, kr, req.wLenDone, length);
        return -1;
    }
    return 0;
}

static bool validate_link2_pro_descriptors(void) {
    UInt8 count = 0, active_config = 0;
    if ((*device)->GetNumberOfConfigurations(device, &count) != kIOReturnSuccess ||
        (*device)->GetConfiguration(device, &active_config) != kIOReturnSuccess ||
        active_config == 0) {
        fprintf(stderr, "[camera] Link 2 Pro configuration query failed\n");
        return false;
    }

    bool found_active = false, vc_interface_zero = false;
    bool zoom_advertised = false, pan_advertised = false, center_advertised = false;
    for (UInt8 index = 0; index < count; ++index) {
        IOUSBConfigurationDescriptorPtr config = NULL;
        if ((*device)->GetConfigurationDescriptorPtr(device, index, &config) != kIOReturnSuccess ||
            !config) {
            fprintf(stderr, "[camera] Link 2 Pro configuration descriptor query failed\n");
            return false;
        }
        const uint8_t *data = (const uint8_t *)config;
        size_t total = le16(data + 2);
        if (data[0] < 9 || data[1] != 2 || total < data[0]) {
            fprintf(stderr, "[camera] malformed Link 2 Pro configuration descriptor\n");
            return false;
        }
        if (data[5] != active_config) continue;
        found_active = true;

        bool in_vc_zero = false;
        for (size_t pos = 0; pos < total;) {
            const uint8_t *d = data + pos;
            if (total - pos < 2 || d[0] < 2 || d[0] > total - pos) {
                fprintf(stderr, "[camera] malformed Link 2 Pro subdescriptor\n");
                return false;
            }
            size_t length = d[0];
            if (d[1] == 4) {
                if (length < 9) return false;
                in_vc_zero = d[2] == 0 && d[3] == 0 && d[5] == 0x0e && d[6] == 1;
                if (in_vc_zero) vc_interface_zero = true;
            } else if (in_vc_zero && d[1] == 0x24) {
                if (length < 3) return false;
                if (d[2] == 2 && length >= 15 && d[3] == CT_UNIT_ID &&
                    le16(d + 4) == 0x0201) {
                    size_t controls = d[14];
                    if (length < 15 + controls) return false;
                    zoom_advertised = descriptor_bit(d + 15, controls, 9);
                } else if (d[2] == 6 && length >= 24 && d[3] == XU_UNIT_ID) {
                    size_t pins = d[21];
                    if (length < 24 + pins) return false;
                    size_t controls = d[22 + pins];
                    if (length < 24 + pins + controls) return false;
                    const uint8_t *mask = d + 23 + pins;
                    pan_advertised = descriptor_bit(mask, controls, SEL_PAN_TILT_RELATIVE - 1);
                    center_advertised = descriptor_bit(mask, controls, SEL_GIMBAL_CENTER - 1);
                }
            }
            pos += length;
        }
    }
    if (!found_active || !vc_interface_zero || !zoom_advertised ||
        !pan_advertised || !center_advertised) {
        fprintf(stderr, "[camera] Link 2 Pro required UVC controls are not advertised\n");
        return false;
    }
    return true;
}

static bool validate_link2_pro_controls(void) {
    uint8_t info, length[2], range[2];
    memset(&link2_pro_capabilities, 0, sizeof(link2_pro_capabilities));
    if (!validate_link2_pro_descriptors()) return false;

    if (uvc_get(XU_UNIT_ID, SEL_PAN_TILT_RELATIVE, UVC_GET_INFO, &info, 1) ||
        (info & 0x07) != 0x03 ||
        uvc_get(XU_UNIT_ID, SEL_PAN_TILT_RELATIVE, UVC_GET_LEN, length, 2) ||
        le16(length) != 4 ||
        uvc_get(XU_UNIT_ID, SEL_GIMBAL_CENTER, UVC_GET_INFO, &info, 1) ||
        (info & 0x07) != 0x03 ||
        uvc_get(XU_UNIT_ID, SEL_GIMBAL_CENTER, UVC_GET_LEN, length, 2) ||
        le16(length) != 8 ||
        uvc_get(CT_UNIT_ID, SEL_ZOOM_ABSOLUTE, UVC_GET_INFO, &info, 1) ||
        (info & 0x07) != 0x03 ||
        uvc_get(CT_UNIT_ID, SEL_ZOOM_ABSOLUTE, UVC_GET_MIN, range, 2)) {
        fprintf(stderr, "[camera] Link 2 Pro UVC control validation failed\n");
        return false;
    }
    link2_pro_capabilities.zoom_min = le16(range);
    if (uvc_get(CT_UNIT_ID, SEL_ZOOM_ABSOLUTE, UVC_GET_MAX, range, 2)) return false;
    link2_pro_capabilities.zoom_max = le16(range);
    if (uvc_get(CT_UNIT_ID, SEL_ZOOM_ABSOLUTE, UVC_GET_RES, range, 2)) return false;
    link2_pro_capabilities.zoom_res = le16(range);
    if (link2_pro_capabilities.zoom_res == 0 ||
        link2_pro_capabilities.zoom_min > link2_pro_capabilities.zoom_max) {
        fprintf(stderr, "[camera] invalid Link 2 Pro zoom range\n");
        return false;
    }
    link2_pro_capabilities.validated = true;
    fprintf(stderr, "[camera] Link 2 Pro zoom range: %u-%u, step %u\n",
           link2_pro_capabilities.zoom_min, link2_pro_capabilities.zoom_max,
           link2_pro_capabilities.zoom_res);
    return true;
}

// --- Public API ---

int camera_init(void) {
    // No global init needed for IOKit
    return 0;
}

void camera_select_link2_pro(void) {
    if (device) {
        fprintf(stderr, "[camera] Link 2 Pro profile must be selected before camera_open\n");
        return;
    }
    link2_pro_selected = true;
    auto_camera_selected = false;
    memset(&link2_pro_capabilities, 0, sizeof(link2_pro_capabilities));
}

void camera_select_auto(void) {
    if (device) return;
    auto_camera_selected = true;
}

static int matching_devices(uint16_t product, io_iterator_t *iterator) {
    // Match exact VID/PID pairs: legacy IOUSB compatibility matching on macOS
    // does not reliably enumerate modern IOUSBHostDevices with a vendor-only match.
    CFMutableDictionaryRef matchDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchDict) {
        fprintf(stderr, "[camera] IOServiceMatching failed\n");
        return -1;
    }

    UInt16 vid = INSTA360_VID;
    UInt16 pid = product;
    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &pid);
    if (!vidRef || !pidRef) {
        if (vidRef) CFRelease(vidRef);
        if (pidRef) CFRelease(pidRef);
        CFRelease(matchDict);
        return -1;
    }
    CFDictionarySetValue(matchDict, CFSTR(kUSBVendorID), vidRef);
    CFDictionarySetValue(matchDict, CFSTR(kUSBProductID), pidRef);
    CFRelease(vidRef);
    CFRelease(pidRef);

    return IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, iterator) == kIOReturnSuccess ? 0 : -1;
}

int camera_open(void) {
    if (device) return 0;

    // Match only explicitly supported models. Reject ambiguous selection rather
    // than silently controlling a different camera from the preview selection.
    const uint16_t products[] = {INSTA360_PID, INSTA360_LINK2_PRO_PID};
    io_service_t service = IO_OBJECT_NULL, candidate;
    bool ambiguous = false;
    uint16_t selected_product = link2_pro_selected ? INSTA360_LINK2_PRO_PID : INSTA360_PID;
    for (unsigned i = 0; i < sizeof(products) / sizeof(products[0]); ++i) {
        if (!auto_camera_selected && products[i] != selected_product) continue;
        io_iterator_t iterator = IO_OBJECT_NULL;
        if (matching_devices(products[i], &iterator) != 0) {
            if (service) IOObjectRelease(service);
            return -1;
        }
        while ((candidate = IOIteratorNext(iterator))) {
            if (!service) {
                service = candidate;
                selected_product = products[i];
            } else {
                ambiguous = true;
                IOObjectRelease(candidate);
            }
        }
        IOObjectRelease(iterator);
    }
    if (ambiguous) {
        IOObjectRelease(service);
        fprintf(stderr, "[camera] Multiple supported cameras found; connect one camera at a time\n");
        return -1;
    }
    if (!service) {
        return -1;  // Not found — caller can retry
    }
    link2_pro_selected = selected_product == INSTA360_LINK2_PRO_PID;

    // Get plugin interface
    IOCFPlugInInterface **plugIn = NULL;
    SInt32 score;
    kern_return_t kr = IOCreatePlugInInterfaceForService(
        service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
        &plugIn, &score);
    IOObjectRelease(service);

    if (kr != kIOReturnSuccess || !plugIn) {
        fprintf(stderr, "[camera] IOCreatePlugInInterface failed: 0x%08x\n", kr);
        return -1;
    }

    // Query for device interface
    HRESULT res = (*plugIn)->QueryInterface(plugIn,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),
        (LPVOID *)&device);
    (*plugIn)->Release(plugIn);

    if (res || !device) {
        fprintf(stderr, "[camera] QueryInterface failed: 0x%08x\n", (int)res);
        device = NULL;
        return -1;
    }

    // NOTE: We intentionally do NOT call USBDeviceOpen().
    // DeviceRequest() works without it for class-specific requests,
    // allowing us to coexist with the Apple UVC kernel driver.

    if (link2_pro_selected && !validate_link2_pro_controls()) {
        camera_close();
        return -1;
    }

    discover_settings(selected_product);
    fprintf(stderr, "[camera] Insta360 %s found via IOKit\n",
           link2_pro_selected ? "Link 2 Pro" : "Link");
    return 0;
}

void camera_close(void) {
    if (device) {
        (*device)->Release(device);
        device = NULL;
    }
    memset(&link2_pro_capabilities, 0, sizeof(link2_pro_capabilities));
    settings_init(&image_settings, NULL, NULL);
}

void camera_destroy(void) {
    camera_close();
}

bool camera_is_connected(void) {
    return device != NULL;
}

int camera_pan_tilt(uint8_t pan_dir, uint8_t pan_speed,
                    uint8_t tilt_dir, uint8_t tilt_speed) {
    uint8_t data[4] = { pan_dir, pan_speed, tilt_dir, tilt_speed };
    int rc = uvc_set_cur(XU_UNIT_ID, SEL_PAN_TILT_RELATIVE, data, sizeof(data));
    if (rc != 0) {
        camera_close();
    }
    return rc;
}

int camera_stop(void) {
    return camera_pan_tilt(DIR_STOP, 1, DIR_STOP, 1);
}

int camera_center(void) {
    uint8_t data[8] = {0};
    int rc = uvc_set_cur(XU_UNIT_ID, SEL_GIMBAL_CENTER, data, sizeof(data));
    if (rc != 0) {
        camera_close();
    }
    return rc;
}

int camera_zoom(uint16_t value) {
    if (value < ZOOM_MIN || value > ZOOM_MAX) return -1;
    if (link2_pro_selected &&
        (!link2_pro_capabilities.validated ||
         value < link2_pro_capabilities.zoom_min ||
         value > link2_pro_capabilities.zoom_max ||
         (value - link2_pro_capabilities.zoom_min) % link2_pro_capabilities.zoom_res != 0)) {
        fprintf(stderr, "[camera] zoom value %u is unsupported by Link 2 Pro\n", value);
        return -1;
    }

    uint8_t data[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    int rc = uvc_set_cur(CT_UNIT_ID, SEL_ZOOM_ABSOLUTE, data, sizeof(data));
    if (rc != 0) {
        camera_close();
    }
    return rc;
}

int camera_get_zoom(void) {
    uint8_t data[2] = {0};
    int rc = uvc_get_cur(CT_UNIT_ID, SEL_ZOOM_ABSOLUTE, data, sizeof(data));
    if (rc != sizeof(data)) {
        camera_close();
        return -1;
    }
    return (int)(data[0] | (data[1] << 8));
}

camera_status_t camera_get_status(void) {
    camera_status_t status;
    status.connected = camera_is_connected();
    status.zoom = 0;

    if (status.connected) {
        int z = camera_get_zoom();
        if (z >= 0) status.zoom = (uint16_t)z;
        status.connected = camera_is_connected();
    }

    return status;
}

cJSON *camera_settings_get(void) {
    if (!device) return settings_error("Camera not connected");
    cJSON *result = settings_snapshot(&image_settings);
    if (image_settings.disconnected) camera_close();
    return result;
}

cJSON *camera_setting_set(const char *id, int64_t value) {
    if (!device) return settings_error("Camera not connected");
    cJSON *result = settings_set(&image_settings, id, value);
    if (image_settings.disconnected) camera_close();
    return result;
}
