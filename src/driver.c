#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <pappl/pappl.h>
#include "driver.h"
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

/* px in 1/100 mm so CUPS' truncating px conversion round-trips exactly.
 * cupsRasterInitHeader does cupsWidth = width*dpi/2540 with INTEGER TRUNCATION,
 * so round() can drift down by 1 (e.g. 76px->round 1072->floor 75). CEIL of
 * px*2540/dpi gives the smallest 1/100 mm whose truncation is exactly px. */
static int pt_px_to_centimm(int px, int dpi)
{
    return (px * 2540 + dpi - 1) / dpi;
}

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

/* Read the live tape from an already-open device: ESC @ + ESC i S, then poll for
 * the reply. The caller owns the device and interprets the result; a supported
 * tape is `parsed && st.tape_px > 0`. Callers must have checked that this is a
 * ptouch:// device: pt_usb_read_status casts the device data to our libusb
 * struct, so calling it on socket:// is undefined behaviour. */
static pt_live_read pt_read_live_tape(pappl_device_t *device)
{
    uint8_t cmd[8];
    pt_dev_write(device, cmd, pt_cmd_init(cmd));            /* ESC @ : reset the parser so the device answers */
    pt_dev_write(device, cmd, pt_cmd_status_request(cmd));  /* ESC i S : status info request */
    papplDeviceFlush(device);

    uint8_t buf[32] = {0};
    pt_live_read r = {0};
    for (int tries = 0; r.n == 0 && tries < PT_STATUS_POLL_TRIES; tries++) {
        struct timespec w = {0, PT_STATUS_POLL_NS};
        nanosleep(&w, NULL);
        r.n = pt_usb_read_status(device, buf, sizeof buf);
    }
    r.parsed = (r.n == 32 && pt_parse_status(buf, &r.st) == 0);
    return r;
}

#define PT_MAX_LABELS    64
#define PT_OPS_PER_LABEL 5
#define PT_OPS_CAP       (PT_MAX_LABELS * PT_OPS_PER_LABEL)
typedef struct { pt_op ops[PT_OPS_CAP]; int n_ops; int index;
                uint8_t tape_mm; } pt_batch;


/* Render one planned op to the device (PRINT_PAGE is the raster, handled by rwriteline). */
static bool pt_emit_op(pappl_device_t *d, const pt_op *op, uint8_t tape_mm)
{
    uint8_t buf[16];
    size_t n = 0;
    switch (op->kind) {
        case PT_OP_PRINTINFO:   n = pt_cmd_printinfo(buf, tape_mm); break;
        case PT_OP_SETMODE:     n = pt_cmd_setmode(buf, op->arg);   break;
        case PT_OP_SETADVANCED: n = pt_cmd_setadvanced(buf, op->arg); break;
        case PT_OP_FF:          n = pt_cmd_ff(buf);    break;
        case PT_OP_EJECT:       n = pt_cmd_eject(buf); break;
        case PT_OP_PRINT_PAGE:  return true;  /* raster handled elsewhere */
    }
    return pt_dev_write(d, buf, n);
}

/* Defined below with status_cb; forward-declared for the #38 live read in r_startjob. */
static bool media_col_for(int tape_mm, int length_centimm, int dpi, pappl_media_col_t *out);

/* ---- cross-job chaining state (#41) -------------------------------------
 * "chain open" means the last label of a job was finished with FF instead of
 * EJECT, so the tape is still at the head and the run is not finalized. The
 * status callback and raster callbacks can run on different threads, so
 * every access takes g_chain_lock. Entries are keyed by printer ID,
 * never by pappl_printer_t *: a stored pointer can dangle when the printer is
 * deleted, an ID cannot (papplSystemFindPrinter just returns NULL).
 * The same table carries `media_confirmed` (#43), which needs exactly the same
 * per-printer, cross-thread storage. */
#define PT_CHAIN_MAX        8

static pthread_mutex_t g_chain_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int printer_id; bool open; bool media_confirmed; } g_chain[PT_CHAIN_MAX];

/* Slot for this printer id, optionally allocating one. Call with the lock held. */
static int pt_chain_slot(int id, bool create)
{
    int free_slot = -1;
    for (int i = 0; i < PT_CHAIN_MAX; i++) {
        if (g_chain[i].printer_id == id)
            return i;
        if (free_slot < 0 && g_chain[i].printer_id == 0)
            free_slot = i;
    }
    if (!create || free_slot < 0)
        return -1;
    g_chain[free_slot].printer_id = id;
    g_chain[free_slot].open = false;
    g_chain[free_slot].media_confirmed = false;
    return free_slot;
}

/* Reserve state before accepting a no-cut job; never silently cut on exhaustion. */
static bool pt_chain_reserve(pappl_printer_t *printer)
{
    pthread_mutex_lock(&g_chain_lock);
    bool ok = pt_chain_slot(papplPrinterGetID(printer), true) >= 0;
    pthread_mutex_unlock(&g_chain_lock);
    return ok;
}

static void pt_chain_set(pappl_printer_t *printer, bool open)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(papplPrinterGetID(printer), open);
    if (s >= 0) {
        g_chain[s].open = open;
    }
    pthread_mutex_unlock(&g_chain_lock);
}

static bool pt_chain_get(pappl_printer_t *printer)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(papplPrinterGetID(printer), false);
    bool open = s >= 0 && g_chain[s].open;
    pthread_mutex_unlock(&g_chain_lock);
    return open;
}

/* #43: has a live status read ever confirmed the loaded tape for this printer?
 * Until it has, media-default is still the safe 12 mm placeholder from
 * pt_driver_cb, and any page built from it is a guess. */
static void pt_set_media_confirmed(pappl_printer_t *printer, bool confirmed)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(papplPrinterGetID(printer), confirmed);
    if (s >= 0)
        g_chain[s].media_confirmed = confirmed;
    pthread_mutex_unlock(&g_chain_lock);
}

static bool pt_media_confirmed(pappl_printer_t *printer)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(papplPrinterGetID(printer), false);
    bool confirmed = s >= 0 && g_chain[s].media_confirmed;
    pthread_mutex_unlock(&g_chain_lock);
    return confirmed;
}

static void pt_chain_drop(int id)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(id, false);
    if (s >= 0)
        memset(&g_chain[s], 0, sizeof g_chain[s]);
    pthread_mutex_unlock(&g_chain_lock);
}

/* Drop the entry when PAPPL deletes the printer (tidy-up only: the ID lookup is
 * what makes deletion safe). */
static void pt_delete_cb(pappl_printer_t *printer, pappl_pr_driver_data_t *data)
{
    (void)data;
    pt_chain_drop(papplPrinterGetID(printer));
}

/* Start of job: init the printer and select raster transfer mode.
 * flags = 0: PT-2730 is FLAG_NONE. Per-model dispatch (P700 rasterstart variant,
 * packbits) is deferred until there is non-PT-2730 hardware to verify it. */
static bool r_startjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    pappl_printer_t *printer = papplJobGetPrinter(j);

    /* strict width guard (#19; fit policy is #35). PTOUCH_WIDTH_GUARD: default/strict.
     * Any non-strict value (e.g. fit) is unimplemented -> fall back to strict, never accept. */
    const char *gpol = getenv("PTOUCH_WIDTH_GUARD");
    if (gpol && strcmp(gpol, "strict") != 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN,
                        "PTOUCH_WIDTH_GUARD=%s not implemented; using strict (#35)", gpol);

    pappl_pr_driver_data_t dd;
    papplPrinterGetDriverData(printer, &dd);  /* fills dd with current driver data incl. media_ready */
    int dpi = o->header.HWResolution[0] ? (int)o->header.HWResolution[0] : dd.x_default;

    /* #38: a job queued while the printer was offline keeps a stale OFFLINE reason
     * (PAPPL suppresses status_cb while a job holds the device), which the strict
     * width guard would fault as "tape unknown". Re-read the live tape from the
     * already-open device, but ONLY when:
     *   (a) this is our ptouch:// device - pt_usb_read_status casts the device data
     *       to our libusb struct, so calling it on socket:// is undefined behaviour; and
     *   (b) the cached state is suspect - a healthy printer keeps the fast path
     *       (a dead device can cost ~4s: 10 tries x (100ms sleep + 300ms timeout)). */
    bool live_ok = false;
    pt_status live = {0};
    const char *duri = papplPrinterGetDeviceURI(printer);
    pappl_preason_t pr0 = papplPrinterGetReasons(printer);
    if (duri && !strncmp(duri, "ptouch://", 9) &&
        (pr0 & (PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY))) {
        pt_live_read lr = pt_read_live_tape(d);
        if (lr.parsed && lr.st.tape_px > 0) {
            pappl_media_col_t mc;
            if (media_col_for(lr.st.tape_mm, 10000, dpi, &mc)) {
                live = lr.st;
                live_ok = true;
                papplPrinterSetReadyMedia(printer, 1, &mc);
                papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                       PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY);
            }
        } else if (lr.parsed && lr.st.tape_mm == 0) {
            papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_OFFLINE);
        }
    }

    /* #39: compare like for like in FULL-PAGE px. cupsWidth is the full media width on every
     * CUPS version (cups/raster-stream.c: cupsWidth = media->width * xdpi / 2540); margins only
     * populate ImageBox*. size_width is the nominal tape width, so this is the same space. */
    int loaded_px = (int)(dd.media_ready[0].size_width * dpi / 2540);
    unsigned w = o->header.cupsWidth;

    /* reasons re-read AFTER the live block, which may have cleared them */
    bool unknown = (papplPrinterGetReasons(printer) &
                    (PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_OFFLINE)) != 0;
    if (live_ok) {                 /* live read wins over cached state (#38) */
        /* #43: compare like for like. live.tape_px is the IMAGEABLE width (128 for 24mm),
         * but `w` is cupsWidth, which since #39 is the FULL nominal page width (170 for
         * 24mm). Convert the live tape to the same nominal space instead of mixing them -
         * the old assignment faulted correct jobs whenever this branch fired, and is the
         * source of the "!= loaded tape 128 px" message reported in #43. */
        loaded_px = (int)((live.tape_mm * 100) * dpi / 2540);
        unknown   = false;
    }
    if (unknown || (int)w != loaded_px) {
        papplJobSetReasons(j, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(j, "width guard (strict): job width %u px != loaded tape %d px%s",
                           w, loaded_px, unknown ? " (tape unknown)" : "");
        papplLogJob(j, PAPPL_LOGLEVEL_ERROR,
                    "width guard (strict): job width %u px != loaded tape %d px%s",
                    w, loaded_px, unknown ? " (tape unknown)" : "");
        return false;  /* fault the job; no raster has been written */
    }

    /* The finishing choice is already resolved by PAPPL, including defaults
     * and finishings-col. No queue or environment setting can override it. */
    pt_cut_mode mode = (o->finishings & PAPPL_FINISHINGS_TRIM) ? PT_CUT_EACH : PT_CUT_NONE;
    if (mode == PT_CUT_NONE && !pt_chain_reserve(printer)) {
        papplJobSetMessage(j, "no-cut run cannot be tracked: printer state table is full");
        return false;
    }

    /* A previous none job may have completed while the queue was empty.
     * Reopening its handle does not make an ESC @ reset safe. */
    uint8_t buf[8];
    if (!pt_chain_get(printer) && !pt_dev_write(d, buf, pt_cmd_init(buf)))
        return false;
    if (!pt_dev_write(d, buf, pt_cmd_rasterstart(buf, 0)))
        return false;

    int n = papplJobGetCopies(j);
    if (n < 1) n = 1;
    if (n > PT_MAX_LABELS) {
        papplJobSetReasons(j, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(j, "batch too large: %d copies (max %d)", n, PT_MAX_LABELS);
        return false;
    }
    int precut = 1;
    const char *pc = getenv("PTOUCH_PRECUT");
    if (pc && !strcmp(pc, "0")) precut = 0;

    pt_batch *b = calloc(1, sizeof *b);
    if (!b) return false;
    /* live tape mm when the re-read succeeded; otherwise reverse-lookup from the
     * width guard's loaded_px (Codex #1) */
    /* #39: size_width is the nominal tape width, so take mm from it directly; loaded_px is
     * full-page px now and pt_mm_for_px() expects imageable px. */
    b->tape_mm = live_ok ? (uint8_t)live.tape_mm : (uint8_t)(dd.media_ready[0].size_width / 100);
    b->n_ops = pt_plan_batch(n, b->tape_mm, mode, precut, false, b->ops, PT_OPS_CAP);
    if (b->n_ops < 0) {                      /* cap exceeded (Codex #5) */
        free(b);
        papplJobSetReasons(j, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(j, "cutter plan overflow for %d copies", n);
        return false;
    }
    b->index = 0;
    papplJobSetData(j, b);
    return true;
}

static bool r_startpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)o; (void)p;
    pt_batch *b = papplJobGetData(j);
    if (!b) return true;
    if (papplJobIsCanceled(j)) return false;
    pt_chain_set(papplJobGetPrinter(j), true);

    for (int k = 0; k < b->n_ops; k++) {
        if (b->ops[k].page_index != b->index) continue;
        if (b->ops[k].kind == PT_OP_PRINT_PAGE) break;   /* raster follows */
        if (!pt_emit_op(d, &b->ops[k], b->tape_mm)) return false;
    }
#if PT_LEADER_PX > 0
    /* Leading-edge quirk: feed PT_LEADER_PX blank cross-tape lines so leading
     * content is not clipped. Adds to the cut length (Codex #4). Default 0. */
    { uint8_t blank[16] = {0}, buf[24];
      for (int i = 0; i < PT_LEADER_PX; i++)
          if (!pt_dev_write(d, buf, pt_cmd_sendraster(buf, blank, 16))) return false;
      papplDeviceFlush(d); }
#endif
    return true;
}

/* One delivered scanline is one cross-tape Brother raster line (M0 Probe B). */
static bool r_writeline(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned y, const unsigned char *l)
{
    (void)y;
    /* #39: with nominal media sizes the page raster is the FULL tape width (cupsWidth),
     * and the printable area is a window inside it - cupsWidth is NOT reduced by margins on
     * any CUPS version (see cups/raster-stream.c: cupsWidth = media->width * xdpi / 2540).
     * Extract [left_px, left_px + win) and centre that in the 128-dot head. With zero
     * margins this degenerates to the previous behaviour (start 0, win = cupsWidth). */
    int dpi   = o->header.HWResolution[0] ? (int)o->header.HWResolution[0] : 180;
    int width = (int)o->header.cupsWidth;
    int left  = o->media.left_margin  * dpi / 2540;
    int right = o->media.right_margin * dpi / 2540;
    int win   = width - left - right;
    if (win > 128) win = 128;            /* never exceed the head */
    if (win < 0)   win = 0;
    if (left + win > width) win = width - left;
    if (win <= 0) {
        papplLogJob(j, PAPPL_LOGLEVEL_ERROR,
                    "empty printable window (cupsWidth=%d left=%d right=%d)", width, left, right);
        return false;
    }
    uint8_t packed[16];
    pt_pack_window(packed, l, left, win);
    uint8_t buf[24];
    /* One checked transfer per line avoids bursts overrunning the printer buffer. */
    return pt_dev_write(d, buf, pt_cmd_sendraster(buf, packed, 16));
}

/* Wait for receive-ready, not just print-completed. This is USB flow control,
 * independent of the client finishing choice. No commands are sent here. */
#define PT_PRINT_READY_TIMEOUT_MS 20000
static long pt_ms_since(const struct timespec *t0);

static bool pt_wait_ready(pappl_job_t *j, pappl_device_t *d)
{
    const char *uri = papplPrinterGetDeviceURI(papplJobGetPrinter(j));
    if (!uri || strncmp(uri, "ptouch://", 9)) return true;
    uint8_t packet[32];
    size_t used = 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (pt_ms_since(&start) < PT_PRINT_READY_TIMEOUT_MS) {
        if (papplJobIsCanceled(j)) return false;
        ssize_t n = pt_usb_read_status(d, packet + used, sizeof packet - used);
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
    (void)o; (void)p;
    pt_batch *b = papplJobGetData(j);
    if (!b) return true;
    if (papplJobIsCanceled(j)) return false;
    bool seen_print = false;
    for (int k = 0; k < b->n_ops; k++) {
        if (b->ops[k].page_index != b->index) continue;
        if (seen_print) {
            if (!pt_emit_op(d, &b->ops[k], b->tape_mm)) return false;
            if (b->ops[k].kind == PT_OP_EJECT)
                pt_chain_set(papplJobGetPrinter(j), false);
            if (!pt_wait_ready(j, d)) return false;
            b->index++;
            return true;
        }
        if (b->ops[k].kind == PT_OP_PRINT_PAGE) seen_print = true;
    }
    return false;
}

/* Cleanup never invents a cut or replays an uncertain terminal command. */
static bool r_endjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)o; (void)d;
    free(papplJobGetData(j));
    papplJobSetData(j, NULL);
    return true;
}

/* Raw-print callback. Only needed because PAPPL's validate_driver reads a
 * non-null driver_data.format as "supports raw printing" and rejects the driver
 * without a printfile_cb. PT_RASTER_FORMAT is a filter target, not a format a
 * client can usefully send, so raw data in it faults (#40). */
static bool pt_printfile_cb(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)o; (void)d;
    papplJobSetReasons(j, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
    papplJobSetMessage(j, "raw '%s' data is not printable; send image/png", PT_RASTER_FORMAT);
    papplLogJob(j, PAPPL_LOGLEVEL_ERROR,
                "raw '%s' data is not printable; send image/png", PT_RASTER_FORMAT);
    return false;
}

/* Fill a media-col for a loaded tape width (mm) at a continuous length (1/100 mm).
 * The cross-tape dimension is the tape's imageable px expressed in 1/100 mm
 * (round-trips to exactly pt_tape_px). Returns false for an unknown/too-wide
 * width. size_name is owned by the struct copy, so formatting it here is safe. */
static bool media_col_for(int tape_mm, int length_centimm, int dpi, pappl_media_col_t *out)
{
    memset(out, 0, sizeof(*out));  /* zero even on the false path so callers that ignore the return get a safe col */
    int px = pt_tape_px(tape_mm);
    if (px <= 0 || px > 128)       /* 128 = head width, the actual cross-tape limit */
        return false;
    /* #39: advertise the NOMINAL tape width with real margins, so clients can match the
     * tape they bought (a 24mm template vs 24mm tape). The printable area is the
     * head-limited imageable width; the difference is unprintable edge, expressed as
     * symmetric left/right margins. Requires CUPS >= 2.5, where PAPPL passes margins into
     * cupsRasterInitHeader so cupsWidth is the PRINTABLE width (2400-296-296 -> 128px). */
    int printable = pt_px_to_centimm(px, dpi);
    out->size_width = tape_mm * 100;
    out->size_length = length_centimm;
    out->left_margin = out->right_margin = (out->size_width - printable) / 2;
    pwgFormatSizeName(out->size_name, sizeof(out->size_name), NULL, NULL,
                      out->size_width, length_centimm, "mm");
    strncpy(out->source, "main-roll", sizeof(out->source) - 1);
    strncpy(out->type, "labels", sizeof(out->type) - 1);
    out->tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;
    return true;
}

/* Read the loaded tape over USB and publish media-ready + printer reasons.
 * Distinguishes unplugged (OFFLINE) from in-use (NULL open while present). */
static bool status_cb(pappl_printer_t *printer)
{
    char model[64];
    parse_mdl(papplPrinterGetDeviceID(printer), model, sizeof model);
    const pt_dev *dt = pt_lookup_name(model);
    if (!dt)
        dt = pt_lookup_dev(0x04f9, 0x2041);  /* fallback PT-2730 */

    if (!pt_usb_present(dt->vid, dt->pid)) {
        /* unplugged: OFFLINE only; drop any stale MEDIA_EMPTY from a prior poll */
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_MEDIA_EMPTY);
        pt_chain_set(printer, false);   /* #41: the tape left with the device */
        pt_set_media_confirmed(printer, false);  /* #43: a power-cycle must re-confirm */
        return true;
    }

    /* A no-cut run can remain open indefinitely. Idle polling must not send
     * ESC @ into it; cached media stays valid until the device disappears. */
    if (pt_chain_get(printer)) return true;

    pappl_device_t *dev = papplPrinterOpenDevice(printer);
    if (!dev) {
        /* present but not openable: a job owns it; clear OFFLINE, leave media */
        papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
        return true;
    }

    pt_live_read lr = pt_read_live_tape(dev);
    papplPrinterCloseDevice(printer);

    pt_status st = lr.st;
    if (lr.parsed && st.tape_px > 0) {
        /* #41: a cassette swapped mid-chain must not inherit the flag - the tape at
         * the head is gone, so treat the run as finished (the next job pre-cuts). */
        pappl_pr_driver_data_t dd;
        papplPrinterGetDriverData(printer, &dd);
        if ((int)(dd.media_ready[0].size_width / 100) != st.tape_mm)
            pt_chain_set(printer, false);
        pappl_media_col_t mc;
        if (media_col_for(st.tape_mm, 10000, dt->dpi, &mc) &&
            papplPrinterSetReadyMedia(printer, 1, &mc)) {
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                   PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY);
            pt_set_media_confirmed(printer, true);   /* #43 */
        } else {
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
        }
    } else if (lr.parsed && st.tape_mm == 0) {
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_OFFLINE);
        pt_chain_set(printer, false);   /* #41: no tape, nothing to finalize */
        pt_set_media_confirmed(printer, false);  /* #43 */
    } else if (lr.n < 0) {
        /* I/O error: device likely gone; OFFLINE, drop any stale MEDIA_EMPTY */
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_MEDIA_EMPTY);
        pt_chain_set(printer, false);   /* #41 */
        pt_set_media_confirmed(printer, false);  /* #43 */
    } else {  /* 0 / short / garbled: transient, keep last-known media */
        papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
    }
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
    const char *duri = papplPrinterGetDeviceURI(printer);

    /* pt_usb_read_status casts the device data to our libusb struct, so a
     * socket:// sink must never reach it (the same gate as #38 and #40). */
    if (!duri || strncmp(duri, "ptouch://", 9))
        return true;
    /* An open chain means the previous job left the tape at the head with the
     * device still open. ESC @ there resets the printer mid-run ("Tape cassette
     * changed!", #41), and the media cannot be stale anyway: it was just printed on. */
    if (pt_chain_get(printer))
        return true;
    /* Only suspect media is worth the poll; a confirmed, online printer pays nothing. */
    if (pt_media_confirmed(printer) &&
        !(papplPrinterGetReasons(printer) & (PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY)))
        return true;

    pappl_pr_driver_data_t dd;
    papplPrinterGetDriverData(printer, &dd);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    const char *fail = "tape not readable yet";   /* reason if the barrier gives up */
    for (;;) {
        pt_live_read lr = pt_read_live_tape(device);
        if (lr.parsed && lr.st.tape_px > 0) {
            pappl_media_col_t mc;
            if (media_col_for(lr.st.tape_mm, 10000, dd.x_default, &mc) &&
                papplPrinterSetReadyMedia(printer, 1, &mc)) {
                /* Only now is the state trustworthy: both the read and the publish
                 * succeeded, so the reasons may be cleared. */
                papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                       PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY);
                pt_set_media_confirmed(printer, true);
                /* papplPrinterSetReadyMedia reaches media_default only when the ready
                 * entry's source matches the default's ("main-roll" in both, from
                 * media_col_for), and papplJobCreatePrintOptions builds the page from
                 * media_default. Log what actually landed there so a future divergence
                 * is visible instead of silently resurrecting this bug. */
                papplPrinterGetDriverData(printer, &dd);
                papplLogJob(job, PAPPL_LOGLEVEL_DEBUG,
                            "#43 media refresh: default now %d centimm (tape %d mm)",
                            dd.media_default.size_width, lr.st.tape_mm);
                return true;
            }
            fail = "loaded tape is not supported";   /* re-reading returns the same width */
            break;
        }
        if (lr.parsed && lr.st.tape_mm == 0) {       /* answered, cassette empty */
            papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_OFFLINE);
            pt_set_media_confirmed(printer, false);
            fail = "no tape loaded";
            break;
        }
        if (pt_ms_since(&t0) >= PT_MEDIA_REFRESH_MS)
            break;
        papplLogJob(job, PAPPL_LOGLEVEL_DEBUG,
                    "#43 media refresh: no status after %ld ms, retrying", pt_ms_since(&t0));
        struct timespec w = {0, PT_MEDIA_RETRY_NS};
        nanosleep(&w, NULL);
    }

    /* Do NOT fall through and build the page: papplJobCreatePrintOptions would
     * snapshot the stale default, and r_startjob's live read can then only fault the
     * finished header, not repair it. Failing loudly beats printing wrong geometry. */
    papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
    papplJobSetMessage(job, "media refresh failed: %s", fail);
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "#43 media refresh failed after %ld ms: %s",
                pt_ms_since(&t0), fail);
    return false;
}

bool pt_driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri,
                  const char *device_id, pappl_pr_driver_data_t *data, ipp_t **driver_attrs, void *cbdata)
{
    (void)system; (void)driver_name; (void)device_uri; (void)device_id;
    (void)driver_attrs; (void)cbdata;

    data->rstartjob_cb = r_startjob;
    data->rstartpage_cb = r_startpage;
    data->rwriteline_cb = r_writeline;
    data->rendpage_cb = r_endpage;
    data->rendjob_cb = r_endjob;
    data->status_cb = status_cb;
    data->delete_cb = pt_delete_cb;   /* #41: forget this printer's chain state */
    /* #40: our own raster destination type, so main.c's image/png filter is
     * chosen ahead of PAPPL's built-in one. printfile_cb is mandatory alongside
     * `format`; it only faults (see pt_printfile_cb). Side effect:
     * PT_RASTER_FORMAT appears in document-format-supported. */
    data->format = PT_RASTER_FORMAT;
    data->printfile_cb = pt_printfile_cb;

    char model[64];
    parse_mdl(device_id, model, sizeof model);
    const pt_dev *dev = pt_lookup_name(model);
    if (!dev)
        dev = pt_lookup_dev(0x04f9, 0x2041);  /* fallback PT-2730 */
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

    data->num_resolution = 1;
    data->x_resolution[0] = data->y_resolution[0] = dpi;
    data->x_default = data->y_default = dpi;
    /* 0/0 margins keep the imageable px exact: a nonzero margin would shrink
     * media->width-2*margin and perturb cupsWidth (Codex #4). */
    /* #39: media-*-margin-supported. One printer-wide value; status_cb keeps it on the
     * loaded tape's margin. Seeded from the safe default (12mm) before the first status read. */
    {
        int dpx = pt_tape_px(12);
        data->left_right = dpx > 0 ? (1200 - pt_px_to_centimm(dpx, dpi)) / 2 : 0;
    }
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
    return true;
}
