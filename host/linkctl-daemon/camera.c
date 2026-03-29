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

static IOUSBDeviceInterface **device = NULL;

// --- IOKit USB helpers ---

static int uvc_set_cur(uint8_t unit, uint8_t selector,
                        void *data, uint16_t length) {
    if (!device) return -1;

    IOUSBDevRequest req;
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface);
    req.bRequest = UVC_SET_CUR;
    req.wValue = (selector << 8);
    req.wIndex = (unit << 8);  // unit ID in high byte, interface 0 in low byte
    req.wLength = length;
    req.wLenDone = 0;
    req.pData = data;

    IOReturn kr = (*device)->DeviceRequest(device, &req);
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

    IOUSBDevRequest req;
    req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface);
    req.bRequest = UVC_GET_CUR;
    req.wValue = (selector << 8);
    req.wIndex = (unit << 8);
    req.wLength = length;
    req.wLenDone = 0;
    req.pData = data;

    IOReturn kr = (*device)->DeviceRequest(device, &req);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "[camera] GET_CUR failed (unit=0x%02x sel=0x%02x): 0x%08x\n",
                unit, selector, kr);
        return -1;
    }
    return (int)req.wLenDone;
}

// --- Public API ---

int camera_init(void) {
    // No global init needed for IOKit
    return 0;
}

int camera_open(void) {
    if (device) return 0;  // Already open

    // Match by VID/PID
    CFMutableDictionaryRef matchDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchDict) {
        fprintf(stderr, "[camera] IOServiceMatching failed\n");
        return -1;
    }

    UInt16 vid = INSTA360_VID;
    UInt16 pid = INSTA360_PID;
    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &pid);
    CFDictionarySetValue(matchDict, CFSTR(kUSBVendorID), vidRef);
    CFDictionarySetValue(matchDict, CFSTR(kUSBProductID), pidRef);
    CFRelease(vidRef);
    CFRelease(pidRef);

    // IOServiceGetMatchingService consumes matchDict
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matchDict);
    if (!service) {
        return -1;  // Not found — caller can retry
    }

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
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
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

    printf("[camera] Insta360 Link found via IOKit\n");
    return 0;
}

void camera_close(void) {
    if (device) {
        (*device)->Release(device);
        device = NULL;
    }
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
    if (rc < 0) return -1;
    return (int)(data[0] | (data[1] << 8));
}

camera_status_t camera_get_status(void) {
    camera_status_t status;
    status.connected = camera_is_connected();
    status.zoom = 0;

    if (status.connected) {
        int z = camera_get_zoom();
        if (z >= 0) status.zoom = (uint16_t)z;
    }

    return status;
}
