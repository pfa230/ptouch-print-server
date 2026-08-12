#ifndef PT_DRIVER_H
#define PT_DRIVER_H

#include <pappl/pappl.h>

/* Private raster destination type (#40). Job processing looks for a filter to
 * driver_data.format BEFORE one to image/pwg-raster, so giving the driver its
 * own destination type is what lets pt_filter_png replace PAPPL's built-in PNG
 * filter (re-registering image/png -> image/pwg-raster is silently ignored).
 * It is a filter target only, never a printable format: raw jobs in this type
 * are faulted. */
#define PT_RASTER_FORMAT "image/vnd.ptouch-raster"

/* Roll length range, in 1/100 mm, as advertised in media-col. */
#define PT_ROLL_MIN_CENTIMM 500     /* 5 mm   */
#define PT_ROLL_MAX_CENTIMM 30000   /* 300 mm */

/* Idle finaliser for cross-job chaining (#41). Register as a 1 s system timer:
 * it ejects (and cuts) a run that chained out to a job that never printed. */
bool pt_chain_timer(pappl_system_t *system, void *cb_data);

/* #43: make media-default describe the tape that is actually loaded, before the
 * caller builds the page geometry from it. A no-op unless the printer is on our
 * ptouch:// scheme and its media is suspect (never confirmed by a live read, or
 * the printer is offline/media-empty), so healthy jobs pay nothing. Returns false
 * when suspect media could not be resolved within the barrier's budget: the job
 * has already been faulted and the caller must not print. Call it with the
 * device open and owned by the job. */
bool pt_refresh_ready_media(pappl_job_t *job, pappl_device_t *device);

/* Fill driver_data for a P-touch printer (PAPPL pappl_pr_driver_cb_t). */
bool pt_driver_cb(pappl_system_t *system, const char *driver_name,
                  const char *device_uri, const char *device_id,
                  pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata);

#endif /* PT_DRIVER_H */
