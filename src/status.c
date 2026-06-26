#include <string.h>
#include "status.h"
#include "tables.h"

int pt_parse_status(const uint8_t buf[32], pt_status *s)
{
    if (buf[0] != 0x80 || buf[1] != 0x20) {  /* printheadmark + size, per upstream */
        return -1;
    }
    memcpy(s->raw, buf, 32);
    s->tape_mm = buf[10];
    s->tape_px = pt_tape_px(buf[10]);
    s->error = (uint16_t)(buf[8] | (buf[9] << 8));
    return 0;
}
