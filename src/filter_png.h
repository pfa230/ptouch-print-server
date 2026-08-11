#ifndef PT_FILTER_PNG_H
#define PT_FILTER_PNG_H

#include <pappl/pappl.h>

/* Document filter for image/png -> PT_RASTER_FORMAT (pappl_mime_filter_cb_t).
 * Same decode as PAPPL's built-in PNG filter, but the page length comes from the
 * image's own physical size instead of the media default (#40). */
bool pt_filter_png(pappl_job_t *job, pappl_device_t *device, void *data);

#endif /* PT_FILTER_PNG_H */
