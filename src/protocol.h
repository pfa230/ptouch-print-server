#ifndef PT_PROTOCOL_H
#define PT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Brother P-touch raster command builders. Each writes bytes into the caller's
 * buffer `o` and returns the number of bytes written. ALL device byte sequences
 * live here so the adapter never hardcodes Brother bytes. Legacy commands were
 * verified on PT-2730; extended setup matches the P710BT geometry probes. */

size_t pt_cmd_invalidate(uint8_t *o);                        /* 100 zero bytes         */
size_t pt_cmd_init(uint8_t *o);                              /* 1b 40            ESC @  */
size_t pt_cmd_rasterstart(uint8_t *o, uint32_t flags);       /* 1b6952 01 / 1b6961 01  */
size_t pt_cmd_ff(uint8_t *o);                                /* 0c               feed   */
size_t pt_cmd_eject(uint8_t *o);                             /* 1a               eject  */
size_t pt_cmd_printinfo(uint8_t *o, uint8_t mm);             /* 1b6963 84 00 mm 00 00  */
/* P710BT: 13-byte ESC i z, actual page rows (LE32), page 0 fresh / 1 continued. */
size_t pt_cmd_printinfo_extended(uint8_t *o, uint8_t mm, uint32_t rows, uint8_t page);
size_t pt_cmd_compression(uint8_t *o, uint8_t mode);         /* 4d mode (02 PackBits)  */
size_t pt_cmd_auto_status(uint8_t *o, bool enabled);         /* 1b6921 00 on / 01 off  */
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
