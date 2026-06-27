#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <pappl/pappl.h>
#include "driver.h"
#include "tables.h"

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

/* Raster callbacks are no-op stubs in M2 (M3 fills them). */
static bool r_startjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{ (void)j; (void)o; (void)d; return true; }
static bool r_startpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{ (void)j; (void)o; (void)d; (void)p; return true; }
static bool r_writeline(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned y, const unsigned char *l)
{ (void)j; (void)o; (void)d; (void)y; (void)l; return true; }
static bool r_endpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{ (void)j; (void)o; (void)d; (void)p; return true; }
static bool r_endjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{ (void)j; (void)o; (void)d; return true; }

/* Status callback is a stub in T1; T4 (#13) does the real libusb status read. */
static bool status_cb(pappl_printer_t *printer) { (void)printer; return true; }

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
