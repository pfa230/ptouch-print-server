#include <string.h>
#include <stdbool.h>
#include <stdio.h>
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

/* `probe` subcommand: list ONLY our custom scheme (never PAPPL's built-in usb://,
 * whose device-ID probe is the M0 hang). Safe regression check for the scheme. */
static bool probe_print_cb(const char *info, const char *uri, const char *id, void *data)
{
    (void)id; (void)data;
    printf("DEVICE: %s | %s\n", uri, info);
    return false;  /* continue listing */
}

static int probe_cb(const char *base, int num_options, cups_option_t *options, void *data)
{
    (void)base; (void)num_options; (void)options; (void)data;
    pt_usb_register();
    printf("probing ptouch:// (CUSTOM_LOCAL only)...\n");
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, probe_print_cb, NULL, NULL, NULL);
    printf("probe done\n");
    return 0;
}

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
                         "probe", probe_cb, system_cb, NULL, NULL);
}
