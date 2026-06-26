/* Custom "ptouch://" PAPPL device scheme over raw libusb with bounded timeouts.
 * T1: scheme registered with stub callbacks so the app builds and runs; the real
 * libusb enumeration / open / bulk transfer is filled in T2 (#31). */

#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <pappl/pappl.h>
#include "device_usb.h"

static bool pt_usb_list(pappl_device_cb_t cb, void *data,
                        pappl_deverror_cb_t err_cb, void *err_data)
{
    (void)cb; (void)data; (void)err_cb; (void)err_data;
    return false;  /* T2: enumerate 04f9:* by serial */
}

static bool pt_usb_open(pappl_device_t *device, const char *device_uri, const char *name)
{
    (void)device; (void)device_uri; (void)name;
    return false;  /* T2: libusb_open by serial + claim */
}

static void pt_usb_close(pappl_device_t *device) { (void)device; }

static ssize_t pt_usb_read(pappl_device_t *device, void *buffer, size_t bytes)
{
    (void)device; (void)buffer; (void)bytes;
    return -1;  /* T2: bounded bulk read */
}

static ssize_t pt_usb_write(pappl_device_t *device, const void *buffer, size_t bytes)
{
    (void)device; (void)buffer; (void)bytes;
    return -1;  /* T2: bounded bulk write */
}

static pappl_preason_t pt_usb_status(pappl_device_t *device)
{
    (void)device;
    return PAPPL_PREASON_NONE;
}

static char *pt_usb_id(pappl_device_t *device, char *buffer, size_t bufsize)
{
    (void)device;
    strncpy(buffer, "MFG:Brother;MDL:PT-2730;", bufsize - 1);
    buffer[bufsize - 1] = '\0';
    return buffer;
}

void pt_usb_register(void)
{
    papplDeviceAddScheme("ptouch", PAPPL_DEVTYPE_CUSTOM_LOCAL,
                         pt_usb_list, pt_usb_open, pt_usb_close,
                         pt_usb_read, pt_usb_write, pt_usb_status, pt_usb_id);
}
