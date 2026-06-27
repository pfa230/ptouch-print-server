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

static int probe_cb(const char *base, int num_options, cups_option_t *options,
                    int num_files, char **files, void *data)
{
    (void)base; (void)num_options; (void)options; (void)num_files; (void)files; (void)data;
    pt_usb_register();
    printf("probing ptouch:// (CUSTOM_LOCAL only)...\n");
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, probe_print_cb, NULL, NULL, NULL);
    printf("probe done\n");
    printf("PRESENT 04f9:2041 = %s\n", pt_usb_present(0x04f9, 0x2041) ? "yes" : "no");
    return 0;
}

/* Capture the first ptouch:// device for in-process create. */
typedef struct { char uri[256]; char id[256]; bool found; } pt_found;
static bool grab_cb(const char *info, const char *uri, const char *id, void *data)
{
    (void)info;
    pt_found *f = data;
    strncpy(f->uri, uri, sizeof f->uri - 1);
    strncpy(f->id, id ? id : "", sizeof f->id - 1);
    f->found = true;
    return true;  /* stop at the first */
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

    /* In-process create from OUR scheme only (never the built-in usb:// hang). */
    pt_found found = {0};
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, grab_cb, &found, NULL, NULL);
    if (found.found) {
        pappl_printer_t *p = papplPrinterCreate(sys, 0, "ptouch", "brother_ptouch",
                                                found.id, found.uri);
        papplLog(sys, p ? PAPPL_LOGLEVEL_INFO : PAPPL_LOGLEVEL_ERROR,
                 "%s printer for %s", p ? "Created" : "FAILED to create", found.uri);
    } else {
        papplLog(sys, PAPPL_LOGLEVEL_WARN, "No ptouch device found at startup.");
    }
    return sys;
}

int main(int argc, char *argv[])
{
    return papplMainloop(argc, argv, "1.0", NULL,
                         NDRIVERS, g_drivers, NULL /*autoadd*/, pt_driver_cb,
                         "probe", probe_cb, system_cb, NULL, NULL);
}
