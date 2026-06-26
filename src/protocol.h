#ifndef PT_PROTOCOL_H
#define PT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Brother P-touch raster command builders. Each writes bytes into the caller's
 * buffer `o` and returns the number of bytes written. ALL device byte sequences
 * live here so the adapter never hardcodes Brother bytes. Byte values verified
 * against hannesweisbach/ptouch-print + on PT-2730 hardware in M0. */

size_t pt_cmd_init(uint8_t *o);                              /* 1b 40            ESC @  */
size_t pt_cmd_rasterstart(uint8_t *o, uint32_t flags);       /* 1b6952 01 / 1b6961 01  */
size_t pt_cmd_ff(uint8_t *o);                                /* 0c               feed   */
size_t pt_cmd_eject(uint8_t *o);                             /* 1a               eject  */
size_t pt_cmd_printinfo(uint8_t *o, uint8_t mm);             /* 1b6963 84 00 mm 00 00  */
size_t pt_cmd_setmode(uint8_t *o, uint8_t flags);            /* 1b694d <flags>         */
size_t pt_cmd_setadvanced(uint8_t *o, uint8_t flags);        /* 1b694b <flags>         */
size_t pt_cmd_status_request(uint8_t *o);                    /* 1b6953           ESC iS */
size_t pt_cmd_margin(uint8_t *o, uint16_t dots);            /* 1b6964 lo hi    ESC i d */

/* sendraster: one raster line. Uncompressed (FLAG_NONE): 47 <len> 00 <data>.
 * PackBits variant (FLAG_RASTER_PACKBITS): 47 <len+1> 00 <len-1> <data>
 * (a single uncompressed run). Driver picks the variant by model flag. */
size_t pt_cmd_sendraster(uint8_t *o, const uint8_t *data, size_t len);
size_t pt_cmd_sendraster_packbits(uint8_t *o, const uint8_t *data, size_t len);

#endif /* PT_PROTOCOL_H */
