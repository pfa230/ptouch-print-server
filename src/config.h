#ifndef PT_CONFIG_H
#define PT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Startup setting parsers. Pure: no environment access, no I/O. */

/* PTOUCH_IDLE_POLL_SECONDS. NULL/empty yields -1 (follow every PAPPL status
 * request); otherwise a non-negative decimal integer, where 0 disables idle
 * device access. Anything else is rejected. */
bool pt_parse_idle_poll(const char *text, int *seconds);

/* PTOUCH_BT_ADDRESS as XX:XX:XX:XX:XX:XX hex, most significant byte first. */
bool pt_parse_bt_address(const char *text, uint8_t address[6]);

#endif /* PT_CONFIG_H */
