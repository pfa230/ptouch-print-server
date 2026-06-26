#ifndef PT_DRIVER_H
#define PT_DRIVER_H

#include <pappl/pappl.h>

/* Fill driver_data for a P-touch printer (PAPPL pappl_pr_driver_cb_t). */
bool pt_driver_cb(pappl_system_t *system, const char *driver_name,
                  const char *device_uri, const char *device_id,
                  pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata);

#endif /* PT_DRIVER_H */
