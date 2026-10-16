#ifndef PT_DRIVER_H
#define PT_DRIVER_H

#include <pappl/pappl.h>
#include "tables.h"

/* Confirm clean live tape before geometry is constructed, using a bounded
 * status query on the job-owned USB handle. Non-USB test transports retain
 * configured media. Uncertain output blocks every transport until recovery. */
bool pt_refresh_ready_media(pappl_job_t *job, pappl_device_t *device);

/* Selected model for a printer, so geometry and preflight share one definition. */
const pt_dev *pt_printer_model(pappl_printer_t *printer);

/* Fill driver_data for a P-touch printer (PAPPL pappl_pr_driver_cb_t). */
bool pt_driver_cb(pappl_system_t *system, const char *driver_name,
                  const char *device_uri, const char *device_id,
                  pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata);

#endif /* PT_DRIVER_H */
