#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pappl/pappl.h>
#include "driver.h"
#include "filter_png.h"
#include "device_usb.h"
#include "protocol.h"
#include "tables.h"

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

static void probe_error_cb(const char *message, void *data)
{
    (void)data;
    fprintf(stderr, "probe: %s\n", message);
}

static int probe_cb(const char *base, int num_options, cups_option_t *options,
                    int num_files, char **files, void *data)
{
    (void)base; (void)num_options; (void)options; (void)num_files; (void)files; (void)data;
    pt_usb_register();
    printf("probing ptouch:// (CUSTOM_LOCAL only)...\n");
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, probe_print_cb, NULL, probe_error_cb, NULL);
    printf("probe done\n");
    printf("PRESENT 04f9:2041 = %s\n", pt_usb_present(0x04f9, 0x2041) ? "yes" : "no");
    return 0;
}

static void discovery_error_cb(const char *message, void *data)
{
    papplLog(data, PAPPL_LOGLEVEL_ERROR, "%s", message);
}

static bool discovery_cb(const char *info, const char *uri, const char *id, void *data)
{
    (void)info; (void)uri; (void)id; (void)data;
    return false;
}

static pappl_system_t *system_cb(int num_options, cups_option_t *options, void *data)
{
    (void)num_options; (void)options; (void)data;
    pappl_system_t *sys = papplSystemCreate(
        PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE,
        "ptouch", 8000, "_print,_universal",
        NULL, "-", PAPPL_LOGLEVEL_INFO, NULL, false);
    if (!sys)
        return NULL;

    /* PAPPL 1.4.11 localizes its optional footer without checking for NULL. */
    papplSystemSetFooterHTML(sys, "");
    papplSystemAddListeners(sys, NULL);
    pt_usb_register();  /* register the scheme BEFORE any list/open */
    papplSystemSetPrinterDrivers(sys, NDRIVERS, g_drivers, NULL /*autoadd*/, NULL, pt_driver_cb, NULL);

    papplSystemAddMIMEFilter(sys, "image/png", "image/png", pt_filter_png, NULL);

    /* Test override: create the printer on an explicit URI (e.g. a socket sink)
     * instead of enumerating ptouch://. Lets the geometry gate run against
     * socket://127.0.0.1:9100 with no PT-2730 involved (no physical printing). */
    const char *override_uri = getenv("PTOUCH_DEVICE_URI");
    if (override_uri && *override_uri) {
        pappl_printer_t *p = papplPrinterCreate(sys, 0, "ptouch", "brother_ptouch",
                                                "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;",
                                                override_uri);
        papplLog(sys, p ? PAPPL_LOGLEVEL_INFO : PAPPL_LOGLEVEL_ERROR,
                 "%s printer for %s (PTOUCH_DEVICE_URI override)",
                 p ? "Created" : "FAILED to create", override_uri);
        return sys;
    }

    /* Always create the configured supported model's stable queue. Discovery
     * supplies diagnostics, never a first-device fallback or a different model. */
    const pt_dev *model = pt_usb_supported_model(getenv("PTOUCH_MODEL"));
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, discovery_cb, NULL, discovery_error_cb, sys);
    ssize_t matches = pt_usb_count(model->vid, model->pid);
    pappl_printer_t *p = papplPrinterCreate(sys, 0, "ptouch", "brother_ptouch",
                                           "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;",
                                           "ptouch://Brother/PT-2730");
    if (p && matches != 1)
        papplPrinterSetReasons(p, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_NONE);
    papplLog(sys, p ? PAPPL_LOGLEVEL_INFO : PAPPL_LOGLEVEL_ERROR,
             "%s printer for ptouch://Brother/PT-2730 (%zd matching USB devices)",
             p ? "Created" : "FAILED to create", matches);
    return sys;
}

static bool smoke_saw_error = false;

static void smoke_err_cb(const char *message, void *data)
{
    (void)data;
    smoke_saw_error = true;
    fprintf(stderr, "smoke: device error: %s\n", message);
}

/* Open the device, write a 2-byte init, flush, and confirm the bytes reached the
 * wire. papplDeviceWrite only BUFFERS (2 bytes < PAPPL's buffer) and returns 2
 * before pt_usb_write runs; the real bulk transfer happens in papplDeviceFlush.
 * Success is judged by the device metric write_bytes (what the scheme write
 * callback actually pushed), not papplDeviceWrite's return. Isolated transport
 * check: no system, printer, or status read. */
static int run_smoke(void)
{
    pt_usb_register();
    const char *uri = "ptouch://Brother/PT-2730";
    const pt_dev *model = pt_usb_supported_model(getenv("PTOUCH_MODEL"));
    ssize_t count = pt_usb_count(model->vid, model->pid);
    if (count != 1) {
        fprintf(stderr, "smoke: expected one PT-2730, found %zd (none or ambiguous)\n", count);
        return 2;
    }
    pappl_device_t *dev = papplDeviceOpen(uri, "smoke", smoke_err_cb, NULL);
    if (!dev) {
        fprintf(stderr, "smoke: open failed for %s\n", uri);
        return 3;
    }
    uint8_t init[8];
    size_t n = pt_cmd_init(init);                 /* 1b 40, n == 2 */
    papplDeviceWrite(dev, init, n);               /* buffered; returns before flush */
    papplDeviceFlush(dev);                         /* the actual bulk OUT happens here */
    pappl_devmetrics_t m;
    papplDeviceGetMetrics(dev, &m);
    papplDeviceClose(dev);
    printf("smoke: device write_bytes=%zu write_requests=%zu to %s\n",
           m.write_bytes, m.write_requests, uri);
    return (!smoke_saw_error && m.write_bytes >= n) ? 0 : 1;  /* 1 = no real write */
}

int main(int argc, char *argv[])
{
    const char *configured = getenv("PTOUCH_MODEL");
    if (!pt_usb_supported_model(configured)) {
        fprintf(stderr, "PTOUCH_MODEL=%s is %s; only PT-2730 is print-supported\n",
                configured, pt_lookup_name(configured) ? "recognized but unsupported" : "unknown");
        return EXIT_FAILURE;
    }
    if (argc >= 2 && strcmp(argv[1], "smoke") == 0)
        return run_smoke();
    return papplMainloop(argc, argv, "1.0", NULL,
                         NDRIVERS, g_drivers, NULL /*autoadd*/, pt_driver_cb,
                         "probe", probe_cb, system_cb, NULL, NULL);
}
