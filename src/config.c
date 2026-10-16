#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include "config.h"

bool pt_parse_idle_poll(const char *text, int *seconds)
{
    if (!text || !*text) {
        *seconds = -1;
        return true;
    }
    long value = 0;
    for (const char *p = text; *p; p++) {
        if (!isdigit((unsigned char)*p)) return false;
        value = value * 10 + (*p - '0');
        if (value > INT_MAX) return false;
    }
    *seconds = (int)value;
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool pt_parse_bt_address(const char *text, uint8_t address[6])
{
    if (!text) return false;
    for (int i = 0; i < 6; i++) {
        const char *p = text + i * 3;
        int high = hex_digit(p[0]), low = high < 0 ? -1 : hex_digit(p[1]);
        if (low < 0 || p[2] != (i < 5 ? ':' : '\0')) return false;
        address[i] = (uint8_t)(high << 4 | low);
    }
    return true;
}
