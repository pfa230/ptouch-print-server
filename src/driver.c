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

/* Candidate tape widths → PWG self-describing media names (W x 100mm continuous). */
static const struct { int mm; const char *name; } pt_media[] = {
    { 6,  "om_pt-6mm_6x100mm"  },
    { 9,  "om_pt-9mm_9x100mm"  },
    { 12, "om_pt-12mm_12x100mm" },
    { 18, "om_pt-18mm_18x100mm" },
    { 24, "om_pt-24mm_24x100mm" },
    { 36, "om_pt-36mm_36x100mm" },
};

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
{ (void)j; (void)o; (void)d; (void)p; return true; }

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

/* Fill a media-col for a loaded tape width (mm). Returns false if the width
 * is not in the advertised table (caller should not publish it). */
static bool media_col_for(int tape_mm, pappl_media_col_t *out)
{
    const char *name = NULL;
    for (size_t i = 0; i < sizeof(pt_media) / sizeof(pt_media[0]); i++) {
        if (pt_media[i].mm == tape_mm) { name = pt_media[i].name; break; }
    }
    if (!name)
        return false;
    memset(out, 0, sizeof(*out));
    out->size_width = tape_mm * 100;   /* mm -> 1/100 mm */
    out->size_length = 10000;          /* 100 mm continuous */
    strncpy(out->size_name, name, sizeof(out->size_name) - 1);
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
        if (media_col_for(st.tape_mm, &mc) &&
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
    data->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
    data->color_default = PAPPL_COLOR_MODE_BI_LEVEL;
    data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
    data->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;
    data->finishings = PAPPL_FINISHINGS_TRIM;

    data->num_resolution = 1;
    data->x_resolution[0] = data->y_resolution[0] = dpi;
    data->x_default = data->y_default = dpi;
    data->left_right = 0;  /* nominal; exact per-width geometry is M3 (head-limiting) */
    data->bottom_top = 0;

    data->num_source = 1; data->source[0] = "main-roll";
    data->num_type = 1; data->type[0] = "labels";

    /* Advertise every supported tape width (pt_tape_px>0 and <= head width). */
    int n = 0;
    for (size_t i = 0; i < sizeof(pt_media) / sizeof(pt_media[0]); i++) {
        int px = pt_tape_px(pt_media[i].mm);
        if (px <= 0 || px > max_px)
            continue;
        data->media[n++] = pt_media[i].name;
    }
    data->num_media = n;

    /* media_default / media_ready = the loaded 12mm tape (T4 makes ready live). */
    pappl_media_col_t mc;
    memset(&mc, 0, sizeof(mc));
    mc.size_width = 1200; mc.size_length = 10000;  /* 12mm x 100mm in 1/100mm */
    strncpy(mc.size_name, "om_pt-12mm_12x100mm", sizeof(mc.size_name) - 1);
    strncpy(mc.source, "main-roll", sizeof(mc.source) - 1);
    strncpy(mc.type, "labels", sizeof(mc.type) - 1);
    mc.tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;
    data->media_default = mc;
    data->media_ready[0] = mc;
    return true;
}
