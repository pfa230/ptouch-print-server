#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <pappl/pappl.h>
#include "driver.h"
#include "filter_png.h"
#include "tables.h"
#include "protocol.h"
#include "raster.h"
#include "status.h"
#include "device_usb.h"
#include "cutter.h"
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <cups/cups.h>

/* Tape widths (mm) we may advertise; 36mm (192px) is dropped at registration
 * because it exceeds the 128-dot head (pt_tape_px>0 && <=max_px filter). */
static const int pt_tape_mm[] = { 6, 9, 12, 18, 24, 36 };

/* Discrete preset lengths, in mm (the roll range itself lives in driver.h). */
static const int pt_preset_mm[] = { 25, 50, 100 };

/* Durable storage for advertised media-size names: PAPPL keeps the const char*
 * pointers in data->media[], so the strings must outlive pt_driver_cb (a stack
 * buffer would dangle). 5 names per width (roll min/max + 3 presets). */
#define PT_MEDIA_NAME_LEN 64
#define PT_MAX_MEDIA_NAMES 64
static char g_media_names[PT_MAX_MEDIA_NAMES][PT_MEDIA_NAME_LEN];

/* Extract the model from an IEEE-1284 device-id ("...;MDL:PT-2730;..."). */
static void parse_mdl(const char *device_id, char *out, size_t n)
{
    out[0] = '\0';
    const char *p = device_id ? strstr(device_id, "MDL:") : NULL;
    if (!p)
        return;
    p += 4;
    size_t i = 0;
    while (p[i] && p[i] != ';' && i < n - 1) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
}

/* Confirm transfer, not merely acceptance into PAPPL's output buffer. */
static bool pt_dev_write(pappl_device_t *device, const uint8_t *buf, size_t n)
{
    pappl_devmetrics_t before, after;
    papplDeviceGetMetrics(device, &before);
    ssize_t accepted = papplDeviceWrite(device, buf, n);
    papplDeviceFlush(device);
    papplDeviceGetMetrics(device, &after);
    return accepted == (ssize_t)n && after.write_bytes - before.write_bytes == n;
}

/* The device may not have the 32-byte status reply ready on the first read, so
 * poll a few times with a short delay, mirroring upstream ptouch_getstatus. Each
 * read is itself bounded (300 ms) so the loop cannot hang: ~4 s worst case. */
#define PT_STATUS_POLL_TRIES 10
#define PT_STATUS_POLL_NS    100000000L   /* 0.1 s */

/* One live status read. `st` is meaningful only when `parsed`; `n` is the raw
 * read result, which separates an I/O error (< 0) from no answer at all (0). */
typedef struct { ssize_t n; bool parsed; pt_status st; } pt_live_read;

/* Read the live tape from an already-open device: ESC i S, then poll for
 * the reply. The caller owns the device and interprets the result; a supported
 * tape is `parsed && st.tape_px > 0`. Validate transport before any command or
 * connection-data access; a socket sink has no USB status path. */
static pt_live_read pt_read_live_tape(pappl_device_t *device, const char *uri)
{
    uint8_t cmd[8];
    pt_live_read r = {0};
    if (!pt_usb_is_uri(uri)) {
        r.n = -1;
        return r;
    }
    if (!pt_dev_write(device, cmd, pt_cmd_status_request(cmd))) {
        r.n = -1;
        return r;
    }

    uint8_t buf[32] = {0};
    size_t used = 0;
    for (int tries = 0; tries < PT_STATUS_POLL_TRIES; tries++) {
        struct timespec w = {0, PT_STATUS_POLL_NS};
        nanosleep(&w, NULL);
        ssize_t n = pt_usb_read_status(device, uri, buf + used, sizeof buf - used);
        if (n < 0) { r.n = n; break; }
        used += (size_t)n;
        r.n = (ssize_t)used;
        if (used < sizeof buf) continue;
        /* Earlier page notifications may precede our requested snapshot.
         * Consume them so they cannot satisfy the next page's readiness wait. */
        if (buf[18] == 0) {
            r.parsed = pt_parse_status(buf, &r.st) == 0;
            return r;
        }
        used = 0;
        r.n = 0;
    }
    return r;
}

/* One table supplies public enum/template aliases and the resolved policy. */
typedef struct { int value; const char *name; pt_cut_mode mode; } pt_finishing;
static const pt_finishing pt_finishings[] = {
    {3, NULL, PT_CUT_NONE},
    {11, "trim", PT_CUT_EACH},
    {60, "trim-after-pages", PT_CUT_EACH},
    {63, "trim-after-job", PT_CUT_END},
    {0, "ptouch-chain", PT_CUT_CHAIN},
};
#define PT_NUM_FINISHINGS (sizeof pt_finishings / sizeof pt_finishings[0])

typedef struct {
    pt_cut_policy policy;
    uint8_t tape_mm;
    bool first_label, page_active, pending, failed;
} pt_batch;

static bool pt_finishing_value(int value, pt_cut_mode *mode)
{
    for (size_t i = 0; i < PT_NUM_FINISHINGS; i++)
        if (pt_finishings[i].value && pt_finishings[i].value == value) {
            *mode = pt_finishings[i].mode;
            return true;
        }
    return false;
}

/* Read the original standard attributes: PAPPL's legacy mask loses 60/63 and
 * the chain template. Omission follows the current synchronized printer default. */
static bool pt_job_policy(pappl_job_t *job, pt_cut_policy *policy)
{
    ipp_attribute_t *values = papplJobGetAttribute(job, "finishings");
    ipp_attribute_t *collection = papplJobGetAttribute(job, "finishings-col");
    pt_cut_mode mode = PT_CUT_NONE;
    if (values && collection) return false;
    if (collection) {
        if (ippGetValueTag(collection) == IPP_TAG_NOVALUE) {
            *policy = pt_cut_policy_for(PT_CUT_NONE);
            return true;
        }
        if (ippGetValueTag(collection) != IPP_TAG_BEGIN_COLLECTION || ippGetCount(collection) != 1)
            return false;
        ipp_t *col = ippGetCollection(collection, 0);
        ipp_attribute_t *template = ippFindAttribute(col, "finishing-template", IPP_TAG_KEYWORD);
        const char *name = template ? ippGetString(template, 0, NULL) : NULL;
        if (!name || ippGetCount(template) != 1) return false;
        for (size_t i = 0; i < PT_NUM_FINISHINGS; i++)
            if (pt_finishings[i].name && !strcmp(pt_finishings[i].name, name)) {
                *policy = pt_cut_policy_for(pt_finishings[i].mode);
                return true;
            }
        return false;
    }
    if (values) {
        if (ippGetValueTag(values) != IPP_TAG_ENUM || ippGetCount(values) < 1) return false;
        for (int i = 0; i < ippGetCount(values); i++) {
            pt_cut_mode selected;
            if (!pt_finishing_value(ippGetInteger(values, i), &selected)) return false;
            /* FIN: none is ignored alongside other finishings; 60 already
             * includes the final cut, so adding 63 does not cut twice. */
            if (selected == PT_CUT_EACH || mode == PT_CUT_NONE) mode = selected;
        }
    } else {
        ipp_t *attrs = papplPrinterGetDriverAttributes(papplJobGetPrinter(job));
        ipp_attribute_t *def = attrs ? ippFindAttribute(attrs, "finishings-default", IPP_TAG_ENUM) : NULL;
        bool valid = def && ippGetCount(def) == 1 && pt_finishing_value(ippGetInteger(def, 0), &mode);
        ippDelete(attrs);
        if (!valid) return false;
    }
    *policy = pt_cut_policy_for(mode);
    return true;
}

static void pt_finishing_attrs(ipp_t *attrs)
{
    int values[PT_NUM_FINISHINGS];
    const char *templates[PT_NUM_FINISHINGS];
    ipp_t *collections[PT_NUM_FINISHINGS];
    size_t nvalues = 0, ntemplates = 0;
    for (size_t i = 0; i < PT_NUM_FINISHINGS; i++) {
        if (pt_finishings[i].value) values[nvalues++] = pt_finishings[i].value;
        if (!pt_finishings[i].name) continue;
        templates[ntemplates] = pt_finishings[i].name;
        collections[ntemplates] = ippNew();
        ippAddString(collections[ntemplates], IPP_TAG_ZERO, IPP_TAG_KEYWORD,
                     "finishing-template", NULL, pt_finishings[i].name);
        ntemplates++;
    }
    ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-supported", (int)nvalues, values);
    ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-ready", (int)nvalues, values);
    ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-default", pt_finishings[1].value);
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishings-col-supported", NULL, "finishing-template");
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template-supported", (int)ntemplates, NULL, templates);
    ippAddCollections(attrs, IPP_TAG_PRINTER, "finishings-col-database", (int)ntemplates, (const ipp_t **)collections);
    ippAddCollections(attrs, IPP_TAG_PRINTER, "finishings-col-ready", (int)ntemplates, (const ipp_t **)collections);
    ippAddCollection(attrs, IPP_TAG_PRINTER, "finishings-col-default", collections[0]);
    for (size_t i = 0; i < ntemplates; i++) ippDelete(collections[i]);
}

/* Defined below with status_cb; forward-declared for the #38 live read in r_startjob. */
static bool media_col_for(int tape_mm, int length_centimm, int dpi, pappl_media_col_t *out);

/* The queue owns its state; PAPPL serializes jobs, while status may run on
 * another thread. Never call PAPPL APIs with this mutex held. */
typedef enum { PT_RUN_CLOSED, PT_RUN_OPEN, PT_RUN_UNCERTAIN } pt_run;
typedef struct {
    pthread_mutex_t lock;
    const pt_dev *model;
    pt_run run;
    bool media_confirmed;
} pt_printer_state;

static pt_printer_state *pt_state(pappl_printer_t *printer)
{
    pappl_pr_driver_data_t data;
    papplPrinterGetDriverData(printer, &data);
    return data.extension;
}

static pt_run pt_run_get(pappl_printer_t *printer)
{
    pt_printer_state *state = pt_state(printer);
    if (!state) return PT_RUN_UNCERTAIN;
    pthread_mutex_lock(&state->lock);
    pt_run run = state->run;
    pthread_mutex_unlock(&state->lock);
    return run;
}

static void pt_run_set(pappl_printer_t *printer, pt_run run)
{
    pt_printer_state *state = pt_state(printer);
    if (!state) return;
    pthread_mutex_lock(&state->lock);
    state->run = run;
    pthread_mutex_unlock(&state->lock);
}

static bool pt_chain_get(pappl_printer_t *printer)
{
    return pt_run_get(printer) == PT_RUN_OPEN;
}

/* Cassette/offline observations may invalidate a healthy run, but cannot prove
 * that an interrupted command buffer is safe to reuse. */
static void pt_chain_set(pappl_printer_t *printer, bool open)
{
    pt_printer_state *state = pt_state(printer);
    if (!state) return;
    pthread_mutex_lock(&state->lock);
    if (state->run != PT_RUN_UNCERTAIN)
        state->run = open ? PT_RUN_OPEN : PT_RUN_CLOSED;
    pthread_mutex_unlock(&state->lock);
}

static void pt_set_media_confirmed(pappl_printer_t *printer, bool confirmed)
{
    pt_printer_state *state = pt_state(printer);
    if (!state) return;
    pthread_mutex_lock(&state->lock);
    state->media_confirmed = confirmed;
    pthread_mutex_unlock(&state->lock);
}

static void pt_delete_cb(pappl_printer_t *printer, pappl_pr_driver_data_t *data)
{
    (void)printer;
    pt_printer_state *state = data->extension;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
        data->extension = NULL;
    }
}

static bool pt_recovery_required(pappl_printer_t *printer)
{
    if (pt_run_get(printer) != PT_RUN_UNCERTAIN) return false;
    papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_NONE);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR,
                    "Output uncertain: power-cycle the printer, then restart the server before resubmitting");
    return true;
}

/* Resolve policy and confirm tape; setup waits for the first valid label. */
static bool r_startjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)o;
    pappl_printer_t *printer = papplJobGetPrinter(j);
    pt_cut_policy policy;
    if (papplJobIsCanceled(j)) return false;
    if (!pt_job_policy(j, &policy)) {
        papplJobSetMessage(j, "Unsupported or conflicting finishing selection");
        return false;
    }
    const char *format = papplJobGetFormat(j);
    if (format && (!strcmp(format, "image/pwg-raster") || !strcmp(format, "image/urf")) &&
        papplJobGetCopies(j) != 1) {
        papplJobSetMessage(j, "Raster batches require copies=1; include each label in the document");
        return false;
    }
    if (pt_recovery_required(printer)) {
        papplJobSetMessage(j, "Output uncertain: power-cycle printer, then restart server");
        return false;
    }
    if (!pt_refresh_ready_media(j, d)) return false;
    pappl_pr_driver_data_t dd;
    papplPrinterGetDriverData(printer, &dd);
    pt_batch *batch = calloc(1, sizeof *batch);
    if (!batch) return false;
    batch->policy = policy;
    batch->tape_mm = (uint8_t)(dd.media_ready[0].size_width / 100);
    batch->first_label = true;
    papplJobSetData(j, batch);
    return true;
}

static bool pt_wait_ready(pappl_job_t *j, pappl_device_t *d);

/* Consume the one complete pending label only when the next valid label or
 * normal job completion proves whether it is intermediate or final. */
static bool pt_print_pending(pappl_job_t *job, pappl_device_t *device, bool final)
{
    pt_batch *batch = papplJobGetData(job);
    uint8_t command[8];
    bool cut = final && batch->policy.cut_at_end;
    if (papplJobIsCanceled(job)) return false;
    if (cut && !pt_dev_write(device, command, pt_cmd_setadvanced(command, 8))) return false;
    if (papplJobIsCanceled(job)) return false;
    if (!pt_dev_write(device, command, cut ? pt_cmd_eject(command) : pt_cmd_ff(command)) ||
        !pt_wait_ready(job, device)) return false;
    batch->pending = false;
    pt_run_set(papplJobGetPrinter(job), cut ? PT_RUN_CLOSED : PT_RUN_OPEN);
    return true;
}

static bool pt_fail_job(pappl_job_t *job)
{
    pt_batch *batch = papplJobGetData(job);
    if (batch) batch->failed = true;
    return false;
}

/* CUPS custom sizes can quantize a tape width by one 1/100 mm unit. */
static bool pt_page_width_matches(const pappl_pr_options_t *o, const pt_geometry *g, int tape_mm)
{
    if (o->header.cupsWidth == (unsigned)g->nominal_px) return true;
    int width = o->media.size_width;
    return width >= tape_mm * 100 - 1 && width <= tape_mm * 100 + 1 &&
           o->header.cupsWidth == (unsigned)(width * 180 / 2540) &&
           o->header.cupsWidth >= (unsigned)g->right_px;
}

static bool r_startpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)p;
    pt_batch *b = papplJobGetData(j);
    if (!b || b->failed || b->page_active || papplJobIsCanceled(j)) return pt_fail_job(j);
    pt_geometry g;
    if (!pt_tape_geometry(b->tape_mm, &g) || o->header.HWResolution[0] != 180 ||
        o->header.HWResolution[1] != 180 || o->header.cupsBitsPerPixel != 1 ||
        !pt_page_width_matches(o, &g, b->tape_mm) ||
        o->header.cupsHeight < PT_ROLL_MIN_CENTIMM * 180 / 2540 ||
        o->header.cupsHeight > PT_ROLL_MAX_CENTIMM * 180 / 2540) {
        papplJobSetReasons(j, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(j, "Page geometry does not match loaded %d mm tape at 180 dpi", b->tape_mm);
        return pt_fail_job(j);
    }
    int left = o->media.left_margin * 180 / 2540;
    int span = (int)o->header.cupsWidth - (o->media.left_margin + o->media.right_margin) * 180 / 2540;
    if (left < g.left_px || span <= 0 || left + span > g.right_px) {
        papplJobSetMessage(j, "Selected margins extend beyond the physical printable window");
        return pt_fail_job(j);
    }
    pappl_printer_t *printer = papplJobGetPrinter(j);
    /* Page setup is sent only after validating the actual page, never a stale
     * default header. Keep a healthy preceding run if validation rejected it. */
    pt_run run = pt_run_get(printer);
    if (run == PT_RUN_UNCERTAIN && b->first_label) return pt_fail_job(j);
    if (b->pending && !pt_print_pending(j, d, false)) return pt_fail_job(j);
    bool continuation = pt_chain_get(printer);
    pt_run_set(printer, PT_RUN_UNCERTAIN);
    const pt_dev *model = pt_state(printer)->model;
    bool p710bt = model->pid == 0x20af;
    uint8_t buf[100];
    if (b->first_label &&
        ((!continuation && p710bt && !pt_dev_write(d, buf, pt_cmd_invalidate(buf))) ||
         (!continuation && !pt_dev_write(d, buf, pt_cmd_init(buf))) ||
         !pt_dev_write(d, buf, pt_cmd_rasterstart(buf, model->flags)) ||
         (p710bt && !pt_dev_write(d, buf, pt_cmd_auto_status(buf, true)))))
        return pt_fail_job(j);
    size_t info_len = p710bt ? pt_cmd_printinfo_extended(buf, b->tape_mm,
                             o->header.cupsHeight, b->first_label && !continuation ? 0 : 1) :
                             pt_cmd_printinfo(buf, b->tape_mm);
    if (!pt_dev_write(d, buf, info_len) ||
        !pt_dev_write(d, buf, pt_cmd_setmode(buf, pt_cut_autocut(b->policy, b->first_label) ? 0x40 : 0)) ||
        !pt_dev_write(d, buf, pt_cmd_setadvanced(buf, 0)) ||
        (p710bt && (!pt_dev_write(d, buf, pt_cmd_margin(buf, 14)) ||
                   !pt_dev_write(d, buf, pt_cmd_compression(buf, 2)))))
        return pt_fail_job(j);
    b->first_label = false;
    b->page_active = true;
    return true;
}

/* One delivered scanline is one cross-tape Brother raster line (M0 Probe B). */
static bool r_writeline(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned y, const unsigned char *l)
{
    (void)y;
    pt_batch *batch = papplJobGetData(j);
    if (!batch || batch->failed || !batch->page_active || papplJobIsCanceled(j)) return pt_fail_job(j);
    pt_geometry g;
    if (!pt_tape_geometry(batch->tape_mm, &g) ||
        !pt_page_width_matches(o, &g, batch->tape_mm))
        return pt_fail_job(j);
    uint8_t packed[16], buf[24];
    pt_pack_window(packed, l, g.left_px, g.imageable_px);
    const pt_dev *model = pt_state(papplJobGetPrinter(j))->model;
    size_t length = model->flags & FLAG_RASTER_PACKBITS ?
                    pt_cmd_sendraster_packbits(buf, packed, sizeof packed) :
                    pt_cmd_sendraster(buf, packed, sizeof packed);
    return pt_dev_write(d, buf, length) || pt_fail_job(j);
}

/* Wait for receive-ready, not just print-completed. This is USB flow control,
 * independent of the client finishing choice. No commands are sent here. */
#define PT_PRINT_READY_TIMEOUT_MS 20000
static long pt_ms_since(const struct timespec *t0);

static bool pt_wait_ready(pappl_job_t *j, pappl_device_t *d)
{
    const char *uri = papplPrinterGetDeviceURI(papplJobGetPrinter(j));
    if (!pt_usb_is_uri(uri)) return true;
    uint8_t packet[32];
    size_t used = 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (pt_ms_since(&start) < PT_PRINT_READY_TIMEOUT_MS) {
        if (papplJobIsCanceled(j)) return false;
        ssize_t n = pt_usb_read_status(d, uri, packet + used, sizeof packet - used);
        if (n < 0) {
            papplJobSetMessage(j, "printer status read failed after printing");
            return false;
        }
        used += (size_t)n;
        if (used < sizeof packet) continue;
        pt_status status;
        if (pt_parse_status(packet, &status)) {
            papplJobSetMessage(j, "printer reported invalid status after printing");
            return false;
        }
        papplLogJob(j, PAPPL_LOGLEVEL_DEBUG, "print status error=0x%04x type=0x%02x phase=0x%02x",
                    status.error, packet[18], packet[19]);
        if (status.error) {
            papplJobSetMessage(j, "printer reported error 0x%04x after printing", status.error);
            return false;
        }
        if (packet[18] == 6 && packet[19] == 0 && packet[20] == 0 && packet[21] == 0)
            return true;
        used = 0;
    }
    papplJobSetMessage(j, "printer did not become ready after printing");
    return false;
}

static bool r_endpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)o; (void)d; (void)p;
    pt_batch *batch = papplJobGetData(j);
    if (!batch || batch->failed || !batch->page_active || papplJobIsCanceled(j)) return pt_fail_job(j);
    batch->page_active = false;
    batch->pending = true;
    return true;
}

/* Abort cleanup never sends the pending label or replays a terminal attempt. */
static bool r_endjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)o;
    pt_batch *batch = papplJobGetData(j);
    if (!batch) return true;
    bool normal = !batch->failed && !papplJobIsCanceled(j) &&
                  papplJobGetState(j) < IPP_JSTATE_CANCELED;
    bool result = true;
    if (normal && batch->page_active) result = false;
    else if (normal && batch->pending) result = pt_print_pending(j, d, true);
    free(batch);
    papplJobSetData(j, NULL);
    return result;
}

/* Publish nominal tape dimensions with pixel-aligned physical margins. */
static bool media_col_for(int tape_mm, int length_centimm, int dpi, pappl_media_col_t *out)
{
    memset(out, 0, sizeof *out);
    pt_geometry g;
    if (dpi != 180 || !pt_tape_geometry(tape_mm, &g)) return false;
    out->size_width = tape_mm * 100;
    out->size_length = length_centimm;
    out->left_margin = g.left_margin_centimm;
    out->right_margin = g.right_margin_centimm;
    pwgFormatSizeName(out->size_name, sizeof(out->size_name), NULL, NULL,
                      out->size_width, length_centimm, "mm");
    strncpy(out->source, "main-roll", sizeof(out->source) - 1);
    strncpy(out->type, "labels", sizeof(out->type) - 1);
    out->tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;
    return true;
}

/* PAPPL's generated database applies one printer-wide minimum to every
 * size. Standard clients need the cassette's actual margins, including custom
 * lengths, before rendering a page that the physical guard can accept. */
static void pt_media_attrs(ipp_t *attrs)
{
    ipp_t *database[PT_MAX_MEDIA_NAMES], *sizes[sizeof pt_tape_mm / sizeof pt_tape_mm[0]];
    int left[sizeof pt_tape_mm / sizeof pt_tape_mm[0]], right[sizeof pt_tape_mm / sizeof pt_tape_mm[0]];
    int ncols = 0, nwidths = 0;
    const int presets = (int)(sizeof pt_preset_mm / sizeof pt_preset_mm[0]);
    for (size_t i = 0; i < sizeof pt_tape_mm / sizeof pt_tape_mm[0]; i++) {
        pt_geometry geometry;
        if (!pt_tape_geometry(pt_tape_mm[i], &geometry)) continue;
        left[nwidths] = geometry.left_margin_centimm;
        right[nwidths] = geometry.right_margin_centimm;
        sizes[nwidths] = ippNew();
        ippAddRange(sizes[nwidths], IPP_TAG_ZERO, "x-dimension", pt_tape_mm[i] * 100 - 1, pt_tape_mm[i] * 100 + 1);
        ippAddRange(sizes[nwidths], IPP_TAG_ZERO, "y-dimension", PT_ROLL_MIN_CENTIMM, PT_ROLL_MAX_CENTIMM);
        for (int k = 0; k <= presets; k++) {
            pappl_media_col_t media;
            media_col_for(pt_tape_mm[i], k < presets ? pt_preset_mm[k] * 100 : PT_ROLL_MIN_CENTIMM, 180, &media);
            ipp_t *col = ippNew(), *size = k == presets ? sizes[nwidths] : ippNew();
            if (k < presets) {
                ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", media.size_width);
                ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", media.size_length);
                ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-size-name", NULL, media.size_name);
            }
            ippAddCollection(col, IPP_TAG_ZERO, "media-size", size);
            if (k < presets) ippDelete(size);
            ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-left-margin", media.left_margin);
            ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-right-margin", media.right_margin);
            ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-top-margin", 0);
            ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-bottom-margin", 0);
            ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-source", NULL, media.source);
            ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-type", NULL, media.type);
            database[ncols++] = col;
        }
        nwidths++;
    }
    ippAddCollections(attrs, IPP_TAG_PRINTER, "media-col-database", ncols, (const ipp_t **)database);
    ippAddCollections(attrs, IPP_TAG_PRINTER, "media-size-supported", nwidths, (const ipp_t **)sizes);
    /* Publish ascending unique values so a generic minimum remains accurate. */
    int *margins[] = {left, right};
    const char *names[] = {"media-left-margin-supported", "media-right-margin-supported"};
    for (int side = 0; side < 2; side++) {
        for (int i = 1; i < nwidths; i++) {
            int value = margins[side][i], j = i;
            while (j && margins[side][j - 1] > value) { margins[side][j] = margins[side][j - 1]; j--; }
            margins[side][j] = value;
        }
        int n = 0;
        for (int i = 0; i < nwidths; i++)
            if (!n || margins[side][i] != margins[side][n - 1]) margins[side][n++] = margins[side][i];
        ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, names[side], n, margins[side]);
    }
    for (int i = 0; i < ncols; i++) ippDelete(database[i]);
    for (int i = 0; i < nwidths; i++) ippDelete(sizes[i]);
}

#define PT_STATUS_REASONS (PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED | PAPPL_PREASON_OTHER)

/* Idle polling and job-owned refresh publish the same clean tape/error result. */
static bool pt_publish_tape(pappl_printer_t *printer, pt_live_read live)
{
    pappl_preason_t reasons;
    if (!live.parsed) {
        reasons = PAPPL_PREASON_OFFLINE;
    } else if (live.st.error) {
        reasons = PAPPL_PREASON_OTHER;
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR,
                        "Printer status error=0x%04x, tape=%d mm", live.st.error, live.st.tape_mm);
    } else if (!live.st.tape_mm) {
        reasons = PAPPL_PREASON_MEDIA_EMPTY;
        pt_chain_set(printer, false);
    } else {
        pappl_media_col_t media;
        pappl_pr_driver_data_t data;
        papplPrinterGetDriverData(printer, &data);
        if (media_col_for(live.st.tape_mm, 10000, data.x_default, &media) &&
            papplPrinterSetReadyMedia(printer, 1, &media)) {
            if (data.media_ready[0].size_width != media.size_width)
                pt_chain_set(printer, false);
            pt_set_media_confirmed(printer, true);
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PT_STATUS_REASONS);
            return true;
        }
        reasons = PAPPL_PREASON_MEDIA_NEEDED | PAPPL_PREASON_OTHER;
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR,
                        "Loaded tape width %d mm cannot be published", live.st.tape_mm);
    }
    pt_set_media_confirmed(printer, false);
    papplPrinterSetReasons(printer, reasons, PT_STATUS_REASONS & ~reasons);
    return false;
}

static bool status_cb(pappl_printer_t *printer)
{
    /* The job owns live status and may have an unfinished command in flight. */
    if (papplPrinterGetState(printer) == IPP_PSTATE_PROCESSING) return true;
    if (pt_recovery_required(printer)) return true;
    const char *uri = papplPrinterGetDeviceURI(printer);
    if (!pt_usb_is_uri(uri)) return true;
    const pt_dev *model = pt_state(printer)->model;
    ssize_t count = pt_usb_count(model->vid, model->pid);
    if (count != 1) {
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PT_STATUS_REASONS & ~PAPPL_PREASON_OFFLINE);
        pt_set_media_confirmed(printer, false);
        if (!count) pt_chain_set(printer, false);
        if (count > 1) papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Multiple %s devices match; selection is ambiguous", model->name);
        return true;
    }
    pappl_device_t *device = papplPrinterOpenDevice(printer);
    if (!device) {
        /* Scheduling may have claimed the device after the idle check. */
        if (papplPrinterGetState(printer) == IPP_PSTATE_PROCESSING) return true;
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PT_STATUS_REASONS & ~PAPPL_PREASON_OFFLINE);
        pt_set_media_confirmed(printer, false);
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "%s is present but cannot be opened or claimed", model->name);
        return true;
    }
    /* Status requests do not initialize or finalize the printer. */
    pt_live_read live = pt_read_live_tape(device, uri);
    papplPrinterCloseDevice(printer);
    pt_publish_tape(printer, live);
    return true;
}

/* #43 barrier budget. A printer that has just been switched on can take seconds
 * to answer ESC i S even though its USB handle already opens, so one poll round
 * is not enough. Each pt_read_live_tape is bounded at ~4 s, so the true ceiling
 * is one in-flight attempt past the deadline (~14 s). One-time cost: only the
 * first job after a start-while-off can hit it. */
#define PT_MEDIA_REFRESH_MS 10000
#define PT_MEDIA_RETRY_NS   500000000L   /* 0.5 s between attempts */

static long pt_ms_since(const struct timespec *t0)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

bool pt_refresh_ready_media(pappl_job_t *job, pappl_device_t *device)
{
    pappl_printer_t *printer = papplJobGetPrinter(job);
    if (pt_recovery_required(printer)) {
        papplJobSetMessage(job, "Output uncertain: power-cycle printer, then restart server");
        return false;
    }
    const char *uri = papplPrinterGetDeviceURI(printer);
    if (!pt_usb_is_uri(uri)) return true;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        pt_live_read live = pt_read_live_tape(device, uri);
        if (pt_publish_tape(printer, live)) return true;
        if (live.parsed || live.n != 0 || papplJobIsCanceled(job)) break;
        struct timespec delay = {0, PT_MEDIA_RETRY_NS};
        nanosleep(&delay, NULL);
    } while (pt_ms_since(&start) < PT_MEDIA_REFRESH_MS);
    papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
    papplJobSetMessage(job, "Live tape status is not clean, readable, and supported");
    return false;
}

bool pt_driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri,
                  const char *device_id, pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata)
{
    (void)system; (void)driver_name; (void)device_uri; (void)device_id;
    (void)cbdata;

    char model[64];
    parse_mdl(device_id, model, sizeof model);
    const pt_dev *dev = model[0] ? pt_usb_supported_model(model) : NULL;
    if (!dev) {
        papplLog(system, PAPPL_LOGLEVEL_ERROR,
                 "Unsupported printer identity '%s'; select PT-2730 or PT-P710BT", model);
        return false;
    }
    if (device_uri && !strncmp(device_uri, "ptouch://", 9) &&
        pt_usb_model_from_uri(device_uri) != dev) {
        papplLog(system, PAPPL_LOGLEVEL_ERROR,
                 "Printer identity '%s' does not match USB URI '%s'", model, device_uri);
        return false;
    }

    pt_printer_state *state = calloc(1, sizeof *state);
    if (!state) return false;
    if (pthread_mutex_init(&state->lock, NULL)) { free(state); return false; }
    state->model = dev;
    data->extension = state;

    data->rstartjob_cb = r_startjob;
    data->rstartpage_cb = r_startpage;
    data->rwriteline_cb = r_writeline;
    data->rendpage_cb = r_endpage;
    data->rendjob_cb = r_endjob;
    data->status_cb = status_cb;
    data->delete_cb = pt_delete_cb;
    /* PNG is both a normal public format and the owned filter destination. */
    data->format = "image/png";
    data->printfile_cb = pt_png_printfile;

    int dpi = dev->dpi;
    int max_px = dev->max_px;

    char mm[80];
    snprintf(mm, sizeof mm, "Brother %s", dev->name);
    strncpy(data->make_and_model, mm, sizeof(data->make_and_model) - 1);
    data->ppm = 20;
    data->kind = PAPPL_KIND_LABEL;
    data->scaling_default = PAPPL_SCALING_NONE;  /* 1:1, no fit/fill (center/crop) */
    data->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
    data->color_default = PAPPL_COLOR_MODE_BI_LEVEL;
    data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
    data->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;
    data->finishings = PAPPL_FINISHINGS_TRIM;
    if (!*driver_attrs) *driver_attrs = ippNew();
    pt_finishing_attrs(*driver_attrs);

    data->num_resolution = 1;
    data->x_resolution[0] = data->y_resolution[0] = dpi;
    data->x_default = data->y_default = dpi;
    /* One lower bound must admit every advertised cassette. Per-tape margins
     * still constrain the actual physical window. */
    data->left_right = 53;
    data->bottom_top = 0;

    data->num_source = 1; data->source[0] = "main-roll";
    data->num_type = 1; data->type[0] = "labels";

    /* Advertise discrete per-width length presets PLUS one continuous roll range.
     * PAPPL collapses the media list to a SINGLE roll min/max box (printer-driver.c
     * keeps only the last roll_min_/roll_max_ seen), so the range must span the
     * full width span (narrowest..widest tape) x [5mm..300mm]; any (width,length)
     * inside the box is then honored. Names live in durable file-static storage
     * (PAPPL keeps the const char* pointers; Codex #3, #6). */
    int n = 0;
    int min_w100 = 0, max_w100 = 0;
    const int per_width = (int)(sizeof(pt_preset_mm) / sizeof(pt_preset_mm[0]));
    for (size_t i = 0; i < sizeof(pt_tape_mm) / sizeof(pt_tape_mm[0]); i++) {
        int px = pt_tape_px(pt_tape_mm[i]);
        if (px <= 0 || px > max_px)
            continue;
        if (n + per_width + 2 > PT_MAX_MEDIA_NAMES || n + per_width + 2 > PAPPL_MAX_MEDIA)
            break;
        int w100 = pt_tape_mm[i] * 100;   /* #39: nominal tape width, not the imageable width */
        if (min_w100 == 0 || w100 < min_w100) min_w100 = w100;
        if (w100 > max_w100) max_w100 = w100;
        for (int k = 0; k < per_width; k++) {
            pwgFormatSizeName(g_media_names[n], PT_MEDIA_NAME_LEN, NULL, NULL,
                              w100, pt_preset_mm[k] * 100, "mm");
            data->media[n] = g_media_names[n]; n++;
        }
    }
    /* One roll range across the full width span and 5..300mm length. */
    if (max_w100 > 0 && n + 2 <= PT_MAX_MEDIA_NAMES) {
        pwgFormatSizeName(g_media_names[n], PT_MEDIA_NAME_LEN, "roll", "min",
                          min_w100, PT_ROLL_MIN_CENTIMM, "mm");
        data->media[n] = g_media_names[n]; n++;
        pwgFormatSizeName(g_media_names[n], PT_MEDIA_NAME_LEN, "roll", "max",
                          max_w100, PT_ROLL_MAX_CENTIMM, "mm");
        data->media[n] = g_media_names[n]; n++;
    }
    data->num_media = n;

    /* Safe fixed default: 12mm x 100mm. status_cb republishes media_ready as the
     * live-loaded width once the tape is read (Codex #2). */
    pappl_media_col_t mc;
    media_col_for(12, 10000, dpi, &mc);
    data->media_default = mc;
    data->media_ready[0] = mc;
    pt_media_attrs(*driver_attrs);
    return true;
}
