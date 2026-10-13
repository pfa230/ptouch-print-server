#ifndef PT_DEVICE_USB_H
#define PT_DEVICE_USB_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */
#include <pappl/pappl.h> /* pappl_device_t */
#include "tables.h"

/* NULL/empty selects the default; recognized models without a print path fail. */
const pt_dev *pt_usb_supported_model(const char *model);

/* Exact supported URI resolution and synthetic identity; no hardware reads. */
const pt_dev *pt_usb_model_from_uri(const char *uri);
void pt_usb_format_uri(const pt_dev *model, char *buffer, size_t size);
void pt_usb_format_id(const pt_dev *model, char *buffer, size_t size);

/* Exact supported model URI; test/network transports never use USB data. */
bool pt_usb_is_uri(const char *uri);

/* Register the custom "ptouch://" libusb device scheme with PAPPL.
 * Must be called before any device list/open. */
void pt_usb_register(void);

/* Number of matching devices, or -1 if enumeration fails. No device I/O. */
ssize_t pt_usb_count(uint16_t vid, uint16_t pid);

/* True iff exactly one USB device with this vid:pid is on the bus.
 * Enumeration only: no libusb_open, no control transfer to the device.
 * Used by the status callback to tell "unplugged" from "in use". */
bool pt_usb_present(uint16_t vid, uint16_t pid);

/* Short-timeout (300 ms) status read for status_cb; bounds the mute-device
 * retry loop. Same return convention as the scheme read. */
/* device_uri must be the owning printer's URI. The guard runs before accessing
 * connection data; PAPPL provides no public device URI accessor. */
ssize_t pt_usb_read_status(pappl_device_t *device, const char *device_uri,
                           void *buffer, size_t bytes);

#endif /* PT_DEVICE_USB_H */
