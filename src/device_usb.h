#ifndef PT_DEVICE_USB_H
#define PT_DEVICE_USB_H

/* Register the custom "ptouch://" libusb device scheme with PAPPL.
 * Must be called before any device list/open. */
void pt_usb_register(void);

#endif /* PT_DEVICE_USB_H */
