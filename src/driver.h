#ifndef PT_DRIVER_H
#define PT_DRIVER_H

#include <pappl/pappl.h>

/* Roll length range, in 1/100 mm, as advertised in media-col. */
#define PT_ROLL_MIN_CENTIMM 500     /* 5 mm   */
#define PT_ROLL_MAX_CENTIMM 30000   /* 300 mm */

/* Confirm clean live tape before geometry is constructed, using a bounded
 * status query on the job-owned USB handle. Non-USB test transports retain
 * configured media. Uncertain output blocks every transport until recovery. */
bool pt_refresh_ready_media(pappl_job_t *job, pappl_device_t *device);

/* Find the supported tape whose geometry matches the page and replace margins
 * the job's media-col did not submit with that tape's published margins.
 * Returns the tape width in mm, or 0 when no supported tape matches. */
int pt_resolve_page_margins(pappl_job_t *job, pappl_pr_options_t *options);

/* Fill driver_data for a P-touch printer (PAPPL pappl_pr_driver_cb_t). */
bool pt_driver_cb(pappl_system_t *system, const char *driver_name,
                  const char *device_uri, const char *device_id,
                  pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata);

#endif /* PT_DRIVER_H */
