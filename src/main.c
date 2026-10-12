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

    /* #40: take over image/png so the label length follows the image. Must run
     * before papplSystemRun, and must target our own PT_RASTER_FORMAT: PAPPL
     * already has image/png -> image/pwg-raster and matches filters on src+dst
     * only, so re-registering that pair would be silently dropped. */
    papplSystemAddMIMEFilter(sys, "image/png", PT_RASTER_FORMAT, pt_filter_png, NULL);

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

    /* In-process create from OUR scheme only (never the built-in usb:// hang). */
    pt_found found = {0};
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, grab_cb, &found, NULL, NULL);
    if (found.found) {
        pappl_printer_t *p = papplPrinterCreate(sys, 0, "ptouch", "brother_ptouch",
                                                found.id, found.uri);
        papplLog(sys, p ? PAPPL_LOGLEVEL_INFO : PAPPL_LOGLEVEL_ERROR,
                 "%s printer for %s", p ? "Created" : "FAILED to create", found.uri);
    } else {
        /* No device attached: still create the printer so clients see a real
         * queue reporting `offline` instead of client-error-not-found, and so a
         * power-cycle recovers without restarting (status_cb clears OFFLINE on
         * the next attribute query). #38 */
        const char *model = getenv("PTOUCH_MODEL");
        if (!model || !*model)
            model = "PT-2730";
        if (!pt_lookup_name(model)) {
            papplLog(sys, PAPPL_LOGLEVEL_WARN,
                     "PTOUCH_MODEL=%s is not a known model; using PT-2730", model);
            model = "PT-2730";
        }
        char uri[256], id[256];
        snprintf(uri, sizeof uri, "ptouch://Brother/%s", model);
        snprintf(id, sizeof id, "MFG:Brother;MDL:%s;CMD:PT-CBP;", model);
        pappl_printer_t *p = papplPrinterCreate(sys, 0, "ptouch", "brother_ptouch", id, uri);
        if (p)  /* mark offline NOW: without it reasons are `none`, the live-read gate in
                 * r_startjob never fires, and the width guard silently uses the default 12mm media */
            papplPrinterSetReasons(p, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_MEDIA_EMPTY);
        papplLog(sys, p ? PAPPL_LOGLEVEL_INFO : PAPPL_LOGLEVEL_ERROR,
                 "%s offline printer for %s (no device at startup; awaiting connection)",
                 p ? "Created" : "FAILED to create", uri);
    }
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
    pt_found found = {0};
    papplDeviceList(PAPPL_DEVTYPE_CUSTOM_LOCAL, grab_cb, &found, smoke_err_cb, NULL);
    if (!found.found) {
        fprintf(stderr, "smoke: no ptouch device found\n");
        return 2;
    }
    pappl_device_t *dev = papplDeviceOpen(found.uri, "smoke", smoke_err_cb, NULL);
    if (!dev) {
        fprintf(stderr, "smoke: open failed for %s\n", found.uri);
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
           m.write_bytes, m.write_requests, found.uri);
    return (!smoke_saw_error && m.write_bytes >= n) ? 0 : 1;  /* 1 = no real write */
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "smoke") == 0)
        return run_smoke();
    return papplMainloop(argc, argv, "1.0", NULL,
                         NDRIVERS, g_drivers, NULL /*autoadd*/, pt_driver_cb,
                         "probe", probe_cb, system_cb, NULL, NULL);
}
