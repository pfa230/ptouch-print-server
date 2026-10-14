#ifndef PT_DEVICE_BT_H
#define PT_DEVICE_BT_H

#include <stdbool.h>
#include <sys/types.h>   /* ssize_t */
#include <pappl/pappl.h> /* pappl_device_t */

/* Exact PT-P710BT Bluetooth URI with a well-formed address; no radio access. */
bool pt_bt_is_uri(const char *uri);
void pt_bt_format_uri(const char *address, char *buffer, size_t size);

/* Register the custom "ptouch-bt://" RFCOMM device scheme with PAPPL.
 * Must be called before any device open. */
void pt_bt_register(void);

/* 300 ms status read that assembles the requested bytes: the full count, the
 * partial count at timeout, 0 if nothing arrived, -1 on EOF or error.
 * device_uri must be the owning printer's URI; the guard runs before
 * accessing connection data. */
ssize_t pt_bt_read_status(pappl_device_t *device, const char *device_uri,
                          void *buffer, size_t bytes);

#endif /* PT_DEVICE_BT_H */
