#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GET_CUR = 0x81, GET_LEN = 0x85, GET_INFO = 0x86 };
static unsigned query_failures;
static bool ai_current;

/* Candidate addresses from published Link-family maps, with lengths observed
 * on Link 2 Pro. Reading them does not establish physical behavior. Gesture
 * selectors 05/1b also have official SDK wire evidence; see ai-camera-controls.md. */
static uint16_t ai_candidate_size(uint8_t unit, uint8_t selector) {
    if (unit == 11 && selector >= 1 && selector <= 5) return 1;
    if (unit != 9) return 0;
    switch (selector) {
    case 0x02: return 61;
    case 0x05: case 0x12: case 0x13: return 1;
    case 0x06: return 5;
    case 0x15: return 16;
    case 0x18: return 4;
    case 0x1b: return 2;
    default: return 0;
    }
}

static unsigned le16(const uint8_t *p) {
    return p[0] | ((unsigned)p[1] << 8);
}

static void hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) printf("%s%02x", i ? " " : "", p[i]);
}

static bool bit(const uint8_t *mask, size_t size, unsigned index) {
    return index / 8 < size && (mask[index / 8] & (1u << (index % 8)));
}

/* The only explicit USB transfer path: always device-to-host, with an exact
 * allowlist. GET_CUR covers standard zoom and explicitly selected AI candidates;
 * matching metadata is required before reading the latter.
 * Callers additionally gate requests on the descriptor control bitmap. */
static bool get(IOUSBDeviceInterface320 **dev, uint8_t iface, uint8_t unit,
                uint8_t selector, uint8_t request, uint8_t *data, uint16_t size) {
    if (!((request == GET_INFO && size == 1) ||
          (request == GET_LEN && size == 2) ||
          (request == GET_CUR && selector == 0x0b && size == 2) ||
          (request == GET_CUR && ai_current && size > 0 &&
           size == ai_candidate_size(unit, selector)))) {
        fprintf(stderr, "Internal error: request rejected by read-only allowlist\n");
        exit(2);
    }
    memset(data, 0, size);
    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface);
    req.bRequest = request;
    req.wValue = (uint16_t)selector << 8;
    req.wIndex = ((uint16_t)unit << 8) | iface;
    req.wLength = size;
    req.pData = data;
    req.noDataTimeout = 1000;
    req.completionTimeout = 1500;
    IOReturn result = (*dev)->DeviceRequestTO(dev, &req);
    printf("    if=%u unit=%u selector=0x%02x %s: ", iface, unit, selector,
           request == GET_INFO ? "GET_INFO" : request == GET_LEN ? "GET_LEN" : "GET_CUR");
    if (result != kIOReturnSuccess || req.wLenDone != size) {
        printf("FAILED IOReturn=0x%08x bytes=%u/%u\n", (unsigned)result,
               (unsigned)req.wLenDone, size);
        ++query_failures;
        return false;
    }
    hex(data, size);
    if (request == GET_INFO)
        printf(" (GET=%u SET=%u disabled=%u auto-update=%u async=%u)",
               !!(data[0] & 1), !!(data[0] & 2), !!(data[0] & 4),
               !!(data[0] & 8), !!(data[0] & 16));
    else if (size <= 2)
        printf(" (%u)", size == 1 ? data[0] : le16(data));
    putchar('\n');
    return true;
}

static void probe_xu(IOUSBDeviceInterface320 **dev, uint8_t iface,
                     const uint8_t *d, bool active) {
    unsigned pins = d[21];
    unsigned size = d[22 + pins];
    const uint8_t *mask = d + 23 + pins;
    printf("  Extension Unit id=%u GUID-bytes=", d[3]);
    hex(d + 4, 16);
    printf(" bNumControls=%u sources=", d[20]);
    hex(d + 22, pins);
    printf(" bmControls=");
    hex(mask, size);
    putchar('\n');
    unsigned advertised = 0;
    for (unsigned index = 0; index < size * 8; ++index)
        if (bit(mask, size, index)) ++advertised;
    if (advertised != d[20])
        printf("    Descriptor inconsistency: bNumControls=%u, bitmap has %u set bits; "
               "using bitmap to gate selector reads.\n", d[20], advertised);
    if (d[3] == 9) {
        printf("    Original Link XU9 pan/tilt selector 0x16: %s\n",
               bit(mask, size, 0x16 - 1) ? "ADVERTISED" : "NOT ADVERTISED (not queried)");
        printf("    Original Link XU9 center selector 0x1a: %s\n",
               bit(mask, size, 0x1a - 1) ? "ADVERTISED" : "NOT ADVERTISED (not queried)");
    }
    if (!active) return;
    for (unsigned sel = 1; sel <= 255; ++sel) {
        if (!bit(mask, size, sel - 1)) continue;
        uint8_t info[1], length[2];
        bool info_ok = get(dev, iface, d[3], (uint8_t)sel, GET_INFO, info, sizeof(info));
        /* UVC extension controls support GET_LEN independently of GET_CUR. */
        bool length_ok = get(dev, iface, d[3], (uint8_t)sel, GET_LEN, length, sizeof(length));
        uint16_t candidate_size = ai_candidate_size(d[3], (uint8_t)sel);
        if (ai_current && candidate_size && info_ok && (info[0] & 1) &&
            length_ok && le16(length) == candidate_size) {
            uint8_t current[64];
            get(dev, iface, d[3], (uint8_t)sel, GET_CUR, current, candidate_size);
        }
    }
}

static void probe_camera(IOUSBDeviceInterface320 **dev, uint8_t iface,
                         const uint8_t *d, bool active) {
    unsigned size = d[14];
    const uint8_t *mask = d + 15;
    printf("    Camera Terminal bmControls=");
    hex(mask, size);
    printf("\n    Zoom absolute selector 0x0b (bitmap bit 9): %s\n",
           bit(mask, size, 9) ? "ADVERTISED" : "NOT ADVERTISED (not queried)");
    if (!active || !bit(mask, size, 9)) return;
    uint8_t info[1], current[2];
    printf("    Zoom length=2 (UVC-defined; GET_LEN is not a Camera Terminal request)\n");
    if (get(dev, iface, d[3], 0x0b, GET_INFO, info, sizeof(info)) && (info[0] & 1))
        get(dev, iface, d[3], 0x0b, GET_CUR, current, sizeof(current));
}

static bool configuration(IOUSBDeviceInterface320 **dev, const uint8_t *data,
                          uint8_t active_config) {
    size_t total = le16(data + 2);
    if (data[0] < 9 || total < data[0] || data[1] != 2) return false;
    bool active = data[5] == active_config;
    printf("Configuration value=%u totalLength=%zu interfaces=%u %s\n",
           data[5], total, data[4], active ? "ACTIVE" : "INACTIVE (descriptors only)");
    bool vc = false;
    uint8_t iface = 0;
    for (size_t pos = 0; pos < total;) {
        const uint8_t *d = data + pos;
        if (total - pos < 2 || d[0] < 2 || d[0] > total - pos) return false;
        unsigned n = d[0];
        if (d[1] == 4) {
            if (n < 9) return false;
            iface = d[2];
            vc = d[5] == 0x0e && d[6] == 1 && d[3] == 0;
            printf("Interface=%u alt=%u class=0x%02x subclass=0x%02x protocol=0x%02x%s\n",
                   iface, d[3], d[5], d[6], d[7], vc ? " VideoControl" : "");
        } else if (vc && d[1] == 0x24) {
            if (n < 3) return false;
            printf("  VC descriptor offset=%zu raw=", pos);
            hex(d, n);
            putchar('\n');
            switch (d[2]) {
            case 1:
                if (n < 12 || n < 12u + d[11]) return false;
                printf("  VC Header UVC=%x.%02x classTotalLength=%u clock=%u Hz\n",
                       d[4], d[3], le16(d + 5),
                       (unsigned)d[7] | ((unsigned)d[8] << 8) |
                       ((unsigned)d[9] << 16) | ((unsigned)d[10] << 24));
                break;
            case 2:
                if (n < 8) return false;
                printf("  Input Terminal id=%u type=0x%04x\n", d[3], le16(d + 4));
                if (le16(d + 4) == 0x0201) {
                    if (n < 15 || n < 15u + d[14]) return false;
                    probe_camera(dev, iface, d, active);
                }
                break;
            case 3:
                if (n < 9) return false;
                printf("  Output Terminal id=%u type=0x%04x source=%u\n",
                       d[3], le16(d + 4), d[7]);
                break;
            case 4:
                if (n < 6 || n < 6u + d[4]) return false;
                printf("  Selector Unit id=%u sources=", d[3]);
                hex(d + 5, d[4]);
                putchar('\n');
                break;
            case 5:
                if (n < 9 || n < 9u + d[7]) return false;
                printf("  Processing Unit id=%u source=%u bmControls=", d[3], d[4]);
                hex(d + 8, d[7]);
                putchar('\n');
                break;
            case 6:
                if (n < 24 || n < 24u + d[21] ||
                    n < 24u + d[21] + d[22 + d[21]]) return false;
                probe_xu(dev, iface, d, active);
                break;
            default:
                printf("  Other VC entity subtype=0x%02x\n", d[2]);
            }
        }
        pos += n;
    }
    return true;
}

static bool parse_id(const char *text, uint16_t *value) {
    char *end;
    errno = 0;
    unsigned long v = strtoul(text, &end, 0);
    if (errno || end == text || *end || v > 0xffff) return false;
    *value = (uint16_t)v;
    return true;
}

int main(int argc, char **argv) {
    uint16_t vid = 0x2e1a, pid = 0x4c06;
    if (argc > 1 && strcmp(argv[argc - 1], "--ai") == 0) {
        ai_current = true;
        --argc;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [VID PID] [--ai]\nIDs accept decimal or 0x-prefixed hex.\n"
               "Default: 0x2e1a 0x4c06 (Link 2 Pro). Read-only USB diagnostics.\n", argv[0]);
        return 0;
    }
    if (argc != 1 && (argc != 3 || !parse_id(argv[1], &vid) || !parse_id(argv[2], &pid))) {
        fprintf(stderr, "Usage: %s [VID PID] [--ai]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("linkctl-probe: read-only; target VID=0x%04x PID=0x%04x\n", vid, pid);
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) return 2;
    int v = vid, p = pid;
    CFNumberRef vn = CFNumberCreate(NULL, kCFNumberIntType, &v);
    CFNumberRef pn = CFNumberCreate(NULL, kCFNumberIntType, &p);
    if (!vn || !pn) {
        if (vn) CFRelease(vn);
        if (pn) CFRelease(pn);
        CFRelease(match);
        return 2;
    }
    CFDictionarySetValue(match, CFSTR(kUSBVendorID), vn);
    CFDictionarySetValue(match, CFSTR(kUSBProductID), pn);
    CFRelease(vn);
    CFRelease(pn);
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn result = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iterator);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Matching failed: 0x%08x\n", (unsigned)result);
        return 2;
    }
    io_service_t service = IOIteratorNext(iterator);
    io_service_t extra = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (extra) {
        IOObjectRelease(extra);
        IOObjectRelease(service);
        fprintf(stderr, "Multiple matching cameras: connect only one target camera.\n");
        return 2;
    }
    if (!service) {
        fprintf(stderr, "Target camera not found.\n");
        return 1;
    }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    result = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
                                               kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (result != kIOReturnSuccess || !plugin) {
        fprintf(stderr, "USB plugin failed: 0x%08x\n", (unsigned)result);
        return 2;
    }
    IOUSBDeviceInterface320 **dev = NULL;
    HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),
                                           (LPVOID *)&dev);
    (*plugin)->Release(plugin);
    if (hr || !dev) {
        fprintf(stderr, "USB interface failed: 0x%08x\n", (unsigned)hr);
        return 2;
    }
    /* No open/seize/reset/configuration changes/interface claims/streaming. */
    UInt8 count = 0, active = 0;
    result = (*dev)->GetNumberOfConfigurations(dev, &count);
    if (result == kIOReturnSuccess) result = (*dev)->GetConfiguration(dev, &active);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Configuration information failed: 0x%08x\n", (unsigned)result);
        (*dev)->Release(dev);
        return 2;
    }
    printf("Configurations=%u active=%u\n", count, active);
    int status = 0;
    for (unsigned index = 0; index < count; ++index) {
        IOUSBConfigurationDescriptorPtr descriptor = NULL;
        result = (*dev)->GetConfigurationDescriptorPtr(dev, (UInt8)index, &descriptor);
        if (result != kIOReturnSuccess || !descriptor) {
            fprintf(stderr, "Configuration %u descriptor failed: 0x%08x\n", index, (unsigned)result);
            status = 2;
            continue;
        }
        if (!configuration(dev, (const uint8_t *)descriptor, active)) {
            fprintf(stderr, "Malformed configuration %u descriptor; stopped parsing.\n", index);
            status = 2;
        }
    }
    (*dev)->Release(dev);
    printf("Completed: %u failed/short read requests. No write requests issued.\n", query_failures);
    return status ? status : query_failures ? 3 : 0;
}
