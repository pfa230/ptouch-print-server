#ifndef PT_FILTER_PNG_H
#define PT_FILTER_PNG_H

#include <pappl/pappl.h>

/* Standard image/png -> image/png route with explicit-media precedence and
 * source-content preflight. PAPPL supplies placement and raster callbacks. */
bool pt_filter_png(pappl_job_t *job, pappl_device_t *device, void *data);
bool pt_png_printfile(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);

#endif /* PT_FILTER_PNG_H */
