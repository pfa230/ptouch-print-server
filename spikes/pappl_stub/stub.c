// M0 Probe B/C/F spike stub (THROWAWAY).
// A config-matched PAPPL 1.4.11 printer app that LOGS what the raster pipeline
// delivers and, behind STUB_PRINT=1, crudely prints to discover orientation.
// Not a driver. See spikes/FINDINGS.md.
//
//   STUB_PRINT=1      crude packer: one Brother raster line per scanline
//   STUB_TRANSPOSE=1  buffer the page and emit columns instead (transpose test)

#include <pappl/pappl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define STUB_FORCE_TYPE PAPPL_PWG_RASTER_TYPE_BLACK_1   // flip to _BLACK_8 to test 8-bit
#define MEDIA_NAME      "om_12mm-tape_12x100mm"          // loaded 12mm tape (Probe A)

#define LOG(...) do { fprintf(stderr, "[STUB] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static int            g_print = 0, g_transpose = 0;
static unsigned       g_height = 0, g_bpl = 0, g_width = 0, g_lines = 0;
static unsigned char *g_page = NULL;

static bool rstartjob_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  (void)job; (void)options; (void)device;
  g_lines = 0;
  LOG("rstartjob");
  return true;
}

static bool rstartpage_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
  (void)job;
  cups_page_header2_t *h = &options->header;
  g_height = h->cupsHeight; g_bpl = h->cupsBytesPerLine; g_width = h->cupsWidth; g_lines = 0;
  LOG("rstartpage page=%u  W=%u H=%u  bpp=%u bpc=%u  bpl=%u  colorspace=%u  res=%ux%u",
      page, h->cupsWidth, h->cupsHeight, h->cupsBitsPerPixel, h->cupsBitsPerColor,
      h->cupsBytesPerLine, h->cupsColorSpace, h->HWResolution[0], h->HWResolution[1]);
  LOG("  job: num_pages=%u copies=%d finishings=0x%04x orient=%d  media=%s %dx%d(1/100mm)",
      options->num_pages, options->copies, (unsigned)options->finishings,
      options->orientation_requested, options->media.size_name,
      options->media.size_width, options->media.size_length);

  if (g_print) {
    papplDeviceWrite(device, "\x1b\x40", 2);      // init
    papplDeviceWrite(device, "\x1b\x69\x52\x01", 4); // raster mode
    if (g_transpose) { free(g_page); g_page = calloc(g_height ? g_height : 1, g_bpl ? g_bpl : 1); }
  }
  return true;
}

static bool rwriteline_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned y, const unsigned char *line)
{
  (void)job; (void)options;
  g_lines++;
  if (y == 0 || (g_height && y == g_height / 2) || (g_height && y == g_height - 1)) {
    char hex[64] = ""; size_t n = g_bpl < 12 ? g_bpl : 12;
    for (size_t i = 0; i < n; i++) snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02x ", line[i]);
    LOG("  rwriteline y=%u  bpl=%u  bytes: %s", y, g_bpl, hex);
  }
  if (g_print) {
    if (g_transpose && g_page) {
      memcpy(g_page + (size_t)y * g_bpl, line, g_bpl);
    } else if (!g_transpose) {
      unsigned char rl[16] = {0};
      size_t n = g_bpl < 16 ? g_bpl : 16;     // crude: take first 128 bits of the scanline
      memcpy(rl, line, n);
      papplDeviceWrite(device, "\x47\x10\x00", 3);
      papplDeviceWrite(device, rl, 16);
    }
  }
  return true;
}

static bool rendpage_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
  (void)job; (void)options;
  LOG("rendpage page=%u  total_lines=%u", page, g_lines);
  if (g_print) {
    if (g_transpose && g_page) {                // emit image columns as Brother raster lines
      unsigned cols = g_bpl * 8;
      for (unsigned c = 0; c < cols && c < g_width; c++) {
        unsigned char rl[16] = {0};
        for (unsigned r = 0; r < g_height && r < 128; r++) {
          unsigned char b = g_page[(size_t)r * g_bpl + (c >> 3)];
          if ((b >> (7 - (c & 7))) & 1) rl[r >> 3] |= (unsigned char)(1 << (7 - (r & 7)));
        }
        papplDeviceWrite(device, "\x47\x10\x00", 3);
        papplDeviceWrite(device, rl, 16);
      }
      free(g_page); g_page = NULL;
    }
    papplDeviceWrite(device, "\x0c", 1);        // form feed
  }
  return true;
}

static bool rendjob_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  (void)job; (void)options; (void)device;
  LOG("rendjob");
  return true;
}

static bool status_cb(pappl_printer_t *printer)
{
  pappl_device_t *dev = papplPrinterOpenDevice(printer);
  if (!dev) { LOG("status: OpenDevice returned NULL (device busy?)"); return false; }
  papplDeviceWrite(dev, "\x1b\x69\x53", 3);     // ESC i S = status request
  unsigned char buf[32] = {0};
  ssize_t n = papplDeviceRead(dev, buf, sizeof(buf));
  LOG("status: read %zd bytes  byte10(tape mm)=%u  err=%02x%02x",
      n, (n > 10 ? buf[10] : 0), (n > 9 ? buf[8] : 0), (n > 9 ? buf[9] : 0));
  papplPrinterCloseDevice(printer);
  return true;
}

static const char *autoadd_cb(const char *device_info, const char *device_uri, const char *device_id, void *data)
{
  (void)data;
  if ((device_id && (strstr(device_id, "PT-2730") || strstr(device_id, "Brother"))) ||
      (device_info && strstr(device_info, "PT-2730")) ||
      (device_uri && strstr(device_uri, "Brother")))
    return "brother_ptouch_spike";
  return NULL;
}

static bool driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri,
                      const char *device_id, pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata)
{
  (void)system; (void)driver_name; (void)device_uri; (void)device_id; (void)driver_attrs; (void)cbdata;

  data->rstartjob_cb  = rstartjob_cb;
  data->rstartpage_cb = rstartpage_cb;
  data->rwriteline_cb = rwriteline_cb;
  data->rendpage_cb   = rendpage_cb;
  data->rendjob_cb    = rendjob_cb;
  data->status_cb     = status_cb;

  strncpy(data->make_and_model, "Brother PT-2730 (spike)", sizeof(data->make_and_model) - 1);
  data->ppm             = 20;
  data->kind            = PAPPL_KIND_LABEL;
  data->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
  data->color_default   = PAPPL_COLOR_MODE_MONOCHROME;
  data->raster_types    = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
  data->force_raster_type = STUB_FORCE_TYPE;
  data->finishings      = PAPPL_FINISHINGS_TRIM;

  data->num_resolution = 1;
  data->x_resolution[0] = data->y_resolution[0] = 180;
  data->x_default = data->y_default = 180;
  data->left_right = 0; data->bottom_top = 0;

  pappl_media_col_t mc;
  memset(&mc, 0, sizeof(mc));
  mc.size_width = 1200; mc.size_length = 10000;   // 12mm x 100mm in 1/100mm
  strncpy(mc.size_name, MEDIA_NAME, sizeof(mc.size_name) - 1);
  strncpy(mc.source, "main-roll", sizeof(mc.source) - 1);
  strncpy(mc.type, "labels", sizeof(mc.type) - 1);
  mc.tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;

  data->num_media = 1;
  data->media[0]  = MEDIA_NAME;
  data->num_source = 1;
  data->source[0] = "main-roll";
  data->num_type = 1;
  data->type[0] = "labels";
  data->media_default = mc;
  data->media_ready[0] = mc;

  return true;
}

static pappl_pr_driver_t g_drivers[] = {
  { "brother_ptouch_spike", "Brother P-touch (spike)", NULL, NULL }
};
#define NDRIVERS ((int)(sizeof(g_drivers) / sizeof(g_drivers[0])))

// Build the system in-process and create the printer directly (no `add` CLI,
// no USB auto-discovery). Device is the socket sink so the rasterizer runs
// without the (hanging) USB enumeration.
static pappl_system_t *system_cb(int num_options, cups_option_t *options, void *data)
{
  (void)num_options; (void)options; (void)data;
  pappl_system_t *system = papplSystemCreate(
      PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE,
      "ptouch-spike", 8000, "_print,_universal",
      NULL,            // spooldir (temp)
      "/tmp/s.log",    // logfile
      PAPPL_LOGLEVEL_DEBUG,
      NULL,            // auth service
      false);          // tls_only
  if (!system) { LOG("papplSystemCreate failed"); return NULL; }

  papplSystemAddListeners(system, NULL);
  papplSystemSetPrinterDrivers(system, NDRIVERS, g_drivers, autoadd_cb, NULL, driver_cb, NULL);

  if (!papplPrinterCreate(system, 0, "spike", "brother_ptouch_spike", NULL, "socket://127.0.0.1:9100"))
    LOG("papplPrinterCreate('spike') failed");
  else
    LOG("created printer 'spike' on socket://127.0.0.1:9100");

  return system;
}

int main(int argc, char *argv[])
{
  g_print     = getenv("STUB_PRINT") != NULL;
  g_transpose = getenv("STUB_TRANSPOSE") != NULL;
  LOG("starting (STUB_PRINT=%d STUB_TRANSPOSE=%d force_raster=0x%04x)",
      g_print, g_transpose, (unsigned)STUB_FORCE_TYPE);

  return papplMainloop(argc, argv, "1.0", NULL,
                       NDRIVERS, g_drivers,
                       autoadd_cb, driver_cb, NULL, NULL, system_cb, NULL, NULL);
}
