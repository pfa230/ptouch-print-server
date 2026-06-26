#include <string.h>
#include "protocol.h"
#include "tables.h"

size_t pt_cmd_init(uint8_t *o)
{
    o[0] = 0x1b; o[1] = 0x40;
    return 2;
}

size_t pt_cmd_rasterstart(uint8_t *o, uint32_t flags)
{
    o[0] = 0x1b; o[1] = 0x69;
    o[2] = (flags & FLAG_P700_INIT) ? 0x61 : 0x52;  /* 'a' (switch mode) vs 'R' */
    o[3] = 0x01;
    return 4;
}

size_t pt_cmd_ff(uint8_t *o)
{
    o[0] = 0x0c;
    return 1;
}

size_t pt_cmd_eject(uint8_t *o)
{
    o[0] = 0x1a;
    return 1;
}

size_t pt_cmd_printinfo(uint8_t *o, uint8_t mm)
{
    o[0] = 0x1b; o[1] = 0x69; o[2] = 0x63; o[3] = 0x84;
    o[4] = 0x00; o[5] = mm; o[6] = 0x00; o[7] = 0x00;
    return 8;
}

size_t pt_cmd_setmode(uint8_t *o, uint8_t flags)
{
    o[0] = 0x1b; o[1] = 0x69; o[2] = 0x4d; o[3] = flags;
    return 4;
}

size_t pt_cmd_setadvanced(uint8_t *o, uint8_t flags)
{
    o[0] = 0x1b; o[1] = 0x69; o[2] = 0x4b; o[3] = flags;
    return 4;
}

size_t pt_cmd_status_request(uint8_t *o)
{
    o[0] = 0x1b; o[1] = 0x69; o[2] = 0x53;
    return 3;
}

size_t pt_cmd_margin(uint8_t *o, uint16_t dots)
{
    o[0] = 0x1b; o[1] = 0x69; o[2] = 0x64;
    o[3] = (uint8_t)(dots & 0xff);
    o[4] = (uint8_t)(dots >> 8);
    return 5;
}

size_t pt_cmd_sendraster(uint8_t *o, const uint8_t *data, size_t len)
{
    o[0] = 0x47;
    o[1] = (uint8_t)len;
    o[2] = 0x00;
    memcpy(o + 3, data, len);
    return len + 3;
}

size_t pt_cmd_sendraster_packbits(uint8_t *o, const uint8_t *data, size_t len)
{
    o[0] = 0x47;
    o[1] = (uint8_t)(len + 1);
    o[2] = 0x00;
    o[3] = (uint8_t)(len - 1);
    memcpy(o + 4, data, len);
    return len + 4;
}
