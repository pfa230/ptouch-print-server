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

/* Write exactly n bytes through PAPPL's buffered device API; true iff accepted. */
static bool pt_dev_write(pappl_device_t *device, const uint8_t *buf, size_t n)
{
    return papplDeviceWrite(device, buf, n) == (ssize_t)n;
}

#define PT_MAX_LABELS    64
#define PT_OPS_PER_LABEL 5
#define PT_OPS_CAP       (PT_MAX_LABELS * PT_OPS_PER_LABEL)
typedef struct { pt_op ops[PT_OPS_CAP]; int n_ops; int n_labels; int index;
                uint8_t tape_mm; bool started; bool finalized;
                pt_cut_mode mode; int precut; bool chain_out; } pt_batch;


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
    if (!pt_dev_write(d, buf, n))
        return false;
    papplDeviceFlush(d);
    return true;
}

/* Defined below with status_cb; forward-declared for the #38 live read in r_startjob. */
static bool media_col_for(int tape_mm, int length_centimm, int dpi, pappl_media_col_t *out);

/* Tape mode/precut config. */
static pt_cut_mode pt_env_cut_mode(pappl_printer_t *p)
{
    const char *m = getenv("PTOUCH_CUT_MODE");
    if (!m || !strcmp(m, "each")) return PT_CUT_EACH;
    if (!strcmp(m, "end"))  return PT_CUT_END;
    if (!strcmp(m, "none")) return PT_CUT_NONE;
    papplLogPrinter(p, PAPPL_LOGLEVEL_WARN, "PTOUCH_CUT_MODE=%s unknown; using each", m);
    return PT_CUT_EACH;
}

/* ---- cross-job chaining state (#41) -------------------------------------
 * "chain open" means the last label of a job was finished with FF instead of
 * EJECT, so the tape is still at the head and the run is not finalized. The
 * idle timer runs on PAPPL's main loop while the raster callbacks run on a job
 * thread, so every access takes g_chain_lock. Entries are keyed by printer ID,
 * never by pappl_printer_t *: a stored pointer can dangle when the printer is
 * deleted, an ID cannot (papplSystemFindPrinter just returns NULL). */
#define PT_CHAIN_MAX        8
#define PT_CHAIN_IDLE_TICKS 3   /* ~3 s of quiet before the idle finaliser cuts */

static pthread_mutex_t g_chain_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int printer_id; bool open; int idle_ticks; } g_chain[PT_CHAIN_MAX];

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
    g_chain[free_slot].idle_ticks = 0;
    return free_slot;
}

/* Reserve this printer's slot. False means the table is full: do not chain,
 * finalize normally (always safe, just one wasted leader). */
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
        g_chain[s].idle_ticks = 0;
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

static void pt_chain_drop(int id)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(id, false);
    if (s >= 0)
        memset(&g_chain[s], 0, sizeof g_chain[s]);
    pthread_mutex_unlock(&g_chain_lock);
}

/* Count a qualifying idle tick and report whether the finalise threshold is met. */
static bool pt_chain_idle_ready(int id, bool quiet)
{
    pthread_mutex_lock(&g_chain_lock);
    int s = pt_chain_slot(id, false);
    bool ready = false;
    if (s >= 0) {
        if (!quiet)
            g_chain[s].idle_ticks = 0;
        else if (++g_chain[s].idle_ticks >= PT_CHAIN_IDLE_TICKS)
            ready = true;
    }
    pthread_mutex_unlock(&g_chain_lock);
    return ready;
}

/* Drop the entry when PAPPL deletes the printer (tidy-up only: the ID lookup is
 * what makes deletion safe). */
static void pt_delete_cb(pappl_printer_t *printer, pappl_pr_driver_data_t *data)
{
    (void)data;
    pt_chain_drop(papplPrinterGetID(printer));
}

typedef struct { pappl_job_t *self; bool with_processing; bool found; } pt_job_scan;

static void pt_scan_job_cb(pappl_job_t *job, void *data)
{
    pt_job_scan *s = data;
    if (job == s->self)
        return;
    ipp_jstate_t st = papplJobGetState(job);
    if (st == IPP_JSTATE_PENDING || (s->with_processing && st == IPP_JSTATE_PROCESSING))
        s->found = true;
}

/* Is another job actually runnable right now? papplPrinterGetNumberOfActiveJobs()
 * is not a sound signal: it counts held, incomplete and stopped jobs and reads the
 * array unlocked. Iterate instead and look for a distinct PENDING job. */
static bool pt_has_pending_job(pappl_printer_t *printer, pappl_job_t *self)
{
    pt_job_scan s = { .self = self, .with_processing = false, .found = false };
    papplPrinterIterateActiveJobs(printer, pt_scan_job_cb, &s, 1, 0);
    return s.found;
}

/* Idle finaliser (#41): a run that chained out must never stay uncut when the
 * job it chained to never prints. Registered as a 1 s system timer in main.c and
 * called on PAPPL's main loop, so it resolves printer IDs on every tick and never
 * holds g_chain_lock across a PAPPL call. */
bool pt_chain_timer(pappl_system_t *system, void *cb_data)
{
    (void)cb_data;
    int ids[PT_CHAIN_MAX], n = 0;

    pthread_mutex_lock(&g_chain_lock);
    for (int i = 0; i < PT_CHAIN_MAX; i++)
        if (g_chain[i].printer_id)
            ids[n++] = g_chain[i].printer_id;
    pthread_mutex_unlock(&g_chain_lock);

    for (int i = 0; i < n; i++) {
        pappl_printer_t *printer = papplSystemFindPrinter(system, NULL, ids[i], NULL);
        if (!printer) {                  /* printer gone: forget it */
            pt_chain_drop(ids[i]);
            continue;
        }
        pt_job_scan s = { .self = NULL, .with_processing = true, .found = false };
        bool quiet = pt_chain_get(printer) && pt_env_cut_mode(printer) != PT_CUT_NONE &&
                     papplPrinterGetState(printer) == IPP_PSTATE_IDLE;
        if (quiet) {
            papplPrinterIterateActiveJobs(printer, pt_scan_job_cb, &s, 1, 0);
            quiet = !s.found;
        }
        if (!pt_chain_idle_ready(ids[i], quiet))
            continue;

        /* OpenDevice refuses while a job owns the device; that refusal, not the
         * unlocked state observations above, is the definitive race guard. */
        pappl_device_t *dev = papplPrinterOpenDevice(printer);
        if (!dev)
            continue;                    /* retry next tick, keep the chain open */
        pt_op op = { .kind = PT_OP_EJECT };
        papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "finalizing stranded chained run (#41)");
        if (pt_emit_op(dev, &op, 0))
            pt_chain_set(printer, false);  /* clear BEFORE the close, which can start a job */
        papplPrinterCloseDevice(printer);
    }
    return true;   /* stay scheduled */
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
        uint8_t sc[8];
        pt_dev_write(d, sc, pt_cmd_init(sc));
        pt_dev_write(d, sc, pt_cmd_status_request(sc));
        papplDeviceFlush(d);
        uint8_t sbuf[32] = {0};
        ssize_t sn = 0;
        for (int tries = 0; sn == 0 && tries < 10; tries++) {
            struct timespec w = {0, 100000000};  /* 0.1 s */
            nanosleep(&w, NULL);
            sn = pt_usb_read_status(d, sbuf, sizeof sbuf);
        }
        if (sn == 32 && pt_parse_status(sbuf, &live) == 0 && live.tape_px > 0) {
            pappl_media_col_t mc;
            if (media_col_for(live.tape_mm, 10000, dpi, &mc)) {
                live_ok = true;
                papplPrinterSetReadyMedia(printer, 1, &mc);
                papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                       PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY);
            }
        } else if (sn == 32 && pt_parse_status(sbuf, &live) == 0 && live.tape_mm == 0) {
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
        loaded_px = live.tape_px;
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

    /* #41: when a chain is already open the previous job left the tape unfinalised and
     * PAPPL kept the device open (it only closes when active_jobs hits 0, see
     * job-process.c). Re-sending ESC @ mid-chain resets the printer's state and made a
     * PT-2730 report "Tape cassette changed!" and stop accepting data, so skip the
     * init/rasterstart prologue while chained in. The per-label PRINTINFO/SETMODE from the
     * plan still run, which is what keeps autocut working. */
    uint8_t buf[8];
    bool chain_in = pt_chain_get(printer);
    if (chain_in) {
        /* #41 attempt 3, ONE variable: skip only ESC @ (the parser/state reset that is the
         * suspected trigger for "Tape cassette changed"), but KEEP rasterstart - attempt 2
         * dropped both and stalled, which may simply have left the printer out of raster mode.
         *
         * Diagnostic: before writing anything, drain any status block the printer pushed at the
         * chain boundary. Brother reports state changes unsolicited, so if it latched a
         * cassette-changed condition this both reveals the exact error bits and may clear it. */
        const char *duri = papplPrinterGetDeviceURI(printer);
        if (duri && !strncmp(duri, "ptouch://", 9)) {
            uint8_t sb[32] = {0};
            ssize_t sn = pt_usb_read_status(d, sb, sizeof sb);
            if (sn == 32) {
                pt_status st;
                bool ok_parse = (pt_parse_status(sb, &st) == 0);
                papplLogJob(j, PAPPL_LOGLEVEL_INFO,
                            "chain boundary: unsolicited status error=0x%04x tape=%dmm parsed=%d",
                            ok_parse ? st.error : 0xffff, ok_parse ? st.tape_mm : -1, ok_parse);
                papplLogJob(j, PAPPL_LOGLEVEL_INFO,
                            "chain boundary raw: %02x %02x %02x %02x %02x %02x %02x %02x "
                            "%02x %02x %02x %02x", sb[0], sb[1], sb[2], sb[3], sb[4], sb[5],
                            sb[6], sb[7], sb[8], sb[9], sb[10], sb[11]);
            } else {
                papplLogJob(j, PAPPL_LOGLEVEL_INFO,
                            "chain boundary: no pending status (read=%ld)", (long)sn);
            }
        }
        papplLogJob(j, PAPPL_LOGLEVEL_INFO, "chained in: skipping ESC @, keeping rasterstart");
        if (!pt_dev_write(d, buf, pt_cmd_rasterstart(buf, 0)))
            return false;
        papplDeviceFlush(d);
    } else {
        if (!pt_dev_write(d, buf, pt_cmd_init(buf)))
            return false;
        if (!pt_dev_write(d, buf, pt_cmd_rasterstart(buf, 0)))
            return false;
        papplDeviceFlush(d);
    }

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
    b->mode = pt_env_cut_mode(printer);
    b->precut = precut;
    b->n_ops = pt_plan_batch(n, b->tape_mm, b->mode, precut, false, b->ops, PT_OPS_CAP);
    if (b->n_ops < 0) {                      /* cap exceeded (Codex #5) */
        free(b);
        papplJobSetReasons(j, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(j, "cutter plan overflow for %d copies", n);
        return false;
    }
    b->n_labels = n; b->index = 0; b->started = false; b->finalized = false; b->chain_out = false;
    papplJobSetData(j, b);
    return true;
}

static bool r_startpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)o; (void)p;
    pt_batch *b = papplJobGetData(j);
    if (!b) return true;
    b->started = true;   /* a label has begun (Codex #2: enables abnormal-exit detach) */

    /* #41: the chain bit goes out with THIS label's pre-print ops, so the decision
     * must happen before them. If another job is already queued, re-plan the batch
     * with chain_out so the last label is planned like a non-final one (FF, no
     * eject, autocut untouched); only that label's ops change and every earlier
     * label has already been emitted. */
    if (b->index == b->n_labels - 1 && b->mode != PT_CUT_NONE && !b->chain_out) {
        pappl_printer_t *printer = papplJobGetPrinter(j);
        if (pt_has_pending_job(printer, j) && pt_chain_reserve(printer)) {
            int n = pt_plan_batch(b->n_labels, b->tape_mm, b->mode, b->precut, true,
                                  b->ops, PT_OPS_CAP);
            if (n > 0) {
                b->n_ops = n;
                b->chain_out = true;
                papplLogJob(j, PAPPL_LOGLEVEL_INFO,
                            "chaining out to the next queued job: last label ends with FF (#41)");
            }
        }
    }

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
    if (!pt_dev_write(d, buf, pt_cmd_sendraster(buf, packed, 16)))
        return false;
    papplDeviceFlush(d);  /* one transfer per line, like upstream; avoids bursts overrunning the printer buffer */
    return true;
}

static bool r_endpage(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)o; (void)p;
    pt_batch *b = papplJobGetData(j);
    if (!b) return true;
    /* find this label's finish op (the op after its PRINT_PAGE) */
    pt_op_kind finish = PT_OP_FF;
    int seen_print = 0;
    for (int k = 0; k < b->n_ops; k++) {
        if (b->ops[k].page_index != b->index) continue;
        if (seen_print) { finish = b->ops[k].kind; break; }
        if (b->ops[k].kind == PT_OP_PRINT_PAGE) seen_print = 1;
    }
    pt_op op = { .kind = (papplJobIsCanceled(j) || finish == PT_OP_EJECT) ? PT_OP_EJECT : PT_OP_FF };
    pappl_printer_t *printer = papplJobGetPrinter(j);
    bool last = (b->index == b->n_labels - 1);

    /* #41 arm-then-clear: mark the chain open BEFORE any finish write, and clear it
     * only for an EJECT that was actually written. Arming first is what gives the
     * idle timer something to retry when the write fails - merely retaining a flag
     * that a single job never set would leave the tape uncut with nobody to finish
     * it. A chained FF keeps the flag set either way: on success because the next
     * job continues the run, on failure so the timer finalizes it. */
    if (op.kind == PT_OP_EJECT || (last && b->chain_out))
        pt_chain_set(printer, true);

    bool ok = pt_emit_op(d, &op, b->tape_mm);
    if (op.kind == PT_OP_EJECT && ok) {
        pt_chain_set(printer, false);
        b->finalized = true;    /* only a written EJECT counts as finalized */
    }
    if (ok)
        b->index++;             /* a failed finish leaves index short so r_endjob still ejects */
    return ok;
}

/* End of job: safety finalize (detach printed labels on abnormal exit) and free. */
static bool r_endjob(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)o;
    pt_batch *b = papplJobGetData(j);
    /* Safety detach ONLY on abnormal early exit: a label started, the batch did not finish all
     * labels, and no finalizing EJECT ran. This (a) catches a write error during the first
     * label (index 0) (Codex #2), and (b) does NOT eject on normal PT_CUT_NONE completion where
     * index == n_labels and finalized stays false (Codex #3). */
    pappl_printer_t *printer = papplJobGetPrinter(j);
    if (b && b->started && b->index < b->n_labels && !b->finalized) {
        pt_op op = { .kind = PT_OP_EJECT };
        pt_chain_set(printer, true);            /* arm-then-clear (#41) */
        if (pt_emit_op(d, &op, b->tape_mm))     /* detach printed labels; best effort */
            pt_chain_set(printer, false);
    } else if (b && b->chain_out && pt_chain_get(printer) && !pt_has_pending_job(printer, j)) {
        /* #41 late re-check: the job we chained to was cancelled while we printed
         * (this job is still active, hence the self exclusion), so nothing will
         * finish the run. Finalize now while we still own the device. The chain
         * flag gates this: a cancelled job already ejected and cleared it, and
         * ejecting twice would feed and cut blank tape. */
        pt_op op = { .kind = PT_OP_EJECT };
        papplLogJob(j, PAPPL_LOGLEVEL_INFO, "chained-to job is gone; finalizing now (#41)");
        pt_chain_set(printer, true);
        if (pt_emit_op(d, &op, b->tape_mm))
            pt_chain_set(printer, false);
    }
    papplDeviceFlush(d);
    free(b);
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
        return true;
    }

    pappl_device_t *dev = papplPrinterOpenDevice(printer);
    if (!dev) {
        /* present but not openable: a job owns it; clear OFFLINE, leave media */
        papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
        return true;
    }

    uint8_t cmd[8];
    size_t cn = pt_cmd_init(cmd);            /* ESC @ : reset parser so the device answers */
    papplDeviceWrite(dev, cmd, cn);
    size_t rn = pt_cmd_status_request(cmd);  /* ESC i S : status info request */
    papplDeviceWrite(dev, cmd, rn);
    papplDeviceFlush(dev);
    /* The device may not have the 32-byte reply ready on the first read; poll a
     * few times with a short delay, mirroring upstream ptouch_getstatus. Each
     * read is itself bounded (2 s) so the loop cannot hang. */
    uint8_t buf[32] = {0};
    ssize_t n = 0;
    for (int tries = 0; n == 0 && tries < 10; tries++) {
        struct timespec w = {0, 100000000};  /* 0.1 s */
        nanosleep(&w, NULL);
        n = pt_usb_read_status(dev, buf, sizeof buf);
    }
    papplPrinterCloseDevice(printer);

    pt_status st;
    bool parsed = (n == 32 && pt_parse_status(buf, &st) == 0);
    if (parsed && st.tape_px > 0) {
        /* #41: a cassette swapped mid-chain must not inherit the flag - the tape at
         * the head is gone, so treat the run as finished (the next job pre-cuts). */
        pappl_pr_driver_data_t dd;
        papplPrinterGetDriverData(printer, &dd);
        if ((int)(dd.media_ready[0].size_width / 100) != st.tape_mm)
            pt_chain_set(printer, false);
        pappl_media_col_t mc;
        if (media_col_for(st.tape_mm, 10000, dt->dpi, &mc) &&
            papplPrinterSetReadyMedia(printer, 1, &mc))
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                   PAPPL_PREASON_OFFLINE | PAPPL_PREASON_MEDIA_EMPTY);
        else
            papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
    } else if (parsed && st.tape_mm == 0) {
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_OFFLINE);
        pt_chain_set(printer, false);   /* #41: no tape, nothing to finalize */
    } else if (n < 0) {
        /* I/O error: device likely gone; OFFLINE, drop any stale MEDIA_EMPTY */
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_MEDIA_EMPTY);
        pt_chain_set(printer, false);   /* #41 */
    } else {  /* 0 / short / garbled: transient, keep last-known media */
        papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_OFFLINE);
    }
    return true;
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
