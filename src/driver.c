#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <pappl/pappl.h>
#include "driver.h"
#include "tables.h"
#include "protocol.h"
#include "raster.h"
#include "status.h"
#include "device_usb.h"
#include <stdint.h>
#include <time.h>
#include <cups/cups.h>

/* Tape widths (mm) we may advertise; 36mm (192px) is dropped at registration
 * because it exceeds the 128-dot head (pt_tape_px>0 && <=max_px filter). */
static const int pt_tape_mm[] = { 6, 9, 12, 18, 24, 36 };

/* Roll length range and the discrete preset lengths, in 1/100 mm. */
#define PT_ROLL_MIN_CENTIMM 500     /* 5 mm   */
#define PT_ROLL_MAX_CENTIMM 30000   /* 300 mm */
static const int pt_preset_mm[] = { 25, 50, 100 };

/* Durable storage for advertised media-size names: PAPPL keeps the const char*
 * pointers in data->media[], so the strings must outlive pt_driver_cb (a stack
 * buffer would dangle). 5 names per width (roll min/max + 3 presets). */
#define PT_MEDIA_NAME_LEN 64
#define PT_MAX_MEDIA_NAMES 64
static char g_media_names[PT_MAX_MEDIA_NAMES][PT_MEDIA_NAME_LEN];

/* px in 1/100 mm so CUPS' truncating px conversion round-trips exactly.
 * cupsRasterInitHeader does cupsWidth = width*dpi/2540 with INTEGER TRUNCATION,
 * so round() can drift down by 1 (e.g. 76px->round 1072->floor 75). CEIL of
 * px*2540/dpi gives the smallest 1/100 mm whose truncation is exactly px. */
static int pt_px_to_centimm(int px, int dpi)
{
    return (px * 2540 + dpi - 1) / dpi;
}

/* Extract the model from an IEEE-1284 device-id ("...;MDL:PT-2730;..."). */
static void parse_mdl(const char *device_id, char *out, size_t n)
{
    out[0] = '\0';
    const char *p = device_id ? strstr(device_id, "MDL:") : NULL;
    if (!p)
        return;
    p += 4;
    size_t i = 0;
    while (p[i] && p[i] != ';' && i < n - 1) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
}

/* Write exactly n bytes through PAPPL's buffered device API; true iff accepted. */
static bool pt_dev_write(pappl_device_t *device, const uint8_t *buf, size_t n)
{
    return papplDeviceWrite(device, buf, n) == (ssize_t)n;
}

/* Start of job: init the printer and select raster transfer mode.
 * flags = 0: PT-2730 is FLAG_NONE. Per-model dispatch (P700 rasterstart variant,
 * packbits) is deferred until there is non-PT-2730 hardware to verify it. */
static bool r_startjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)j; (void)o;
    uint8_t buf[8];
    if (!pt_dev_write(d, buf, pt_cmd_init(buf)))
        return false;
    if (!pt_dev_write(d, buf, pt_cmd_rasterstart(buf, 0)))
        return false;
    papplDeviceFlush(d);
    return true;
}

static bool r_startpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)j; (void)o; (void)p;
#if PT_LEADER_PX > 0
    /* Leading-edge quirk: feed PT_LEADER_PX blank cross-tape lines so leading
     * content is not clipped. Adds to the cut length (Codex #4). Default 0. */
    uint8_t blank[16] = {0};
    uint8_t buf[24];
    for (int i = 0; i < PT_LEADER_PX; i++) {
        if (!pt_dev_write(d, buf, pt_cmd_sendraster(buf, blank, 16)))
            return false;
    }
    papplDeviceFlush(d);
#else
    (void)d;
#endif
    return true;
}

/* One delivered scanline is one cross-tape Brother raster line (M0 Probe B). */
static bool r_writeline(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned y, const unsigned char *l)
{
    (void)y;
    unsigned width = o->header.cupsWidth;
    if (width > 128) {  /* do not silently crop a too-wide page */
        papplLogJob(j, PAPPL_LOGLEVEL_ERROR, "raster width %u exceeds 128-dot head", width);
        return false;
    }
    uint8_t packed[16];
    pt_pack_line(packed, l, (int)width);
    uint8_t buf[24];
    if (!pt_dev_write(d, buf, pt_cmd_sendraster(buf, packed, 16)))
        return false;
    papplDeviceFlush(d);  /* one transfer per line, like upstream; avoids bursts overrunning the printer buffer */
    return true;
}

static bool r_endpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{ (void)j; (void)o; (void)d; (void)p; return true; }

/* End of job: print and cut the label. */
static bool r_endjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)j; (void)o;
    papplDeviceFlush(d);  /* push any buffered raster so eject goes out standalone */
    uint8_t buf[8];
    bool ok = pt_dev_write(d, buf, pt_cmd_eject(buf));
    papplDeviceFlush(d);
    return ok;
}

/* Fill a media-col for a loaded tape width (mm) at a continuous length (1/100 mm).
 * The cross-tape dimension is the tape's imageable px expressed in 1/100 mm
 * (round-trips to exactly pt_tape_px). Returns false for an unknown/too-wide
 * width. size_name is owned by the struct copy, so formatting it here is safe. */
static bool media_col_for(int tape_mm, int length_centimm, int dpi, pappl_media_col_t *out)
{
    memset(out, 0, sizeof(*out));  /* zero even on the false path so callers that ignore the return get a safe col */
    int px = pt_tape_px(tape_mm);
    if (px <= 0 || px > 128)       /* 128 = head width, the actual cross-tape limit */
        return false;
    out->size_width = pt_px_to_centimm(px, dpi);
    out->size_length = length_centimm;
    pwgFormatSizeName(out->size_name, sizeof(out->size_name), NULL, NULL,
                      out->size_width, length_centimm, "mm");
    strncpy(out->source, "main-roll", sizeof(out->source) - 1);
    strncpy(out->type, "labels", sizeof(out->type) - 1);
    out->tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;
    return true;
}

/* Read the loaded tape over USB and publish media-ready + printer reasons.
 * Distinguishes unplugged (OFFLINE) from in-use (NULL open while present). */
static bool status_cb(pappl_printer_t *printer)
{
    char model[64];
    parse_mdl(papplPrinterGetDeviceID(printer), model, sizeof model);
    const pt_dev *dt = pt_lookup_name(model);
    if (!dt)
        dt = pt_lookup_dev(0x04f9, 0x2041);  /* fallback PT-2730 */

    if (!pt_usb_present(dt->vid, dt->pid)) {
        /* unplugged: OFFLINE only; drop any stale MEDIA_EMPTY from a prior poll */
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_MEDIA_EMPTY);
        return true;
    }

    pappl_device_t *dev = papplPrinterOpenDevice(printer);
    if (!dev) {
        /* present but not openable: a job owns it; clear OFFLINE, leave media */
        papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
        return true;
    }

    uint8_t cmd[8];
    size_t cn = pt_cmd_init(cmd);            /* ESC @ : reset parser so the device answers */
    papplDeviceWrite(dev, cmd, cn);
    size_t rn = pt_cmd_status_request(cmd);  /* ESC i S : status info request */
    papplDeviceWrite(dev, cmd, rn);
    papplDeviceFlush(dev);
    /* The device may not have the 32-byte reply ready on the first read; poll a
     * few times with a short delay, mirroring upstream ptouch_getstatus. Each
     * read is itself bounded (2 s) so the loop cannot hang. */
    uint8_t buf[32] = {0};
    ssize_t n = 0;
    for (int tries = 0; n == 0 && tries < 10; tries++) {
        struct timespec w = {0, 100000000};  /* 0.1 s */
        nanosleep(&w, NULL);
        n = pt_usb_read_status(dev, buf, sizeof buf);
    }
    papplPrinterCloseDevice(printer);

    pt_status st;
    bool parsed = (n == 32 && pt_parse_status(buf, &st) == 0);
    if (parsed && st.tape_px > 0) {
        pappl_media_col_t mc;
        if (media_col_for(st.tape_mm, 10000, dt->dpi, &mc) &&
            papplPrinterSetReadyMedia(printer, 1, &mc))
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                   PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY);
        else
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
    } else if (parsed && st.tape_mm == 0) {
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_OFFLINE);
    } else if (n < 0) {
        /* I/O error: device likely gone; OFFLINE, drop any stale MEDIA_EMPTY */
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_MEDIA_EMPTY);
    } else {  /* 0 / short / garbled: transient, keep last-known media */
        papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
    }
    return true;
}

bool pt_driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri,
                  const char *device_id, pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata)
{
    (void)system; (void)driver_name; (void)device_uri; (void)device_id;
    (void)driver_attrs; (void)cbdata;

    data->rstartjob_cb = r_startjob;
    data->rstartpage_cb = r_startpage;
    data->rwriteline_cb = r_writeline;
    data->rendpage_cb = r_endpage;
    data->rendjob_cb = r_endjob;
    data->status_cb = status_cb;

    char model[64];
    parse_mdl(device_id, model, sizeof model);
    const pt_dev *dev = pt_lookup_name(model);
    if (!dev)
        dev = pt_lookup_dev(0x04f9, 0x2041);  /* fallback PT-2730 */
    int dpi = dev->dpi;
    int max_px = dev->max_px;

    char mm[80];
    snprintf(mm, sizeof mm, "Brother %s", dev->name);
    strncpy(data->make_and_model, mm, sizeof(data->make_and_model) - 1);
    data->ppm = 20;
    data->kind = PAPPL_KIND_LABEL;
    data->scaling_default = PAPPL_SCALING_NONE;  /* 1:1, no fit/fill (center/crop) */
    data->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
    data->color_default = PAPPL_COLOR_MODE_BI_LEVEL;
    data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
    data->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;
    data->finishings = PAPPL_FINISHINGS_TRIM;

    data->num_resolution = 1;
    data->x_resolution[0] = data->y_resolution[0] = dpi;
    data->x_default = data->y_default = dpi;
    /* 0/0 margins keep the imageable px exact: a nonzero margin would shrink
     * media->width-2*margin and perturb cupsWidth (Codex #4). */
    data->left_right = 0;
    data->bottom_top = 0;

    data->num_source = 1; data->source[0] = "main-roll";
    data->num_type = 1; data->type[0] = "labels";

    /* Advertise discrete per-width length presets PLUS one continuous roll range.
     * PAPPL collapses the media list to a SINGLE roll min/max box (printer-driver.c
     * keeps only the last roll_min_/roll_max_ seen), so the range must span the
     * full width span (narrowest..widest tape) x [5mm..300mm]; any (width,length)
     * inside the box is then honored. Names live in durable file-static storage
     * (PAPPL keeps the const char* pointers; Codex #3, #6). */
    int n = 0;
    int min_w100 = 0, max_w100 = 0;
    const int per_width = (int)(sizeof(pt_preset_mm) / sizeof(pt_preset_mm[0]));
    for (size_t i = 0; i < sizeof(pt_tape_mm) / sizeof(pt_tape_mm[0]); i++) {
        int px = pt_tape_px(pt_tape_mm[i]);
        if (px <= 0 || px > max_px)
            continue;
        if (n + per_width + 2 > PT_MAX_MEDIA_NAMES || n + per_width + 2 > PAPPL_MAX_MEDIA)
            break;
        int w100 = pt_px_to_centimm(px, dpi);
        if (min_w100 == 0 || w100 < min_w100) min_w100 = w100;
        if (w100 > max_w100) max_w100 = w100;
        for (int k = 0; k < per_width; k++) {
            pwgFormatSizeName(g_media_names[n], PT_MEDIA_NAME_LEN, NULL, NULL,
                              w100, pt_preset_mm[k] * 100, "mm");
            data->media[n] = g_media_names[n]; n++;
        }
    }
    /* One roll range across the full width span and 5..300mm length. */
    if (max_w100 > 0 && n + 2 <= PT_MAX_MEDIA_NAMES) {
        pwgFormatSizeName(g_media_names[n], PT_MEDIA_NAME_LEN, "roll", "min",
                          min_w100, PT_ROLL_MIN_CENTIMM, "mm");
        data->media[n] = g_media_names[n]; n++;
        pwgFormatSizeName(g_media_names[n], PT_MEDIA_NAME_LEN, "roll", "max",
                          max_w100, PT_ROLL_MAX_CENTIMM, "mm");
        data->media[n] = g_media_names[n]; n++;
    }
    data->num_media = n;

    /* Safe fixed default: 12mm x 100mm. status_cb republishes media_ready as the
     * live-loaded width once the tape is read (Codex #2). */
    pappl_media_col_t mc;
    media_col_for(12, 10000, dpi, &mc);
    data->media_default = mc;
    data->media_ready[0] = mc;
    return true;
}
