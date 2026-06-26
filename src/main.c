#include <string.h>
#include <stdbool.h>
#include <pappl/pappl.h>
#include "driver.h"
#include "device_usb.h"

static pappl_pr_driver_t g_drivers[] = {
    {"brother_ptouch", "Brother P-touch", NULL, NULL},
};
#define NDRIVERS ((int)(sizeof(g_drivers) / sizeof(g_drivers[0])))

/* No autoadd: we create the one printer in-process (T3) on our ptouch:// scheme.
 * Passing NULL keeps PAPPL from auto-adding its built-in usb:// device, which
 * binds to the hanging transport (M0 Probe H). */

static pappl_system_t *system_cb(int num_options, cups_option_t *options, void *data)
{
    (void)num_options; (void)options; (void)data;
    pappl_system_t *sys = papplSystemCreate(
        PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE,
        "ptouch", 8000, "_print,_universal",
        NULL, "/tmp/ptouch.log", PAPPL_LOGLEVEL_DEBUG, NULL, false);
    if (!sys)
        return NULL;

    papplSystemAddListeners(sys, NULL);
    pt_usb_register();  /* register the scheme BEFORE any list/open */
    papplSystemSetPrinterDrivers(sys, NDRIVERS, g_drivers, NULL /*autoadd*/, NULL, pt_driver_cb, NULL);
    /* T3 (#12): papplPrinterCreate in-process here. */
    return sys;
}

int main(int argc, char *argv[])
{
    return papplMainloop(argc, argv, "1.0", NULL,
                         NDRIVERS, g_drivers, NULL /*autoadd*/, pt_driver_cb,
                         NULL, NULL, system_cb, NULL, NULL);
}
