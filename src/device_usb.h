#ifndef PT_DEVICE_USB_H
#define PT_DEVICE_USB_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */
#include <pappl/pappl.h> /* pappl_device_t */

/* Register the custom "ptouch://" libusb device scheme with PAPPL.
 * Must be called before any device list/open. */
void pt_usb_register(void);

/* True iff a USB device with exactly this vid:pid is on the bus.
 * Enumeration only: no libusb_open, no control transfer to the device.
 * Used by the status callback to tell "unplugged" from "in use". */
bool pt_usb_present(uint16_t vid, uint16_t pid);

/* Short-timeout (300 ms) status read for status_cb; bounds the mute-device
 * retry loop. Same return convention as the scheme read. */
ssize_t pt_usb_read_status(pappl_device_t *device, void *buffer, size_t bytes);

#endif /* PT_DEVICE_USB_H */
