/* Custom "ptouch://" PAPPL device scheme over raw libusb with bounded timeouts.
 * Bypasses PAPPL's built-in usb:// (its IEEE-1284 device-ID probe hangs this
 * printer, M0 Probe H). Identity is VID/PID only — no string/descriptor reads,
 * no GET_DEVICE_ID; the only control transfers are the unavoidable open/claim. */

#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include <pappl/pappl.h>
#include "device_usb.h"
#include "tables.h"

#define PT_VID            0x04f9
#define PT_EP_OUT         0x02   /* fixed across the Brother PT family (upstream) */
#define PT_EP_IN          0x81
#define PT_USB_TIMEOUT_MS 2000
#define PT_USB_STATUS_TIMEOUT_MS 300

const pt_dev *pt_usb_supported_model(const char *model)
{
    if (!model || !*model) model = "PT-2730";
    return (!strcmp(model, "PT-2730") || !strcmp(model, "PT-P710BT"))
        ? pt_lookup_name(model) : NULL;
}

const pt_dev *pt_usb_model_from_uri(const char *uri)
{
    const char *prefix = "ptouch://Brother/";
    return uri && !strncmp(uri, prefix, strlen(prefix)) && uri[strlen(prefix)]
        ? pt_usb_supported_model(uri + strlen(prefix)) : NULL;
}

void pt_usb_format_uri(const pt_dev *model, char *buffer, size_t size)
{
    snprintf(buffer, size, "ptouch://Brother/%s", model->name);
}

void pt_usb_format_id(const pt_dev *model, char *buffer, size_t size)
{
    snprintf(buffer, size, "MFG:Brother;MDL:%s;CMD:PT-CBP;", model->name);
}

bool pt_usb_is_uri(const char *uri)
{
    return pt_usb_model_from_uri(uri) != NULL;
}

/* One application queue; registration fixes its discovery selection. */
static const pt_dev *discovery_model;

static ssize_t count_matches(libusb_device **devs, ssize_t n,
                              uint16_t vid, uint16_t pid, libusb_device **match)
{
    ssize_t count = 0;
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(devs[i], &d) == 0 &&
            d.idVendor == vid && d.idProduct == pid) {
            count++;
            if (match) *match = devs[i];
        }
    }
    return count;
}

typedef struct {
    libusb_context       *ctx;   /* per-connection context, exited on close */
    libusb_device_handle *h;
    int detached;
    const pt_dev *model;
} pt_usb_conn;

/* Every enumeration uses a FRESH libusb context. A process-lifetime context goes
 * stale across an unplug/replug in a container (no hotplug uevents reach it), so
 * libusb_get_device_list keeps returning the old node and the printer never
 * recovers (M2 T6). A fresh context always reflects current hardware. */

static bool pt_usb_list(pappl_device_cb_t cb, void *data,
                        pappl_deverror_cb_t err_cb, void *err_data)
{
    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != 0)
        return false;
    libusb_device **devs;
    ssize_t n = libusb_get_device_list(ctx, &devs);
    if (n < 0) {
        libusb_exit(ctx);
        return false;
    }

    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(devs[i], &d) != 0 || d.idVendor != PT_VID)
            continue;
        const pt_dev *dev = pt_lookup_dev(d.idVendor, d.idProduct);
        if (!dev || dev == discovery_model)
            continue;
        char message[200];
        if (pt_usb_supported_model(dev->name))
            snprintf(message, sizeof message,
                     "%s (%04x:%04x) is attached but unselected; select PTOUCH_MODEL=%s",
                     dev->name, d.idVendor, d.idProduct, dev->name);
        else
            snprintf(message, sizeof message, "%s (%04x:%04x) is recognized but not print-supported",
                     dev->name, d.idVendor, d.idProduct);
        if (err_cb) err_cb(message, err_data);
    }
    const pt_dev *supported = discovery_model;
    if (!supported) {
        libusb_free_device_list(devs, 1);
        libusb_exit(ctx);
        return false;
    }
    ssize_t count = count_matches(devs, n, supported->vid, supported->pid, NULL);
    bool stop = false;
    char uri[80], id[100], message[160];
    pt_usb_format_uri(supported, uri, sizeof uri);
    pt_usb_format_id(supported, id, sizeof id);
    if (count == 1) {
        snprintf(message, sizeof message, "%s (USB)", supported->name);
        stop = cb(message, uri, id, data);
    } else if (count > 1 && err_cb) {
        snprintf(message, sizeof message, "Multiple %s devices match; USB selection is ambiguous", supported->name);
        err_cb(message, err_data);
    }
    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return stop;
}

static bool pt_usb_open(pappl_device_t *device, const char *device_uri, const char *name)
{
    (void)name;
    const pt_dev *supported = pt_usb_model_from_uri(device_uri);
    if (!supported) {
        papplDeviceError(device, "Unsupported P-touch device URI: %s", device_uri);
        return false;
    }

    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != 0)
        return false;
    libusb_device **devs;
    ssize_t n = libusb_get_device_list(ctx, &devs);
    if (n < 0) {
        libusb_exit(ctx);
        return false;
    }

    libusb_device *match = NULL;
    ssize_t count = count_matches(devs, n, supported->vid, supported->pid, &match);
    if (count != 1) {
        if (count > 1)
            papplDeviceError(device, "Multiple %s devices match; USB selection is ambiguous", supported->name);
        else
            papplDeviceError(device, "%s is not attached", supported->name);
        libusb_free_device_list(devs, 1);
        libusb_exit(ctx);
        return false;
    }
    libusb_device_handle *h = NULL;
    int detached = 0;
    if (libusb_open(match, &h) == 0) {
        if (libusb_kernel_driver_active(h, 0) == 1 &&
            libusb_detach_kernel_driver(h, 0) == 0)
            detached = 1;
        if (libusb_claim_interface(h, 0) != 0) {  /* unwind ladder */
            if (detached) libusb_attach_kernel_driver(h, 0);
            libusb_close(h);
            h = NULL; detached = 0;
        }
    }
    libusb_free_device_list(devs, 1);
    if (!h) {
        libusb_exit(ctx);
        return false;
    }

    pt_usb_conn *c = calloc(1, sizeof *c);
    if (!c) {
        libusb_release_interface(h, 0);
        if (detached) libusb_attach_kernel_driver(h, 0);
        libusb_close(h);
        libusb_exit(ctx);
        return false;
    }
    c->ctx = ctx;
    c->h = h;
    c->detached = detached;
    c->model = supported;
    papplDeviceSetData(device, c);
    return true;
}

static void pt_usb_close(pappl_device_t *device)
{
    pt_usb_conn *c = papplDeviceGetData(device);
    if (!c)
        return;
    libusb_release_interface(c->h, 0);
    if (c->detached)
        libusb_attach_kernel_driver(c->h, 0);
    libusb_close(c->h);
    libusb_exit(c->ctx);
    free(c);
    papplDeviceSetData(device, NULL);
}

static ssize_t pt_usb_read(pappl_device_t *device, void *buffer, size_t bytes)
{
    pt_usb_conn *c = papplDeviceGetData(device);
    if (!c)
        return -1;
    int t = 0;
    int rc = libusb_bulk_transfer(c->h, PT_EP_IN, buffer, (int)bytes, &t, PT_USB_TIMEOUT_MS);
    if (rc == 0)
        return t;
    if (rc == LIBUSB_ERROR_TIMEOUT)
        return t > 0 ? t : 0;  /* benign "no data yet" → 0, not error */
    return -1;
}

ssize_t pt_usb_read_status(pappl_device_t *device, const char *device_uri,
                           void *buffer, size_t bytes)
{
    if (!device || !pt_usb_is_uri(device_uri))
        return -1;
    pt_usb_conn *c = papplDeviceGetData(device);
    if (!c)
        return -1;
    int t = 0;
    int rc = libusb_bulk_transfer(c->h, PT_EP_IN, buffer, (int)bytes, &t,
                                  PT_USB_STATUS_TIMEOUT_MS);
    if (rc == 0)
        return t;
    if (rc == LIBUSB_ERROR_TIMEOUT)
        return t > 0 ? t : 0;
    return -1;
}

static ssize_t pt_usb_write(pappl_device_t *device, const void *buffer, size_t bytes)
{
    pt_usb_conn *c = papplDeviceGetData(device);
    if (!c)
        return -1;
    const unsigned char *p = buffer;
    size_t sent = 0;
    while (sent < bytes) {  /* full-write: PAPPL won't retry the tail */
        int t = 0;
        int rc = libusb_bulk_transfer(c->h, PT_EP_OUT, (unsigned char *)p + sent,
                                      (int)(bytes - sent), &t, PT_USB_TIMEOUT_MS);
        if ((rc == 0 || rc == LIBUSB_ERROR_TIMEOUT) && t > 0) {
            sent += (size_t)t;
            continue;
        }
        return -1;  /* real error, or zero progress (timeout/ZLP with no bytes) */
    }
    return (ssize_t)bytes;
}

static pappl_preason_t pt_usb_status(pappl_device_t *device)
{
    (void)device;
    return PAPPL_PREASON_NONE;
}

static char *pt_usb_id(pappl_device_t *device, char *buffer, size_t bufsize)
{
    pt_usb_conn *c = papplDeviceGetData(device);
    if (c) pt_usb_format_id(c->model, buffer, bufsize);
    return buffer;
}

ssize_t pt_usb_count(uint16_t vid, uint16_t pid)
{
    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != 0)
        return -1;

    libusb_device **devs;
    ssize_t n = libusb_get_device_list(ctx, &devs);
    if (n < 0) {
        libusb_exit(ctx);
        return -1;
    }

    ssize_t count = count_matches(devs, n, vid, pid, NULL);
    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return count;
}

bool pt_usb_present(uint16_t vid, uint16_t pid)
{
    return pt_usb_count(vid, pid) == 1;
}

void pt_usb_register(void)
{
    discovery_model = pt_usb_supported_model(getenv("PTOUCH_MODEL"));
    papplDeviceAddScheme("ptouch", PAPPL_DEVTYPE_CUSTOM_LOCAL,
                         pt_usb_list, pt_usb_open, pt_usb_close,
                         pt_usb_read, pt_usb_write, pt_usb_status, pt_usb_id);
}
