#include <string.h>
#include <stdbool.h>
#include <pappl/pappl.h>
#include "driver.h"
#include "tables.h"

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

    strncpy(data->make_and_model, "Brother PT-2730", sizeof(data->make_and_model) - 1);
    data->ppm = 20;
    data->kind = PAPPL_KIND_LABEL;
    data->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
    data->color_default = PAPPL_COLOR_MODE_BI_LEVEL;
    data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
    data->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;
    data->finishings = PAPPL_FINISHINGS_TRIM;

    data->num_resolution = 1;
    data->x_resolution[0] = data->y_resolution[0] = 180;
    data->x_default = data->y_default = 180;
    data->left_right = 0;
    data->bottom_top = 0;

    /* T3 (#12): set media-col margins so imageable width = pt_tape_px (76 for 12mm). */
    pappl_media_col_t mc;
    memset(&mc, 0, sizeof(mc));
    mc.size_width = 1200; mc.size_length = 10000;  /* 12mm x 100mm in 1/100mm */
    strncpy(mc.size_name, "om_12mm-tape_12x100mm", sizeof(mc.size_name) - 1);
    strncpy(mc.source, "main-roll", sizeof(mc.source) - 1);
    strncpy(mc.type, "labels", sizeof(mc.type) - 1);
    mc.tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;

    data->num_media = 1; data->media[0] = "om_12mm-tape_12x100mm";
    data->num_source = 1; data->source[0] = "main-roll";
    data->num_type = 1; data->type[0] = "labels";
    data->media_default = mc;
    data->media_ready[0] = mc;
    return true;
}
