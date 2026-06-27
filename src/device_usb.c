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

typedef struct {
    libusb_device_handle *h;
    int detached;
} pt_usb_conn;

/* Private libusb context (isolated from PAPPL's own USB code); process-lifetime. */
static libusb_context *g_ctx = NULL;

static bool pt_usb_list(pappl_device_cb_t cb, void *data,
                        pappl_deverror_cb_t err_cb, void *err_data)
{
    (void)err_cb; (void)err_data;
    libusb_device **devs;
    ssize_t n = libusb_get_device_list(g_ctx, &devs);
    if (n < 0)
        return false;

    bool stop = false;
    for (ssize_t i = 0; i < n && !stop; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(devs[i], &d) != 0 || d.idVendor != PT_VID)
            continue;
        const pt_dev *dev = pt_lookup_dev(d.idVendor, d.idProduct);
        if (!dev)
            continue;

        char uri[256], id[256], info[128];
        snprintf(uri, sizeof uri, "ptouch://Brother/%s", dev->name);
        snprintf(id, sizeof id, "MFG:Brother;MDL:%s;CMD:PT-CBP;", dev->name);
        snprintf(info, sizeof info, "%s (USB)", dev->name);
        stop = cb(info, uri, id, data);  /* cb returns true to stop early */
    }
    libusb_free_device_list(devs, 1);
    return stop;
}

static bool pt_usb_open(pappl_device_t *device, const char *device_uri, const char *name)
{
    (void)name;
    const char *slash = strrchr(device_uri, '/');
    const char *model = slash ? slash + 1 : device_uri;

    libusb_device **devs;
    ssize_t n = libusb_get_device_list(g_ctx, &devs);
    if (n < 0)
        return false;

    libusb_device_handle *h = NULL;
    int detached = 0;
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(devs[i], &d) != 0 || d.idVendor != PT_VID)
            continue;
        const pt_dev *dev = pt_lookup_dev(d.idVendor, d.idProduct);
        if (!dev || (model[0] && strcmp(dev->name, model) != 0))
            continue;

        if (libusb_open(devs[i], &h) != 0) { h = NULL; continue; }
        if (libusb_kernel_driver_active(h, 0) == 1 &&
            libusb_detach_kernel_driver(h, 0) == 0)
            detached = 1;
        if (libusb_claim_interface(h, 0) != 0) {  /* unwind ladder */
            if (detached) libusb_attach_kernel_driver(h, 0);
            libusb_close(h);
            h = NULL; detached = 0;
            continue;
        }
        break;  /* success */
    }
    libusb_free_device_list(devs, 1);
    if (!h)
        return false;

    pt_usb_conn *c = calloc(1, sizeof *c);
    if (!c) {
        libusb_release_interface(h, 0);
        if (detached) libusb_attach_kernel_driver(h, 0);
        libusb_close(h);
        return false;
    }
    c->h = h;
    c->detached = detached;
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
        if (rc == 0 || (rc == LIBUSB_ERROR_TIMEOUT && t > 0)) {
            sent += (size_t)t;
            continue;
        }
        return -1;  /* real error, or a timeout with no progress */
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
    (void)device;
    strncpy(buffer, "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;", bufsize - 1);
    buffer[bufsize - 1] = '\0';
    return buffer;
}

bool pt_usb_present(uint16_t vid, uint16_t pid)
{
    if (!g_ctx)
        libusb_init(&g_ctx);

    libusb_device **devs;
    ssize_t n = libusb_get_device_list(g_ctx, &devs);
    if (n < 0)
        return false;

    bool found = false;
    for (ssize_t i = 0; i < n && !found; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(devs[i], &d) != 0)
            continue;
        if (d.idVendor == vid && d.idProduct == pid)
            found = true;  /* cached descriptor compare; no device I/O */
    }
    libusb_free_device_list(devs, 1);
    return found;
}

void pt_usb_register(void)
{
    if (!g_ctx)
        libusb_init(&g_ctx);
    papplDeviceAddScheme("ptouch", PAPPL_DEVTYPE_CUSTOM_LOCAL,
                         pt_usb_list, pt_usb_open, pt_usb_close,
                         pt_usb_read, pt_usb_write, pt_usb_status, pt_usb_id);
}
