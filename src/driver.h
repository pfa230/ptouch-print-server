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

/* Fill driver_data for a P-touch printer (PAPPL pappl_pr_driver_cb_t). */
bool pt_driver_cb(pappl_system_t *system, const char *driver_name,
                  const char *device_uri, const char *device_id,
                  pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata);

#endif /* PT_DRIVER_H */
