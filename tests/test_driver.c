/* Actual PAPPL options/objects and buffered device API, with an injected wire.
 * Include the adapter to exercise its private raster callbacks without a server. */
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <png.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <cups/raster.h>
#include "filter_png.h"
#include "../src/driver.c"

static bool pt_media_confirmed(pappl_printer_t *printer)
{
    pt_printer_state *state = pt_state(printer);
    pthread_mutex_lock(&state->lock);
    bool confirmed = state->media_confirmed;
    pthread_mutex_unlock(&state->lock);
    return confirmed;
}

static unsigned char wire[65536], statuses[256];
static size_t wire_used, status_used, status_size, status_chunk = 32;
/* Reads at these offsets fail, or return nothing for status_gap_reads reads. */
static size_t status_error_at = SIZE_MAX, status_gap_at = SIZE_MAX;
static unsigned status_gap_reads;
/* When set, the status stream repeats so concurrent jobs/cuts never starve. */
static bool status_repeat;
/* Job threads read status and append to the wire while tests swap tape. */
static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static int fail_opcode = -1, fail_advanced = -1;
static bool counting;
static const pt_dev *selected_model;
static char selected_id[128], selected_uri[128];
static struct { size_t rows, ff, eject, modes, cuts, final, infos, inits; } counts;
static bool usb_present = true, can_open = true, fail_next_open;
static ssize_t usb_matches = 1;
static unsigned usb_enumerations, usb_reads, bt_reads, opens;
static bool bt_eof;
static pappl_printer_t *poll_during_job;
static bool busy_poll_checked;
/* Cancel this job during the status read that starts at cancel_read_at. */
static pappl_job_t *cancel_during_read;
static size_t cancel_read_at;
/* Writes of exactly this many bytes fail. */
static size_t fail_length;
/* Controlled pause: the next raster-row write or status read blocks until released. */
static bool pause_raster, raster_paused, raster_go;
static bool pause_status_read, status_read_paused, status_read_go;
static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_cond = PTHREAD_COND_INITIALIZER;
/* Serialize the shared status-message buffer for concurrent cut requests. */
static pthread_mutex_t cut_message_lock = PTHREAD_MUTEX_INITIALIZER;
/* Serialize request-thread progress read by the controlling test thread. */
static pthread_mutex_t request_lock = PTHREAD_MUTEX_INITIALIZER;
static ipp_status_t cut(pappl_printer_t *printer);
static ipp_status_t send_cut(const char *host, pappl_printer_t *printer, ipp_t *request, const char *file);

/* The P710BT removes 14 feed dots from each logical page end before output. */
static bool is_p710bt(void) { return selected_model && selected_model->pid == 0x20af; }
static unsigned tx(unsigned logical) { return logical - (is_p710bt() ? 2u * PT_END_MARGIN_PX : 0u); }

ssize_t pt_usb_count(uint16_t vid, uint16_t pid)
{ assert(vid == selected_model->vid && pid == selected_model->pid); usb_enumerations++; return usb_present ? usb_matches : 0; }
bool pt_usb_present(uint16_t vid, uint16_t pid) { return pt_usb_count(vid, pid) == 1; }
const pt_dev *pt_usb_supported_model(const char *name)
{
    if (!name || !*name) name = "PT-2730";
    return !strcmp(name, "PT-2730") || !strcmp(name, "PT-P710BT") ? pt_lookup_name(name) : NULL;
}
const pt_dev *pt_usb_model_from_uri(const char *uri)
{
    if (!uri) return NULL;
    if (!strcmp(uri, "ptouch://Brother/PT-2730")) return pt_lookup_name("PT-2730");
    if (!strcmp(uri, "ptouch://Brother/PT-P710BT")) return pt_lookup_name("PT-P710BT");
    return NULL;
}
bool pt_usb_is_uri(const char *uri) { return pt_usb_model_from_uri(uri) != NULL; }
static ssize_t feed_status(unsigned *reads, void *buffer, size_t bytes)
{
    pthread_mutex_lock(&pause_lock);
    if (pause_status_read) {
        pause_status_read = false;
        status_read_paused = true;
        pthread_cond_broadcast(&pause_cond);
        while (!status_read_go) pthread_cond_wait(&pause_cond, &pause_lock);
    }
    pthread_mutex_unlock(&pause_lock);
    pthread_mutex_lock(&fixture_lock);
    (*reads)++;
    if (cancel_during_read && status_used == cancel_read_at) {
        papplJobCancel(cancel_during_read); cancel_during_read = NULL;
    }
    ssize_t result = -1;
    if (status_used == status_gap_at && status_gap_reads) {
        status_gap_reads--;
        result = 0;
    } else if (status_used != status_error_at) {
        size_t n = status_size - status_used;
        if (n > bytes) n = bytes;
        if (n > status_chunk) n = status_chunk;
        memcpy(buffer, statuses + status_used, n);
        status_used += n;
        if (status_repeat && status_used >= status_size) status_used = 0;
        result = (ssize_t)n;
    }
    pthread_mutex_unlock(&fixture_lock);
    return result;
}
ssize_t pt_usb_read_status(pappl_device_t *d, const char *uri, void *buffer, size_t bytes)
{ (void)d; (void)uri; return feed_status(&usb_reads, buffer, bytes); }
bool pt_bt_is_uri(const char *uri)
{ return uri && !strcmp(uri, "ptouch-bt://Brother/PT-P710BT/A4:C1:38:AB:CD:EF"); }
ssize_t pt_bt_read_status(pappl_device_t *d, const char *uri, void *buffer, size_t bytes)
{ (void)d; assert(pt_bt_is_uri(uri)); if (bt_eof) { bt_reads++; return -1; } return feed_status(&bt_reads, buffer, bytes); }
static bool dev_open(pappl_device_t *d, const char *uri, const char *name)
{
    (void)d; (void)uri; (void)name; opens++;
    if (fail_next_open) { fail_next_open = false; return false; }
    return can_open;
}
static void dev_close(pappl_device_t *d) { (void)d; }
static ssize_t dev_write(pappl_device_t *d, const void *buffer, size_t n)
{
    (void)d;
    const unsigned char *p = buffer;
    if (n && p[0] == 'G' && poll_during_job && !busy_poll_checked) {
        assert(papplPrinterGetState(poll_during_job) == IPP_PSTATE_PROCESSING);
        assert(pt_media_confirmed(poll_during_job));
        pappl_preason_t before = papplPrinterGetReasons(poll_during_job);
        unsigned reads = usb_reads;
        assert(status_cb(poll_during_job));
        assert(pt_media_confirmed(poll_during_job));
        assert(papplPrinterGetReasons(poll_during_job) == before && usb_reads == reads);
        busy_poll_checked = true;
    }
    if (n && p[0] == 'G') {
        pthread_mutex_lock(&pause_lock);
        if (pause_raster) {
            pause_raster = false;
            raster_paused = true;
            pthread_cond_broadcast(&pause_cond);
            while (!raster_go) pthread_cond_wait(&pause_cond, &pause_lock);
        }
        pthread_mutex_unlock(&pause_lock);
    }
    if (n && p[0] == fail_opcode) return -1;
    if (n && n == fail_length) return -1;
    if (n == 4 && p[0] == 27 && p[1] == 'i' && p[2] == 'K' && p[3] == fail_advanced) return -1;
    if (counting) {
        if (p[0] == 'G') counts.rows++;
        else if (p[0] == 12) counts.ff++;
        else if (p[0] == 26) counts.eject++;
        else if (p[0] == 27 && p[1] == '@') counts.inits++;
        else if (p[0] == 27 && p[1] == 'i' && (p[2] == 'c' || p[2] == 'z')) counts.infos++;
        else if (p[0] == 27 && p[1] == 'i' && p[2] == 'M') {
            counts.modes++; counts.cuts += p[3] == 0x40;
        } else if (p[0] == 27 && p[1] == 'i' && p[2] == 'K' && p[3] == 8) {
            counts.final++;
            assert(counts.rows == counts.infos * tx(180));
        }
        return (ssize_t)n;
    }
    pthread_mutex_lock(&fixture_lock);
    assert(wire_used + n <= sizeof wire);
    memcpy(wire + wire_used, buffer, n);
    wire_used += n;
    pthread_mutex_unlock(&fixture_lock);
    return (ssize_t)n;
}
static pappl_preason_t dev_status(pappl_device_t *d) { (void)d; return PAPPL_PREASON_NONE; }
static char *dev_id(pappl_device_t *d, char *buffer, size_t n)
{ (void)d; snprintf(buffer, n, "%s", selected_id); return buffer; }

static void reset_wire(void) { wire_used = 0; fail_opcode = fail_advanced = -1; fail_length = 0; }
static void ready_status(int error, size_t chunk)
{
    memset(statuses, 0, sizeof statuses);
    /* Printing, then completed-but-still-printing, then receiving-ready. */
    for (int i = 0; i < 3; i++) {
        unsigned char *s = statuses + i * 32;
        /* Recorded P710BT snapshot from retained p710bt-rectangles/live.log;
         * phase mutations below reproduce its actual printing/completed/ready replies. */
        static const unsigned char p710bt_snapshot[32] = {
            0x80, 0x20, 0x42, 0x30, 0x76, 0x30, 0x00, 0x00,
            0x00, 0x00, 0x0c, 0x01, 0x00, 0x00, 0x00, 0x40,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        if (selected_model->pid == 0x20af) memcpy(s, p710bt_snapshot, sizeof p710bt_snapshot);
        s[0] = 0x80; s[1] = 0x20; s[10] = 12;
        s[18] = i == 1 ? 1 : 6;
        s[19] = i == 2 ? 0 : 1;
        if (i == 2) s[8] = (unsigned char)error;
    }
    status_used = 0; status_size = 96; status_chunk = chunk;
}
/* Queue one clean snapshot per tape, optionally followed by one print's ready phases. */
static void queue_tapes(const int *tapes, size_t n, bool phases)
{
    unsigned char ready[96];
    pthread_mutex_lock(&fixture_lock);
    ready_status(0, 32); memcpy(ready, statuses, sizeof ready);
    for (size_t i = 0; i < n; i++) {
        unsigned char *s = statuses + i * 32;
        memcpy(s, ready, 32); s[10] = (unsigned char)tapes[i]; s[18] = s[19] = 0;
    }
    if (phases) memcpy(statuses + n * 32, ready, sizeof ready);
    status_size = n * 32 + (phases ? sizeof ready : 0);
    status_error_at = status_gap_at = SIZE_MAX;
    pthread_mutex_unlock(&fixture_lock);
}
/* Count the status requests that start the wire; *other receives the bytes after them. */
static size_t status_requests(size_t *other)
{
    pthread_mutex_lock(&fixture_lock);
    size_t n = 0;
    while ((n + 1) * 3 <= wire_used && !memcmp(wire + n * 3, "\x1b\x69\x53", 3)) n++;
    *other = wire_used - n * 3;
    pthread_mutex_unlock(&fixture_lock);
    return n;
}
static int sink_listener;
static http_t *http;
static int port;
static void *run_server(void *system) { papplSystemRun(system); return NULL; }
static bool fixed_status(pappl_printer_t *p) { (void)p; return true; }
static bool fixture_driver(pappl_system_t *s, const char *name, const char *uri,
                           const char *id, pappl_pr_driver_data_t *data, ipp_t **attrs, void *cb)
{
    bool ok = pt_driver_cb(s, name, uri, id, data, attrs, cb);
    data->status_cb = fixed_status; /* IPP validation must not poll absent hardware. */
    return ok;
}
static void add_finishing(ipp_t *request, const char *finishing, bool collection)
{
    if (finishing) {
        if (collection) {
            ipp_t *col = ippNew();
            if (!strcmp(finishing, "none") || !strcmp(finishing, "{finishing-template=none}"))
                ippAddOutOfBand(request, IPP_TAG_JOB, IPP_TAG_NOVALUE, "finishings-col");
            else {
                const char *name = !strcmp(finishing, "{finishing-template=trim}") ? "trim" : finishing;
                ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, name);
                ippAddCollection(request, IPP_TAG_JOB, "finishings-col", col);
            }
            ippDelete(col);
        } else if (!strcmp(finishing, "combined")) {
            const int values[] = {3, 60, 63};
            ippAddIntegers(request, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", 3, values);
        } else {
            ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings",
                          !strcmp(finishing, "trim") ? 11 : !strcmp(finishing, "trim-after-pages") ? 60 :
                          !strcmp(finishing, "trim-after-job") ? 63 : 3);
        }
    }
}

static pappl_job_t *media_job(pappl_printer_t *p, const char *finishing, int copies,
                              bool collection, ipp_t *media)
{
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(p));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_CREATE_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-hold-until", NULL, "indefinite");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", copies);
    add_finishing(request, finishing, collection);
    if (media) ippAddCollection(request, IPP_TAG_JOB, "media-col", media);
    ipp_t *response = cupsDoRequest(http, request, resource);
    if (!response || ippGetStatusCode(response) > IPP_STATUS_OK_EVENTS_COMPLETE)
        fprintf(stderr, "Create-Job failed: %s (%s)\n", cupsLastErrorString(), resource);
    assert(response && ippGetStatusCode(response) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);
    ippDelete(response);
    pappl_job_t *j = papplPrinterFindJob(p, id); assert(j);
    return j;
}
static pappl_job_t *job(pappl_printer_t *p, const char *finishing, int copies, bool collection)
{
    return media_job(p, finishing, copies, collection, NULL);
}
/* A named size and margins; omitted members (NULL, -1) are inherited from defaults. */
static ipp_t *media_col(const char *name, int left, int right)
{
    ipp_t *col = ippNew();
    if (name) ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-size-name", NULL, name);
    if (left >= 0) ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-left-margin", left);
    if (right >= 0) ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-right-margin", right);
    return col;
}
static pappl_pr_options_t *begin(pappl_job_t *j, pappl_device_t *d)
{
    pappl_pr_options_t *o = papplJobCreatePrintOptions(j, 1, false); assert(o);
    assert(r_startjob(j, o, d));
    return o;
}
static bool page_test(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    unsigned char pixels[32] = {0};
    unsigned y = is_p710bt() ? PT_END_MARGIN_PX : 0; /* first transmitted row */
    if (!r_startpage(j, o, d, 0) || !r_writeline(j, o, d, y, pixels)) return false;
    size_t before = wire_used;
    bool result = r_endpage(j, o, d, 0);
    assert(wire_used == before); /* Page completion cannot guess the batch end. */
    return result;
}
static void end(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    assert(r_endjob(j, o, d)); assert(!papplJobGetData(j));
    size_t before = wire_used;
    assert(r_endjob(j, o, d)); assert(wire_used == before);
    papplJobDeletePrintOptions(o);
    papplJobCancel(j); /* A driven fixture must not linger as accepted print work. */
}
/* Length-aware decoding keeps command-looking raster bytes inside their row. */
static size_t command_size(size_t pos)
{
    assert(pos < wire_used);
    unsigned char op = wire[pos];
    bool p710bt = selected_model->pid == 0x20af;
    size_t n;
    if (op == 0) {
        assert(p710bt); n = 100;
        assert(pos + n <= wire_used);
        for (size_t i = 0; i < n; i++) assert(wire[pos + i] == 0);
    } else if (op == 'G') {
        assert(pos + 3 <= wire_used);
        unsigned length = wire[pos + 1] | (unsigned)wire[pos + 2] << 8;
        assert(length == (p710bt ? 17u : 16u));
        n = 3 + length;
        if (p710bt) assert(wire[pos + 3] == 15); /* Complete 16-byte literal run. */
    } else if (op == 'M') {
        assert(p710bt && wire[pos + 1] == 2); n = 2;
    } else if (op == 12 || op == 26) n = 1;
    else {
        assert(op == 27);
        if (wire[pos + 1] == '@') n = 2;
        else {
            assert(wire[pos + 1] == 'i');
            unsigned char sub = wire[pos + 2];
            if (sub == 'c' || sub == 'z') {
                assert(sub == (p710bt ? 'z' : 'c'));
                assert(wire[pos + 3] == 0x84 && wire[pos + 4] == 0 && wire[pos + 6] == 0);
                n = p710bt ? 13 : 8;
                if (p710bt) assert(wire[pos + 11] <= 1 && wire[pos + 12] == 0);
            } else if (sub == 'd') {
                assert(p710bt && wire[pos + 3] == 14 && wire[pos + 4] == 0); n = 5;
            } else if (sub == 'S') n = 3;
            else {
                assert(sub == 'M' || sub == 'K' || sub == 'R' || sub == 'a' || sub == '!');
                if (sub == 'R' || sub == 'a') assert(sub == (p710bt ? 'a' : 'R') && wire[pos + 3] == 1);
                if (sub == '!') assert(p710bt && wire[pos + 3] == 0);
                n = 4;
            }
        }
    }
    assert(pos + n <= wire_used);
    return n;
}

static void expect_wire(int inits, int rows, int ffs, int ejects)
{
    int actual_init = 0, actual_rows = 0, actual_ff = 0, actual_eject = 0;
    bool fresh = false;
    for (size_t pos = 0; pos < wire_used;) {
        size_t n = command_size(pos);
        if (wire[pos] == 'G') actual_rows++;
        else if (wire[pos] == 12) actual_ff++;
        else if (wire[pos] == 26) actual_eject++;
        else if (wire[pos] == 27 && wire[pos + 1] == '@') { actual_init++; fresh = true; }
        else if (wire[pos] == 27 && wire[pos + 1] == 'i') {
            if (wire[pos + 2] == 'M') assert(wire[pos + 3] == 0x40 || wire[pos + 3] == 0);
            if (wire[pos + 2] == 'z') {
                assert(wire[pos + 11] == (fresh ? 0 : 1)); fresh = false;
            }
        }
        pos += n;
    }
    if (actual_init != inits || actual_rows != rows || actual_ff != ffs || actual_eject != ejects)
        fprintf(stderr, "wire init/rows/FF/EJECT: %d/%d/%d/%d expected %d/%d/%d/%d\n",
                actual_init, actual_rows, actual_ff, actual_eject, inits, rows, ffs, ejects);
    assert(actual_init == inits && actual_rows == rows && actual_ff == ffs && actual_eject == ejects);
}

/* Verify what the wire requests, including the first-only mandatory precut
 * and late finalization after the last label's raster. */
static void expect_policy(unsigned labels, const unsigned *lengths, pt_cut_mode mode)
{
    unsigned infos = 0, modes = 0, ff = 0, eject = 0, advanced_final = 0, rows = 0;
    for (size_t pos = 0; pos < wire_used;) {
        size_t n = command_size(pos);
        if (wire[pos] == 'G') rows++;
        else if (wire[pos] == 12) ff++;
        else if (wire[pos] == 26) eject++;
        else if (wire[pos] == 27 && wire[pos + 1] == 'i') {
            unsigned char op = wire[pos + 2], arg = wire[pos + 3];
            if (op == 'c' || op == 'z') {
                if (infos) assert(rows == tx(lengths[infos - 1]));
                if (op == 'z') {
                    unsigned height = wire[pos + 7] | (unsigned)wire[pos + 8] << 8 |
                                      (unsigned)wire[pos + 9] << 16 | (unsigned)wire[pos + 10] << 24;
                    assert(infos < labels && height == tx(lengths[infos]));
                }
                rows = 0; infos++;
            } else if (op == 'M') {
                bool cut = modes == 0 || mode == PT_CUT_EACH || mode == PT_CUT_CHAIN;
                assert(arg == (cut ? 0x40 : 0)); modes++;
            } else if (op == 'K') {
                assert(arg == 0 || arg == 8);
                if (arg == 8) {
                    assert(infos == labels && rows == tx(lengths[labels - 1]));
                    advanced_final++;
                }
            }
        }
        pos += n;
    }
    assert(infos == labels && modes == labels && rows == tx(lengths[labels - 1]));
    bool final = mode == PT_CUT_EACH || mode == PT_CUT_END;
    assert(ff == labels - (unsigned)final && eject == (unsigned)final && advanced_final == (unsigned)final);
}

static void *socket_sink(void *arg)
{
    int listener = *(int *)arg;
    int peer = accept(listener, NULL, NULL); assert(peer >= 0);
    ssize_t n;
    while ((n = read(peer, wire + wire_used, sizeof wire - wire_used)) > 0)
        wire_used += (size_t)n;
    assert(n == 0); close(peer);
    return NULL;
}

/* A 180-row grayscale PNG with two black columns, in a new temporary file. */
static void write_png(char *filename, int width, int black_a, int black_b)
{
    int fd = mkstemp(filename); assert(fd >= 0);
    FILE *file = fdopen(fd, "wb"); assert(file);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png); assert(png && info);
    assert(!setjmp(png_jmpbuf(png)));
    png_init_io(png, file);
    png_set_IHDR(png, info, (png_uint_32)width, 180, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    unsigned char pixels[128]; assert(width <= (int)sizeof pixels);
    memset(pixels, 255, sizeof pixels);
    int end = is_p710bt() ? PT_END_MARGIN_PX : 0;
    for (int i = 0; i < 180; i++) {
        pixels[black_a] = pixels[black_b] = (i >= end && i < 180 - end) ? 0 : 255;
        png_write_row(png, pixels);
    }
    png_write_end(png, info); png_destroy_write_struct(&png, &info); fclose(file);
}

/* Real IPP upload, Release-Job scheduling, PNG filter, and TCP device output.
 * Only media/status are fixed; every printing callback is the production one. */
static pappl_job_t *upload_image(pappl_printer_t *p, const char *finishing, int copies,
                                 bool collection, ipp_t *media, int width, int black_a, int black_b)
{
    bool malformed = finishing && !strcmp(finishing, "malformed");
    pappl_job_t *j = media_job(p, finishing, copies, collection, media);
    char filename[] = "/tmp/ptouch-finishing-png-XXXXXX";
    write_png(filename, width, black_a, black_b);
    if (malformed) {
        FILE *file = fopen(filename, "wb"); assert(file);
        assert(fwrite("bad PNG", 1, 7, file) == 7); fclose(file);
    }
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(p));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_SEND_DOCUMENT);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", papplJobGetID(j));
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/png");
    ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", true);
    ipp_t *response = cupsDoFileRequest(http, request, resource, filename); unlink(filename);
    assert(response && ippGetStatusCode(response) <= IPP_STATUS_OK_EVENTS_COMPLETE); ippDelete(response);

    return j;
}

static pappl_job_t *upload_png(pappl_printer_t *p, const char *finishing, int copies,
                               bool collection)
{
    return upload_image(p, finishing, copies, collection, NULL, 85, 40, 41);
}

static void png_socket_case(pappl_printer_t *p, const char *finishing, int copies,
                            bool collection, int inits, int ffs, int ejects)
{
    bool malformed = finishing && !strcmp(finishing, "malformed");
    pappl_job_t *j = upload_png(p, finishing, copies, collection);
    reset_wire();
    pthread_t sink; assert(!pthread_create(&sink, NULL, socket_sink, &sink_listener));
    assert(papplJobRelease(j, "test"));
    for (int i = 0; i < 1000 && papplJobGetState(j) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(j) == (malformed ? IPP_JSTATE_ABORTED : IPP_JSTATE_COMPLETED));
    pthread_join(sink, NULL);
    expect_wire(inits, malformed ? 0 : tx(180) * copies, ffs, ejects);
}

static const unsigned raster_lengths[] = {180, 360, 90};

/* 12 mm pages; damage 1 appends a stray byte, 2 truncates the last row. */
static void write_raster(char *path, int pages, int bits, cups_mode_t mode, int damage)
{
    const unsigned *lengths = raster_lengths;
    int fd = mkstemp(path); assert(fd >= 0);
    cups_raster_t *raster = cupsRasterOpen(fd, mode); assert(raster);
    for (int i = 0; i < pages; i++) {
        cups_page_header2_t header;
        pwg_media_t media = {"custom", NULL, NULL, 1200, (int)lengths[i] * 2540 / 180};
        assert(cupsRasterInitPWGHeader(&header, &media, bits == 1 ? "black_1" : "black_8",
                                      180, 180, "one-sided", "normal"));
        if (mode == CUPS_RASTER_WRITE_APPLE) header.cupsColorSpace = CUPS_CSPACE_SW;
        header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 0;
        assert(header.cupsHeight == lengths[i] && cupsRasterWriteHeader2(raster, &header));
        unsigned char row[128]; assert(header.cupsBytesPerLine <= sizeof row);
        memset(row, mode == CUPS_RASTER_WRITE_APPLE ? 255 : 0, sizeof row);
        for (unsigned y = 0; y < header.cupsHeight; y++) {
            /* A distinct final row flushes the preceding compressed blank run.
             * Column zero is outside the printable window, so the row remains
             * printable-blank when its packet is truncated below. */
            if (damage == 2 && i == pages - 1 && y == header.cupsHeight - 1)
                row[0] ^= 0x80;
            assert(cupsRasterWritePixels(raster, row, header.cupsBytesPerLine) == header.cupsBytesPerLine);
        }
    }
    cupsRasterClose(raster);
    if (damage == 1) {
        assert(lseek(fd, 0, SEEK_END) >= 0);
        assert(write(fd, "x", 1) == 1);
    } else if (damage == 2) {
        off_t size = lseek(fd, 0, SEEK_END); assert(size > 3);
        assert(!ftruncate(fd, size - 3));
    }
    close(fd);
}

static void raster_socket_case(pappl_printer_t *printer, const char *finishing,
                                bool collection, int pages, int copies, int bits, cups_mode_t mode,
                                int damage)
{
    const unsigned *lengths = raster_lengths;
    char path[] = "/tmp/ptouch-batch-raster-XXXXXX";
    write_raster(path, pages, bits, mode, damage);
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL,
                 mode == CUPS_RASTER_WRITE_APPLE ? "image/urf" : "image/pwg-raster");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", copies);
    add_finishing(request, finishing, collection);
    reset_wire();
    pthread_t sink;
    if (copies == 1) assert(!pthread_create(&sink, NULL, socket_sink, &sink_listener));
    ipp_t *reply = cupsDoFileRequest(http, request, resource, path); unlink(path);
    if (copies != 1) {
        assert(reply && ippGetStatusCode(reply) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
        assert(wire_used == 0 && pt_run_get(printer) == PT_RUN_CLOSED);
        ippDelete(reply);
        return;
    }
    if (!reply || ippGetStatusCode(reply) > IPP_STATUS_OK_EVENTS_COMPLETE)
        fprintf(stderr, "Raster request rejected: %s\n", cupsLastErrorString());
    assert(reply && ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0);
    ippDelete(reply); pthread_join(sink, NULL);
    pappl_job_t *job = papplPrinterFindJob(printer, id); assert(job);
    for (unsigned i = 0; i < 1000 && papplJobGetState(job) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    bool aborted = damage != 0;
    if (papplJobGetState(job) != (aborted ? IPP_JSTATE_ABORTED : IPP_JSTATE_COMPLETED))
        fprintf(stderr, "Raster %s pages%d bits%d mode%d: state%d message%s\n",
                finishing, pages, bits, mode, papplJobGetState(job), papplJobGetMessage(job));
    assert(papplJobGetState(job) == (aborted ? IPP_JSTATE_ABORTED : IPP_JSTATE_COMPLETED));
    if (damage) {
        /* damage 1 appends a trailing byte after a complete page; damage 2
         * truncates the stream inside the last page's blank trailing rows. Both
         * fail as incomplete input without finishing. */
        if (damage == 1)
            expect_wire(1, tx(180), 0, 0);
        else if (is_p710bt())
            expect_wire(1, tx(180) + tx(360), 1, 0);
        else
            expect_wire(1, 539, 1, 0);
        assert(pt_run_get(printer) == PT_RUN_UNCERTAIN);
    } else {
        pt_cut_mode selected = !strcmp(finishing, "none") ? PT_CUT_NONE :
                               !strcmp(finishing, "ptouch-chain") ? PT_CUT_CHAIN :
                               !strcmp(finishing, "trim-after-job") ? PT_CUT_END : PT_CUT_EACH;
        expect_policy((unsigned)pages, lengths, selected);
    }
}

/* Render one raster page and reconstruct its physical length: the transmitted
 * print-area rows plus the feed dots the model restores at each end. The marker
 * row must land at the same physical row it was submitted at. */
static void raster_length_case(pappl_printer_t *printer, unsigned height)
{
    char path[] = "/tmp/ptouch-length-raster-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    cups_raster_t *raster = cupsRasterOpen(fd, CUPS_RASTER_WRITE_PWG); assert(raster);
    cups_page_header2_t header;
    /* Round centimm up so conversion back to dots retains the requested row. */
    pwg_media_t media = {"custom", NULL, NULL, 1200, (int)((height * 2540 + 179) / 180)};
    assert(cupsRasterInitPWGHeader(&header, &media, "black_1", 180, 180, "one-sided", "normal"));
    header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 0;
    assert(header.cupsHeight == height && cupsRasterWriteHeader2(raster, &header));
    unsigned char row[128] = {0}; assert(header.cupsBytesPerLine <= sizeof row);
    for (unsigned y = 0; y < height; y++) {
        memset(row, 0, header.cupsBytesPerLine);
        if (y == 100) row[5] = 0x80; /* input dot 40, inside the imageable window */
        /* A 12 mm page is wider than its window. Bits outside the printable area
         * and in byte padding are not ink, so a P710BT end row carrying only
         * those bits still validates blank and is replaced by feed. */
        if (is_p710bt() && (y < PT_END_MARGIN_PX || y + PT_END_MARGIN_PX >= height)) {
            row[0] = 0x80;
            row[header.cupsBytesPerLine - 1] = 0xff;
        }
        assert(cupsRasterWritePixels(raster, row, header.cupsBytesPerLine) == header.cupsBytesPerLine);
    }
    cupsRasterClose(raster); close(fd);
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/pwg-raster");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", 1);
    add_finishing(request, "none", false);
    reset_wire();
    pthread_t sink; assert(!pthread_create(&sink, NULL, socket_sink, &sink_listener));
    ipp_t *reply = cupsDoFileRequest(http, request, resource, path); unlink(path);
    assert(reply && ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0); ippDelete(reply);
    pthread_join(sink, NULL);
    pappl_job_t *job = papplPrinterFindJob(printer, id); assert(job);
    for (unsigned i = 0; i < 1000 && papplJobGetState(job) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(job) == IPP_JSTATE_COMPLETED);
    unsigned rows = 0; int marker = -1;
    for (size_t pos = 0; pos < wire_used;) {
        size_t n = command_size(pos);
        if (wire[pos] == 'G') {
            const unsigned char *packed = wire + pos + (is_p710bt() ? 4 : 3);
            bool ink = false;
            for (int b = 0; b < 16; b++) ink |= packed[b] != 0;
            if (ink) marker = (int)rows;
            rows++;
        }
        pos += n;
    }
    unsigned feed = is_p710bt() ? PT_END_MARGIN_PX : 0;
    assert(rows == height - 2 * feed);
    assert(marker == (int)(100 - feed));
}

/* Ink inside a row the P710BT replaces with feed fails; a leading end row fails
 * before any raster leaves, a trailing one after the interior rows are sent.
 * Either way the run is left uncertain for the documented recovery contract. */
static void raster_end_ink_case(pappl_printer_t *printer, unsigned ink_row)
{
    char path[] = "/tmp/ptouch-endink-raster-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    cups_raster_t *raster = cupsRasterOpen(fd, CUPS_RASTER_WRITE_PWG); assert(raster);
    cups_page_header2_t header;
    pwg_media_t media = {"custom", NULL, NULL, 1200, 180 * 2540 / 180};
    assert(cupsRasterInitPWGHeader(&header, &media, "black_1", 180, 180, "one-sided", "normal"));
    header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 0;
    assert(header.cupsHeight == 180 && cupsRasterWriteHeader2(raster, &header));
    unsigned char row[128] = {0}; assert(header.cupsBytesPerLine <= sizeof row);
    for (unsigned y = 0; y < 180; y++) {
        row[5] = y == ink_row ? 0x80 : 0;
        assert(cupsRasterWritePixels(raster, row, header.cupsBytesPerLine) == header.cupsBytesPerLine);
    }
    cupsRasterClose(raster); close(fd);
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/pwg-raster");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", 1);
    add_finishing(request, "none", false);
    reset_wire();
    pthread_t sink; assert(!pthread_create(&sink, NULL, socket_sink, &sink_listener));
    ipp_t *reply = cupsDoFileRequest(http, request, resource, path); unlink(path);
    assert(reply && ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0); ippDelete(reply);
    pthread_join(sink, NULL);
    pappl_job_t *job = papplPrinterFindJob(printer, id); assert(job);
    for (unsigned i = 0; i < 1000 && papplJobGetState(job) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(job) == IPP_JSTATE_ABORTED);
    expect_wire(1, ink_row < PT_END_MARGIN_PX ? 0 : tx(180), 0, 0);
    assert(pt_run_get(printer) == PT_RUN_UNCERTAIN);
}

static void counting_png_case(pappl_printer_t *printer, const char *finishing,
                               int copies, pt_cut_mode mode)
{
    pappl_job_t *j = upload_png(printer, finishing, copies, false);
    memset(&counts, 0, sizeof counts); counting = true;
    assert(papplJobRelease(j, "test"));
    for (unsigned i = 0; i < 2000 && papplJobGetState(j) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(j) == IPP_JSTATE_COMPLETED);
    counting = false;
    bool between = mode == PT_CUT_EACH || mode == PT_CUT_CHAIN;
    bool final = mode == PT_CUT_EACH || mode == PT_CUT_END;
    assert(counts.rows == tx(180) * copies && counts.infos == (unsigned)copies && counts.modes == (unsigned)copies);
    assert(counts.cuts == (between ? (unsigned)copies : 1));
    assert(counts.ff == (unsigned)copies - (unsigned)final && counts.eject == (unsigned)final && counts.final == (unsigned)final);
}

static ipp_t *printer_request(pappl_printer_t *printer, ipp_op_t op)
{
    char uri[256];
    snprintf(uri, sizeof uri, "ipp://localhost:%d/ipp/print/%s", port, papplPrinterGetName(printer));
    ipp_t *request = ippNewRequest(op);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    return request;
}

static ipp_t *printer_response(pappl_printer_t *printer, ipp_t *request)
{
    char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    ipp_t *reply = cupsDoRequest(http, request, resource); assert(reply); return reply;
}

static void check_capabilities(pappl_printer_t *printer)
{
    const char *formats[] = {"image/png", "image/pwg-raster", "image/urf"};
    for (unsigned f = 0; f < 3; f++) {
        ipp_t *q = printer_request(printer, IPP_OP_GET_PRINTER_ATTRIBUTES);
        ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, formats[f]);
        ipp_t *reply = printer_response(printer, q);
        ipp_attribute_t *copies = ippFindAttribute(reply, "copies-supported", IPP_TAG_RANGE);
        int high; assert(copies && ippGetRange(copies, 0, &high) == 1 && high == (f ? 1 : 999));
        ipp_attribute_t *supported = ippFindAttribute(reply, "finishings-supported", IPP_TAG_ENUM);
        const int expected[] = {3, 11, 60, 63};
        assert(supported && ippGetCount(supported) == 4);
        for (int i = 0; i < 4; i++) assert(ippContainsInteger(supported, expected[i]));
        unsigned names = 0;
        for (ipp_attribute_t *a = ippFirstAttribute(reply); a; a = ippNextAttribute(reply))
            if (ippGetName(a) && !strcmp(ippGetName(a), "finishings-default")) names++;
        assert(names == 1);
        ippDelete(reply);
    }
}

static void check_media_database(pappl_printer_t *printer)
{
    ipp_t *request = printer_request(printer, IPP_OP_GET_PRINTER_ATTRIBUTES);
    const char *requested[] = {"media-col-database", "media-size-supported", "media-left-margin-supported", "media-right-margin-supported"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", 4, NULL, requested);
    ipp_t *reply = printer_response(printer, request);
    ipp_attribute_t *db = ippFindAttribute(reply, "media-col-database", IPP_TAG_BEGIN_COLLECTION);
    ipp_attribute_t *sizes = ippFindAttribute(reply, "media-size-supported", IPP_TAG_BEGIN_COLLECTION);
    assert(db && ippGetCount(db) == 20 && sizes && ippGetCount(sizes) == 5);
    unsigned db_names = 0;
    for (ipp_attribute_t *a = ippFirstAttribute(reply); a; a = ippNextAttribute(reply))
        if (ippGetName(a) && !strcmp(ippGetName(a), "media-col-database")) db_names++;
    assert(db_names == 1);
    const int widths[] = {6, 9, 12, 18, 24};
    for (unsigned w = 0; w < 5; w++) {
        pt_geometry g; assert(pt_tape_geometry(widths[w], &g));
        unsigned fixed = 0, roll = 0;
        for (int i = 0; i < ippGetCount(db); i++) {
            ipp_t *col = ippGetCollection(db, i);
            ipp_t *size = ippGetCollection(ippFindAttribute(col, "media-size", IPP_TAG_BEGIN_COLLECTION), 0);
            ipp_attribute_t *x = ippFindAttribute(size, "x-dimension", IPP_TAG_ZERO);
            if (!ippContainsInteger(x, widths[w] * 100)) continue;
            assert(ippGetInteger(ippFindAttribute(col, "media-left-margin", IPP_TAG_INTEGER), 0) == g.left_margin_centimm);
            assert(ippGetInteger(ippFindAttribute(col, "media-right-margin", IPP_TAG_INTEGER), 0) == g.right_margin_centimm);
            ipp_attribute_t *length = ippFindAttribute(size, "y-dimension", IPP_TAG_ZERO); assert(length);
            if (ippGetValueTag(length) == IPP_TAG_RANGE) {
                int high; assert(ippGetRange(length, 0, &high) == PT_ROLL_MIN_CENTIMM && high == PT_ROLL_MAX_CENTIMM); roll++;
                assert(ippGetValueTag(x) == IPP_TAG_RANGE);
                assert(ippGetRange(x, 0, &high) == widths[w] * 100 - 1 && high == widths[w] * 100 + 1);
            } else {
                int value = ippGetInteger(length, 0);
                assert(value == 2500 || value == 5000 || value == 10000); fixed++;
            }
        }
        assert(fixed == 3 && roll == 1);
        ipp_t *range = ippGetCollection(sizes, w);
        int high;
        assert(ippGetRange(ippFindAttribute(range, "x-dimension", IPP_TAG_RANGE), 0, &high) == widths[w] * 100 - 1 && high == widths[w] * 100 + 1);
        assert(ippGetRange(ippFindAttribute(range, "y-dimension", IPP_TAG_RANGE), 0, &high) == PT_ROLL_MIN_CENTIMM && high == PT_ROLL_MAX_CENTIMM);
        for (int delta = -2; delta <= 2; delta++) {
            ipp_t *q = printer_request(printer, IPP_OP_VALIDATE_JOB);
            ippAddBoolean(q, IPP_TAG_OPERATION, "ipp-attribute-fidelity", true);
            ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/png");
            ipp_t *col = ippNew(), *size = ippNew();
            ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", widths[w] * 100 + delta);
            ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", 4000);
            ippAddCollection(col, IPP_TAG_ZERO, "media-size", size);
            ippAddCollection(q, IPP_TAG_JOB, "media-col", col); ippDelete(size); ippDelete(col);
            char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
            ipp_t *response = cupsDoRequest(http, q, resource); assert(response);
            assert(ippGetStatusCode(response) == (abs(delta) <= 1 ? IPP_STATUS_OK : IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES));
            ippDelete(response);
        }
    }
    ippDelete(reply);
}

static void set_default(pappl_printer_t *printer, int value, const char *template)
{
    ipp_t *q = printer_request(printer, IPP_OP_SET_PRINTER_ATTRIBUTES);
    if (template) {
        ipp_t *col = ippNew();
        ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, template);
        ippAddCollection(q, IPP_TAG_PRINTER, "finishings-col-default", col); ippDelete(col);
    } else ippAddInteger(q, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-default", value);
    ipp_t *reply = printer_response(printer, q);
    assert(ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE); ippDelete(reply);
    q = printer_request(printer, IPP_OP_GET_PRINTER_ATTRIBUTES);
    reply = printer_response(printer, q);
    ipp_attribute_t *def = ippFindAttribute(reply, "finishings-default", IPP_TAG_ENUM);
    assert(def && ippGetInteger(def, 0) == value);
    def = ippFindAttribute(reply, "finishings-col-default", IPP_TAG_ZERO); assert(def);
    if (value == 3) assert(ippGetValueTag(def) == IPP_TAG_NOVALUE);
    else {
        const char *name = ippGetString(ippFindAttribute(ippGetCollection(def, 0), "finishing-template", IPP_TAG_KEYWORD), 0, NULL);
        assert(name && !strcmp(name, template ? template : value == 11 ? "trim" : value == 60 ? "trim-after-pages" : "trim-after-job"));
    }
    ippDelete(reply);
}

static void reject_finishing_requests(pappl_printer_t *printer)
{
    for (unsigned fidelity = 0; fidelity < 2; fidelity++) for (unsigned i = 0; i < 7; i++) {
        ipp_t *q = printer_request(printer, i == 6 ? IPP_OP_SET_PRINTER_ATTRIBUTES : IPP_OP_CREATE_JOB);
        ippAddBoolean(q, IPP_TAG_OPERATION, "ipp-attribute-fidelity", fidelity);
        if (i == 0) {
            add_finishing(q, "none", false); add_finishing(q, "trim", true);
        } else if (i == 1) add_finishing(q, "unknown-template", true);
        else if (i == 2) ippAddInteger(q, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", 62);
        else if (i == 3) {
            int values[] = {60, 62}; ippAddIntegers(q, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", 2, values);
        } else if (i == 4) {
            ipp_t *cols[] = {ippNew(), ippNew()};
            ippAddString(cols[0], IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, "trim-after-pages");
            ippAddString(cols[1], IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, "trim-after-job");
            ippAddCollections(q, IPP_TAG_JOB, "finishings-col", 2, (const ipp_t **)cols);
            ippDelete(cols[0]); ippDelete(cols[1]);
        } else if (i == 5) {
            ipp_t *col = ippNew();
            ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, "trim");
            ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_ENUM, "trimming", 1);
            ippAddCollection(q, IPP_TAG_JOB, "finishings-col", col); ippDelete(col);
        } else {
            ipp_t *col = ippNew();
            ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, "ptouch-chain");
            ippAddCollection(q, IPP_TAG_PRINTER, "finishings-col-default", col); ippDelete(col);
        }
        ipp_t *reply = printer_response(printer, q);
        assert(ippGetStatusCode(reply) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
        ippDelete(reply);
    }
}

static void reject_unsupported_finishing(pappl_printer_t *p)
{
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(p));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_CREATE_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddBoolean(request, IPP_TAG_OPERATION, "ipp-attribute-fidelity", true);
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", IPP_FINISHINGS_STAPLE);
    ipp_t *response = cupsDoRequest(http, request, resource);
    assert(response && ippGetStatusCode(response) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
    ippDelete(response);
}

/* Two labels with deferred finalization; returns the complete wire. */
static size_t transport_batch(pappl_printer_t *printer, const char *finishing, pappl_device_t *d,
                              unsigned char *out)
{
    pt_run_set(printer, PT_RUN_CLOSED); reset_wire();
    ready_status(0, 7); statuses[18] = statuses[19] = 0; status_size = 32;
    pappl_job_t *j = job(printer, finishing, 1, !strcmp(finishing, "ptouch-chain"));
    pappl_pr_options_t *o = begin(j, d);
    assert(page_test(j, o, d));
    ready_status(0, 7); assert(page_test(j, o, d));
    ready_status(0, 7); end(j, o, d);
    memcpy(out, wire, wire_used);
    return wire_used;
}

static void clean_snapshot(void)
{
    ready_status(0, 32); statuses[18] = statuses[19] = 0; status_size = 32;
}

/* Bluetooth: identical driver bytes, its own status path, and no USB access. */
static pappl_printer_t *bluetooth_cases(pappl_system_t *sys, pappl_printer_t *usb, pappl_device_t *d)
{
    const char *bt_uri = "ptouch-bt://Brother/PT-P710BT/A4:C1:38:AB:CD:EF";
    pappl_pr_driver_data_t rejected = {0}; ipp_t *attrs = NULL;
    assert(!pt_driver_cb(sys, "brother_ptouch", "ptouch-bt://Brother/PT-P710BT/zz",
                         selected_id, &rejected, &attrs, NULL) && !rejected.extension);
    pappl_printer_t *bt = papplPrinterCreate(sys, 0, "bt", "brother_ptouch", selected_id, bt_uri);
    assert(bt);
    unsigned enumerations = usb_enumerations, reads = usb_reads;

    clean_snapshot(); reset_wire(); unsigned before = bt_reads;
    assert(status_cb(bt) && pt_media_confirmed(bt) && bt_reads > before);
    assert(wire_used == 3 && !memcmp(wire, "\x1b\x69\x53", 3));
    can_open = false; assert(status_cb(bt)); can_open = true;
    assert(!pt_media_confirmed(bt) && (papplPrinterGetReasons(bt) & PAPPL_PREASON_OFFLINE));

    /* Each policy yields distinct bytes, identical on both transports. */
    static unsigned char usb_wire[sizeof wire], bt_wire[4][sizeof wire];
    size_t bt_sizes[4];
    const char *finishings[] = {"none", "trim", "trim-after-job", "ptouch-chain"};
    for (unsigned i = 0; i < 4; i++) {
        unsigned saved_usb = usb_enumerations, saved_reads = usb_reads;
        size_t usb_size = transport_batch(usb, finishings[i], d, usb_wire);
        usb_enumerations = saved_usb; usb_reads = saved_reads;
        bt_sizes[i] = transport_batch(bt, finishings[i], d, bt_wire[i]);
        assert(usb_size == bt_sizes[i] && !memcmp(usb_wire, bt_wire[i], bt_sizes[i]));
        for (unsigned k = 0; k < i; k++)
            assert(bt_sizes[k] != bt_sizes[i] || memcmp(bt_wire[k], bt_wire[i], bt_sizes[i]));
    }

    /* Link loss during raster: the job fails, cleanup sends nothing, and the
     * uncertain run blocks the next job before any command. */
    pt_run_set(bt, PT_RUN_CLOSED); reset_wire(); clean_snapshot();
    pappl_job_t *j = job(bt, "trim", 1, false);
    pappl_pr_options_t *o = begin(j, d);
    fail_opcode = 'G'; assert(!page_test(j, o, d));
    size_t sent = wire_used; end(j, o, d);
    assert(wire_used == sent && pt_run_get(bt) == PT_RUN_UNCERTAIN);
    pappl_job_t *follower = job(bt, "trim", 1, false);
    o = papplJobCreatePrintOptions(follower, 1, false);
    reset_wire(); assert(!r_startjob(follower, o, d) && wire_used == 0);
    end(follower, o, d);
    /* Link loss while finalizing: the status stream ends after the print command. */
    pt_run_set(bt, PT_RUN_CLOSED); reset_wire(); clean_snapshot();
    j = job(bt, "trim", 1, false); o = begin(j, d); assert(page_test(j, o, d));
    bt_eof = true; assert(!r_endjob(j, o, d)); bt_eof = false;
    sent = wire_used; assert(r_endjob(j, o, d) && wire_used == sent);
    papplJobDeletePrintOptions(o);
    assert(pt_run_get(bt) == PT_RUN_UNCERTAIN);
    follower = job(bt, "trim", 1, false);
    o = papplJobCreatePrintOptions(follower, 1, false);
    reset_wire(); assert(!r_startjob(follower, o, d) && wire_used == 0);
    end(follower, o, d);
    pt_run_set(bt, PT_RUN_CLOSED);

    /* Idle polling: 0 never touches the device, N allows one attempt per window
     * whether or not it succeeds, and job status reads are unaffected. */
    pt_printer_state *state = pt_state(bt);
    assert(state->idle_poll_seconds == -1);
    unsigned opened = opens;
    clean_snapshot(); assert(status_cb(bt)); clean_snapshot(); assert(status_cb(bt));
    assert(opens == opened + 2);
    state->idle_poll_seconds = 0; opened = opens; before = bt_reads;
    for (int i = 0; i < 3; i++) assert(status_cb(bt));
    assert(opens == opened && bt_reads == before);
    clean_snapshot(); j = job(bt, "none", 1, false);
    assert(pt_refresh_ready_media(j, d) && bt_reads > before);
    state->idle_poll_seconds = 300; state->idle_polled = false; opened = opens;
    clean_snapshot(); assert(status_cb(bt)); assert(status_cb(bt));
    assert(opens == opened + 1 && pt_media_confirmed(bt));
    /* The interval is exact: 299.5 s is too early, 300 s is due. */
    clock_gettime(CLOCK_MONOTONIC, &state->last_idle_poll);
    state->last_idle_poll.tv_sec -= 299;
    state->last_idle_poll.tv_nsec -= 500000000L;
    if (state->last_idle_poll.tv_nsec < 0) { state->last_idle_poll.tv_sec--; state->last_idle_poll.tv_nsec += 1000000000L; }
    assert(status_cb(bt) && opens == opened + 1);
    state->last_idle_poll.tv_sec -= 1;
    clean_snapshot(); assert(status_cb(bt) && opens == opened + 2);
    can_open = false; state->idle_polled = false; opened = opens;
    for (int i = 0; i < 3; i++) assert(status_cb(bt));
    assert(opens == opened + 1);
    can_open = true; state->idle_poll_seconds = -1;

    assert(usb_enumerations == enumerations && usb_reads == reads);
    return bt;
}

/* Tape width of the last print-information command on the wire. */
static int printed_tape(void)
{
    int tape = 0;
    for (size_t pos = 0; pos < wire_used; pos += command_size(pos))
        if (wire[pos] == 27 && wire[pos + 1] == 'i' && (wire[pos + 2] == 'c' || wire[pos + 2] == 'z'))
            tape = wire[pos + 5];
    return tape;
}

static void await_terminal(pappl_job_t *j)
{
    for (int i = 0; i < 2000 && papplJobGetState(j) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
}

/* Once a job reports media-needed having sent only status requests (and, when
 * `job` is set, the expected message), load `tape`, or cancel the job when 0. */
typedef struct { pappl_printer_t *printer; pappl_job_t *job; const char *message; int tape; bool acted; } tape_wait;
static bool job_waits(const tape_wait *w)
{
    const char *message = w->job ? papplJobGetMessage(w->job) : NULL;
    return (papplPrinterGetReasons(w->printer) & PAPPL_PREASON_MEDIA_NEEDED) &&
           (!w->job || (message && !strcmp(message, w->message)));
}
static void *act_on_wait(void *arg)
{
    tape_wait *w = arg;
    for (int i = 0; i < 1000 && !job_waits(w); i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    size_t other;
    w->acted = job_waits(w) && status_requests(&other) && !other;
    if (w->acted && w->tape) queue_tapes(&w->tape, 1, true);
    else if (w->acted) papplJobCancel(w->job);
    return NULL;
}

/* A 12 mm raster page submitted while 18 mm tape is loaded, without explicit size. */
static void raster_wait_case(pappl_printer_t *usb, cups_mode_t mode, ipp_t *media, bool fits)
{
    char path[] = "/tmp/ptouch-wait-raster-XXXXXX";
    write_raster(path, 1, mode == CUPS_RASTER_WRITE_APPLE ? 8 : 1, mode, 0);
    const int loaded[] = {18, 18}; queue_tapes(loaded, 2, false);
    pt_run_set(usb, PT_RUN_CLOSED); reset_wire();
    assert(!(papplPrinterGetReasons(usb) & PAPPL_PREASON_MEDIA_NEEDED));
    tape_wait w = {usb, NULL, NULL, 12, false};
    pthread_t swapper;
    if (fits) assert(!pthread_create(&swapper, NULL, act_on_wait, &w));
    ipp_t *request = printer_request(usb, IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL,
                 mode == CUPS_RASTER_WRITE_APPLE ? "image/urf" : "image/pwg-raster");
    if (media) ippAddCollection(request, IPP_TAG_JOB, "media-col", media);
    char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(usb));
    ipp_t *reply = cupsDoFileRequest(http, request, resource, path); unlink(path);
    assert(reply && ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    pappl_job_t *j = papplPrinterFindJob(usb, ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0));
    ippDelete(reply); assert(j); await_terminal(j);
    if (fits) {
        pthread_join(swapper, NULL);
        if (papplJobGetState(j) != IPP_JSTATE_COMPLETED)
            fprintf(stderr, "Raster wait mode%d: state%d message %s\n", mode, papplJobGetState(j), papplJobGetMessage(j));
        assert(w.acted && papplJobGetState(j) == IPP_JSTATE_COMPLETED && printed_tape() == 12);
        expect_policy(1, raster_lengths, PT_CUT_EACH);
    } else {
        size_t other;
        assert(papplJobGetState(j) == IPP_JSTATE_ABORTED && status_requests(&other) == 1 && !other);
        assert(!strcmp(papplJobGetMessage(j), "Selected margins extend beyond the physical printable window"));
        assert(!(papplPrinterGetReasons(usb) & PAPPL_PREASON_MEDIA_NEEDED));
    }
}

/* Wrong-width first labels wait for matching tape, sending only status requests. */
static void wait_cases(pappl_printer_t *usb, pappl_device_t *d)
{
    pappl_pr_driver_data_t before; papplPrinterGetDriverData(usb, &before);
    pappl_media_col_t media18; assert(media_col_for(18, 10000, 180, &media18));
    pt_geometry g18; assert(pt_tape_geometry(18, &g18));
    ipp_t *twelve = media_col("om_12x50mm_12x50mm", -1, -1);
    size_t other;
    /* A 12 mm page inherits 18 mm margins, waits through an empty read, then prints. */
    pt_run_set(usb, PT_RUN_CLOSED); assert(papplPrinterSetReadyMedia(usb, 1, &media18)); reset_wire();
    const int swapped[] = {18, 18, 12}; queue_tapes(swapped, 3, false);
    status_gap_at = 64; status_gap_reads = PT_STATUS_POLL_TRIES;
    pappl_job_t *j = media_job(usb, "none", 1, false, twelve);
    pappl_pr_options_t *o = begin(j, d);
    assert(o->header.cupsWidth == 85 && o->media.left_margin == g18.left_margin_centimm &&
           o->media.right_margin == g18.right_margin_centimm);
    assert(r_startpage(j, o, d, 0) && status_used == 96);
    assert(status_requests(&other) == 4 && other && printed_tape() == 12);
    pappl_pr_driver_data_t dd; papplPrinterGetDriverData(usb, &dd);
    assert(dd.media_ready[0].size_width == 1200);
    assert(!(papplPrinterGetReasons(usb) & PAPPL_PREASON_MEDIA_NEEDED));
    papplJobCancel(j); end(j, o, d);
    /* Cancellation ends the wait without print commands. */
    pt_run_set(usb, PT_RUN_CLOSED); assert(papplPrinterSetReadyMedia(usb, 1, &media18)); reset_wire();
    const int stuck[] = {18, 18}; queue_tapes(stuck, 2, false);
    j = media_job(usb, "none", 1, false, twelve); o = begin(j, d);
    tape_wait w = {usb, j, "Waiting for 12 mm tape; 18 mm loaded", 0, false};
    pthread_t canceler; assert(!pthread_create(&canceler, NULL, act_on_wait, &w));
    assert(!r_startpage(j, o, d, 0)); pthread_join(canceler, NULL);
    assert(w.acted && papplJobIsCanceled(j) && status_requests(&other) >= 2 && !other);
    assert(pt_run_get(usb) == PT_RUN_CLOSED && !(papplPrinterGetReasons(usb) & PAPPL_PREASON_MEDIA_NEEDED));
    end(j, o, d);
    /* Cancellation during a read that reports the wanted tape still sends no print commands. */
    assert(papplPrinterSetReadyMedia(usb, 1, &media18)); reset_wire();
    const int matching[] = {18, 12}; queue_tapes(matching, 2, false);
    j = media_job(usb, "none", 1, false, twelve); o = begin(j, d);
    cancel_during_read = j; cancel_read_at = 32;
    assert(!r_startpage(j, o, d, 0) && !cancel_during_read && papplJobIsCanceled(j));
    assert(status_requests(&other) == 2 && !other && status_used == 64);
    assert(pt_run_get(usb) == PT_RUN_CLOSED && !(papplPrinterGetReasons(usb) & PAPPL_PREASON_MEDIA_NEEDED));
    end(j, o, d);
    /* A status I/O error aborts the wait with a specific message. */
    assert(papplPrinterSetReadyMedia(usb, 1, &media18)); reset_wire();
    queue_tapes(stuck, 1, false); status_error_at = 32;
    j = media_job(usb, "none", 1, false, twelve); o = begin(j, d);
    assert(!r_startpage(j, o, d, 0) && status_requests(&other) == 2 && !other);
    assert(!strcmp(papplJobGetMessage(j), "Cannot read loaded tape while waiting for 12 mm tape"));
    end(j, o, d); ippDelete(twelve);
    /* A later label for another width fails without waiting. */
    pt_run_set(usb, PT_RUN_CLOSED); assert(papplPrinterSetReadyMedia(usb, 1, &media18)); reset_wire();
    queue_tapes(stuck, 1, false); j = job(usb, "none", 1, false); o = begin(j, d); assert(page_test(j, o, d));
    size_t first_label = wire_used;
    o->header.cupsWidth = 85; o->media.size_width = 1200;
    assert(!r_startpage(j, o, d, 1) && wire_used == first_label && status_used == 32);
    assert(!strcmp(papplJobGetMessage(j), "Page geometry does not match loaded 18 mm tape at 180 dpi"));
    end(j, o, d);
    /* IPP raster and URF pages without explicit media wait, then print on 12 mm. */
    raster_wait_case(usb, CUPS_RASTER_WRITE_PWG, NULL, true);
    raster_wait_case(usb, CUPS_RASTER_WRITE_APPLE, NULL, true);
    /* Submitted margins stay authoritative after the wait and before it. */
    ipp_t *wide = media_col(NULL, 297, 297), *outside = media_col(NULL, 0, 0);
    const cups_mode_t modes[] = {CUPS_RASTER_WRITE_PWG, CUPS_RASTER_WRITE_APPLE};
    for (size_t m = 0; m < 2; m++) {
        raster_wait_case(usb, modes[m], wide, true);
        raster_wait_case(usb, modes[m], outside, false);
    }
    ippDelete(wide); ippDelete(outside);
    /* A PNG that fits 18 mm but not the loaded 12 mm margins passes preflight, waits, then prints. */
    pt_run_set(usb, PT_RUN_CLOSED);
    ipp_t *eighteen = media_col("om_18x50mm_18x50mm", -1, -1);
    j = upload_image(usb, "none", 1, false, eighteen, 120, 0, 119); ippDelete(eighteen);
    const int loaded12[] = {12, 12, 12}; queue_tapes(loaded12, 3, false);
    reset_wire(); assert(papplJobRelease(j, "test"));
    w = (tape_wait){usb, j, "Waiting for 18 mm tape; 12 mm loaded", 18, false};
    act_on_wait(&w); assert(w.acted); await_terminal(j);
    if (papplJobGetState(j) != IPP_JSTATE_COMPLETED)
        fprintf(stderr, "PNG wait: state%d message %s\n", papplJobGetState(j), papplJobGetMessage(j));
    assert(papplJobGetState(j) == IPP_JSTATE_COMPLETED && printed_tape() == 18);
    const unsigned png_length[] = {354}; expect_policy(1, png_length, PT_CUT_NONE);
    bool dots[128] = {false};
    for (size_t pos = 0; pos < wire_used; pos += command_size(pos)) {
        if (wire[pos] != 'G') continue;
        const unsigned char *head = wire + pos + (selected_model->pid == 0x20af ? 4 : 3);
        for (unsigned x = 0; x < 128; x++) if (head[15 - x / 8] & (1u << (x % 8))) dots[x] = true;
    }
    for (unsigned x = 0; x < 128; x++) assert(dots[x] == (x == 4 || x == 123));
    /* Explicit margins leaving fewer than 120 dots still fail in preflight without waiting. */
    pt_run_set(usb, PT_RUN_CLOSED);
    ipp_t *narrow = media_col("om_18x50mm_18x50mm", g18.left_margin_centimm, 200);
    j = upload_image(usb, "none", 1, false, narrow, 120, 0, 119); ippDelete(narrow);
    queue_tapes(loaded12, 1, false); reset_wire(); assert(papplJobRelease(j, "test")); await_terminal(j);
    assert(papplJobGetState(j) == IPP_JSTATE_ABORTED && status_requests(&other) == 1 && !other);
    assert(strstr(papplJobGetMessage(j), "printable width is 110"));
    assert(!(papplPrinterGetReasons(usb) & PAPPL_PREASON_MEDIA_NEEDED));
    pt_run_set(usb, PT_RUN_CLOSED); assert(papplPrinterSetReadyMedia(usb, 1, before.media_ready));
}

/* Ptouch-Cut on its own connection, so job and status threads can request it too. */
static char cut_message[256];
static ipp_status_t send_cut(const char *host, pappl_printer_t *printer, ipp_t *request, const char *file)
{
    http_t *connection = httpConnect2(host, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL);
    assert(connection);
    if (!request) request = printer_request(printer, PT_OP_CUT);
    char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    ipp_t *reply = file ? cupsDoFileRequest(connection, request, resource, file) :
                          cupsDoRequest(connection, request, resource);
    ipp_status_t status = reply ? ippGetStatusCode(reply) : cupsLastError();
    ipp_attribute_t *message = reply ? ippFindAttribute(reply, "status-message", IPP_TAG_TEXT) : NULL;
    pthread_mutex_lock(&cut_message_lock);
    snprintf(cut_message, sizeof cut_message, "%s", message ? ippGetString(message, 0, NULL) : "");
    pthread_mutex_unlock(&cut_message_lock);
    ippDelete(reply); httpClose(connection);
    return status;
}
static ipp_status_t cut(pappl_printer_t *printer) { return send_cut("localhost", printer, NULL, NULL); }

/* The wire holds an optional status request, then exactly the blank continuation. */
static void expect_cut_wire(bool status_request, int tape)
{
    unsigned char expected[64]; size_t n = 0;
    if (status_request) { memcpy(expected, "\x1b\x69\x53", 3); n = 3; }
    n += pt_cmd_blank_continuation(expected + n, (uint8_t)tape);
    pthread_mutex_lock(&fixture_lock);
    assert(wire_used == n && !memcmp(wire, expected, n));
    pthread_mutex_unlock(&fixture_lock);
}

/* Controlled pause at a job's next raster row, so the test can admit new work. */
static void begin_raster_pause(void)
{
    pthread_mutex_lock(&pause_lock);
    pause_raster = true; raster_paused = raster_go = false;
    pthread_mutex_unlock(&pause_lock);
}
static void wait_raster_pause(void)
{
    pthread_mutex_lock(&pause_lock);
    for (int i = 0; i < 5000 && !raster_paused; i++) {
        struct timespec delay = {0, 1000000L};
        pthread_mutex_unlock(&pause_lock); nanosleep(&delay, NULL); pthread_mutex_lock(&pause_lock);
    }
    assert(raster_paused);
    pthread_mutex_unlock(&pause_lock);
}
static void release_raster_pause(void)
{
    pthread_mutex_lock(&pause_lock);
    raster_go = true; pthread_cond_broadcast(&pause_cond);
    pthread_mutex_unlock(&pause_lock);
}

/* A repeating clean-snapshot/receive-ready stream keeps concurrent work fed. */
static void repeat_status(void)
{
    const int twelve = 12;
    queue_tapes(&twelve, 1, true);
    status_used = 0; status_repeat = true;
}

/* One Ptouch-Cut request on its own connection thread. */
typedef struct { pappl_printer_t *printer; ipp_status_t status; bool started, done; } cut_request;
static void *cut_request_thread(void *arg)
{
    cut_request *r = arg;
    pthread_mutex_lock(&request_lock); r->started = true; pthread_mutex_unlock(&request_lock);
    ipp_status_t status = cut(r->printer);
    pthread_mutex_lock(&request_lock); r->status = status; r->done = true; pthread_mutex_unlock(&request_lock);
    return NULL;
}
static bool cut_done(cut_request *r)
{
    pthread_mutex_lock(&request_lock); bool done = r->done; pthread_mutex_unlock(&request_lock);
    return done;
}
static ipp_status_t cut_status(cut_request *r)
{
    pthread_mutex_lock(&request_lock); ipp_status_t status = r->status; pthread_mutex_unlock(&request_lock);
    return status;
}
static void start_cut(cut_request *r, pthread_t *thread, pappl_printer_t *printer)
{
    memset(r, 0, sizeof *r); r->printer = printer;
    assert(!pthread_create(thread, NULL, cut_request_thread, r));
    bool started = false;
    for (int i = 0; i < 2000 && !started; i++) {
        pthread_mutex_lock(&request_lock); started = r->started; pthread_mutex_unlock(&request_lock);
        if (!started) { struct timespec d = {0, 1000000L}; nanosleep(&d, NULL); }
    }
    assert(started);
}

/* One raster Print-Job submitted from its own thread (direct start_job path). */
typedef struct { pappl_printer_t *printer; pappl_job_t *job; bool started, done; } job_submit;
static bool job_started(job_submit *s)
{
    pthread_mutex_lock(&request_lock); bool started = s->started; pthread_mutex_unlock(&request_lock);
    return started;
}
static bool job_done(job_submit *s)
{
    pthread_mutex_lock(&request_lock); bool done = s->done; pthread_mutex_unlock(&request_lock);
    return done;
}
static void *submit_raster_thread(void *arg)
{
    job_submit *s = arg;
    char path[] = "/tmp/ptouch-order-raster-XXXXXX";
    write_raster(path, 1, 1, CUPS_RASTER_WRITE_PWG, 0);
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(s->printer));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    http_t *connection = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL);
    assert(connection);
    ipp_t *request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/pwg-raster");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", 1);
    add_finishing(request, "none", false);
    pthread_mutex_lock(&request_lock); s->started = true; pthread_mutex_unlock(&request_lock);
    ipp_t *reply = cupsDoFileRequest(connection, request, resource, path); unlink(path);
    assert(reply && ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    s->job = papplPrinterFindJob(s->printer, ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0));
    ippDelete(reply); httpClose(connection);
    pthread_mutex_lock(&request_lock); s->done = true; pthread_mutex_unlock(&request_lock);
    return NULL;
}

/* Raster pages on the wire in order: transmitted row count and any ink. */
typedef struct { size_t rows; bool ink; } wire_page;
static size_t wire_pages(wire_page *pages, size_t max)
{
    size_t n = 0, rows = 0; bool ink = false, active = false;
    for (size_t pos = 0; pos < wire_used;) {
        size_t size = command_size(pos);
        if (wire[pos] == 27 && wire[pos + 1] == 'i' && (wire[pos + 2] == 'c' || wire[pos + 2] == 'z')) {
            if (active && n < max) { pages[n].rows = rows; pages[n].ink = ink; n++; }
            active = true; rows = 0; ink = false;
        } else if (wire[pos] == 'G' && active) {
            rows++;
            const unsigned char *packed = wire + pos + (is_p710bt() ? 4 : 3);
            for (int b = 0; b < 16; b++) ink |= packed[b] != 0;
        }
        pos += size;
    }
    if (active && n < max) { pages[n].rows = rows; pages[n].ink = ink; n++; }
    return n;
}

static const unsigned queued_png_rows = 180;

/* A and B print; C1, D, C2, E are admitted in that order.  Each cut waits for
 * the work accepted before it, and later jobs wait for the earlier cut. */
static void queue_order_case(pappl_printer_t *usb)
{
    pappl_media_col_t media12; assert(media_col_for(12, 10000, 180, &media12));
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    pt_run_set(usb, PT_RUN_CLOSED);
    repeat_status();

    pappl_job_t *a = upload_image(usb, "none", 1, false, NULL, 85, 40, 42);
    pappl_job_t *b = upload_image(usb, "none", 1, false, NULL, 85, 44, 46);
    reset_wire();
    begin_raster_pause();
    assert(papplJobRelease(a, "test"));
    wait_raster_pause();
    assert(papplJobRelease(b, "test"));

    cut_request c1, c2; pthread_t t1, t2;
    start_cut(&c1, &t1, usb);
    struct timespec settle = {0, 200000000L}; nanosleep(&settle, NULL);
    assert(!cut_done(&c1));
    pappl_job_t *d = upload_image(usb, "none", 1, false, NULL, 85, 48, 50);
    assert(papplJobRelease(d, "test"));
    start_cut(&c2, &t2, usb);
    nanosleep(&settle, NULL);
    pappl_job_t *e = upload_image(usb, "none", 1, false, NULL, 85, 52, 54);
    assert(papplJobRelease(e, "test"));
    assert(!cut_done(&c1) && !cut_done(&c2)); /* A response follows its execution. */

    release_raster_pause();
    pthread_join(t1, NULL); pthread_join(t2, NULL);
    assert(cut_status(&c1) == IPP_STATUS_OK && cut_status(&c2) == IPP_STATUS_OK);
    await_terminal(a); await_terminal(b); await_terminal(d); await_terminal(e);
    assert(papplJobGetState(a) == IPP_JSTATE_COMPLETED && papplJobGetState(b) == IPP_JSTATE_COMPLETED);
    assert(papplJobGetState(d) == IPP_JSTATE_COMPLETED && papplJobGetState(e) == IPP_JSTATE_COMPLETED);

    wire_page pages[16];
    size_t count = wire_pages(pages, 16);
    assert(count == 6);
    assert(pages[0].rows == tx(queued_png_rows) && pages[1].rows == tx(queued_png_rows));
    assert(pages[3].rows == tx(queued_png_rows) && pages[5].rows == tx(queued_png_rows));
    assert(pages[2].rows == 1 && pages[4].rows == 1);
    assert(pages[0].ink && pages[1].ink && pages[3].ink && pages[5].ink);
    assert(!pages[2].ink && !pages[4].ink);
    status_repeat = false;
    pt_run_set(usb, PT_RUN_CLOSED);
}

/* An accepted streaming-raster follower uses the direct job-start entry; it must
 * also wait behind the cut accepted before it. */
static void streaming_follower_case(pappl_printer_t *usb)
{
    pappl_media_col_t media12; assert(media_col_for(12, 10000, 180, &media12));
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    pt_run_set(usb, PT_RUN_OPEN);
    repeat_status();
    reset_wire();

    /* Hold the device so the cut and then the follower wait for their turns. */
    assert(papplPrinterOpenDevice(usb));
    cut_request c1; pthread_t t1;
    start_cut(&c1, &t1, usb);
    struct timespec settle = {0, 200000000L}; nanosleep(&settle, NULL);
    job_submit follower = {.printer = usb};
    pthread_t ft; assert(!pthread_create(&ft, NULL, submit_raster_thread, &follower));
    for (int i = 0; i < 2000 && !job_started(&follower); i++) { struct timespec d = {0, 1000000L}; nanosleep(&d, NULL); }
    nanosleep(&settle, NULL);
    assert(!cut_done(&c1) && !job_done(&follower));

    papplPrinterCloseDevice(usb);
    pthread_join(t1, NULL); pthread_join(ft, NULL);
    assert(cut_status(&c1) == IPP_STATUS_OK && job_done(&follower));
    assert(follower.job && papplJobGetState(follower.job) == IPP_JSTATE_COMPLETED);

    wire_page pages[16];
    size_t count = wire_pages(pages, 16);
    assert(count == 2);
    assert(pages[0].rows == 1 && !pages[0].ink);
    assert(pages[1].rows == tx(queued_png_rows) && !pages[1].ink);
    status_repeat = false;
    pt_run_set(usb, PT_RUN_CLOSED);
}

static void *status_poll_thread(void *arg) { assert(status_cb((pappl_printer_t *)arg)); return NULL; }

/* A cut admitted while a status poll owns the device waits for the poll. */
static void status_poll_wait_case(pappl_printer_t *usb)
{
    pappl_media_col_t media12; assert(media_col_for(12, 10000, 180, &media12));
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    pt_run_set(usb, PT_RUN_OPEN);
    repeat_status();
    reset_wire();

    pthread_mutex_lock(&pause_lock);
    pause_status_read = true; status_read_paused = status_read_go = false;
    pthread_mutex_unlock(&pause_lock);
    pthread_t poll; assert(!pthread_create(&poll, NULL, status_poll_thread, usb));
    pthread_mutex_lock(&pause_lock);
    for (int i = 0; i < 5000 && !status_read_paused; i++) {
        struct timespec delay = {0, 1000000L};
        pthread_mutex_unlock(&pause_lock); nanosleep(&delay, NULL); pthread_mutex_lock(&pause_lock);
    }
    assert(status_read_paused);
    pthread_mutex_unlock(&pause_lock);

    reset_wire();
    cut_request c1; pthread_t t1;
    start_cut(&c1, &t1, usb);
    struct timespec settle = {0, 200000000L}; nanosleep(&settle, NULL);
    assert(!cut_done(&c1));

    pthread_mutex_lock(&pause_lock);
    status_read_go = true; pthread_cond_broadcast(&pause_cond);
    pthread_mutex_unlock(&pause_lock);
    pthread_join(poll, NULL); pthread_join(t1, NULL);
    assert(cut_status(&c1) == IPP_STATUS_OK);
    expect_cut_wire(true, 12);
    status_repeat = false;
    pt_run_set(usb, PT_RUN_CLOSED);
}

/* The newest active job; the fixture uses it for the streaming request. */
static pappl_job_t *first_active_job;
static void capture_active_job(pappl_job_t *job, void *data)
{
    (void)data;
    if (!first_active_job) first_active_job = job;
}
static pappl_job_t *newest_active_job(pappl_printer_t *printer)
{
    first_active_job = NULL;
    papplPrinterIterateActiveJobs(printer, capture_active_job, NULL, 1, 1);
    return first_active_job;
}

/* A held PNG released while a later streaming job waits behind its cut: the
 * newest-first scan must pass the streaming follower and start the PNG. */
static void held_png_before_streaming_case(pappl_printer_t *usb)
{
    pappl_media_col_t media12; assert(media_col_for(12, 10000, 180, &media12));
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    pt_run_set(usb, PT_RUN_OPEN);
    repeat_status();
    reset_wire();

    /* B is accepted and held before the cut; the cut and the later streaming
     * follower then wait behind it. */
    pappl_job_t *b = upload_image(usb, "none", 1, false, NULL, 85, 40, 42);
    cut_request c1; pthread_t t1;
    start_cut(&c1, &t1, usb);
    struct timespec settle = {0, 200000000L}; nanosleep(&settle, NULL);
    assert(!cut_done(&c1));

    job_submit s = {.printer = usb};
    pthread_t st; assert(!pthread_create(&st, NULL, submit_raster_thread, &s));
    for (int i = 0; i < 2000 && papplPrinterGetNumberOfActiveJobs(usb) < 2; i++) {
        struct timespec d = {0, 1000000L}; nanosleep(&d, NULL);
    }
    nanosleep(&settle, NULL);
    assert(papplPrinterGetNumberOfActiveJobs(usb) >= 2); /* held B and streaming S */
    assert(!cut_done(&c1) && !job_done(&s));

    /* Releasing B must start it even though S leads the active-job scan. */
    assert(papplJobRelease(b, "test"));
    await_terminal(b);
    assert(papplJobGetState(b) == IPP_JSTATE_COMPLETED);
    pthread_join(t1, NULL); pthread_join(st, NULL);
    assert(cut_status(&c1) == IPP_STATUS_OK && job_done(&s));
    assert(s.job && papplJobGetState(s.job) == IPP_JSTATE_COMPLETED);

    wire_page pages[16];
    size_t count = wire_pages(pages, 16);
    assert(count == 3);
    assert(pages[0].rows == tx(queued_png_rows) && pages[0].ink);
    assert(pages[1].rows == 1 && !pages[1].ink);
    assert(pages[2].rows == tx(queued_png_rows) && !pages[2].ink);
    status_repeat = false;
    pt_run_set(usb, PT_RUN_CLOSED);
}

/* A pending PNG behind a cut whose closure meets a canceled streaming request
 * before that request finishes: its unowned completion must resume scheduling. */
static void canceled_stream_behind_cut_case(pappl_printer_t *usb)
{
    pappl_media_col_t media12; assert(media_col_for(12, 10000, 180, &media12));
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    pt_run_set(usb, PT_RUN_OPEN);
    repeat_status();
    reset_wire();

    /* Hold the device so the cut, the PNG accepted behind it, and then the
     * streaming request all wait their turns. */
    assert(papplPrinterOpenDevice(usb));
    cut_request c1; pthread_t t1;
    start_cut(&c1, &t1, usb);
    struct timespec settle = {0, 200000000L}; nanosleep(&settle, NULL);
    assert(!cut_done(&c1));

    pappl_job_t *p = upload_image(usb, "none", 1, false, NULL, 85, 40, 42);
    assert(papplJobRelease(p, "test"));
    job_submit s = {.printer = usb};
    pthread_t st; assert(!pthread_create(&st, NULL, submit_raster_thread, &s));
    for (int i = 0; i < 2000 && papplPrinterGetNumberOfActiveJobs(usb) < 2; i++) {
        struct timespec d = {0, 1000000L}; nanosleep(&d, NULL);
    }
    nanosleep(&settle, NULL);
    assert(papplPrinterGetNumberOfActiveJobs(usb) >= 2);
    pappl_job_t *stream = newest_active_job(usb);
    assert(stream && papplJobGetID(stream) > papplJobGetID(p));
    assert(!cut_done(&c1) && !job_done(&s));

    /* Cancel the waiting stream, then force the cut's device open to fail once
     * so its closure removes the barrier while the canceled request is active. */
    pt_printer_state *state = pt_state(usb);
    int saved_poll = state->idle_poll_seconds;
    state->idle_poll_seconds = 0; /* No idle probe may consume the failed open. */
    fail_next_open = true;
    papplJobCancel(stream);
    papplPrinterCloseDevice(usb);
    pthread_join(t1, NULL);
    state->idle_poll_seconds = saved_poll;

    assert(cut_status(&c1) == IPP_STATUS_ERROR_BUSY);
    await_terminal(p);
    assert(papplJobGetState(p) == IPP_JSTATE_COMPLETED);
    pthread_join(st, NULL);
    assert(job_done(&s) && s.job && papplJobGetState(s.job) == IPP_JSTATE_CANCELED);

    wire_page pages[16];
    size_t count = wire_pages(pages, 16);
    assert(count == 1);
    assert(pages[0].rows == tx(queued_png_rows) && pages[0].ink);
    status_repeat = false;
    pt_run_set(usb, PT_RUN_CLOSED);
}

static unsigned named(ipp_t *reply, const char *name)
{
    unsigned n = 0;
    for (ipp_attribute_t *a = ippFirstAttribute(reply); a; a = ippNextAttribute(reply))
        if (ippGetName(a) && !strcmp(ippGetName(a), name)) n++;
    return n;
}

/* PAPPL 1.4.11's standard printer operations, plus Ptouch-Cut on P710BT only. */
static void expect_operations(ipp_attribute_t *ops, bool cut_supported)
{
    static const ipp_op_t standard[] = {
        IPP_OP_PRINT_JOB, IPP_OP_VALIDATE_JOB, IPP_OP_CREATE_JOB, IPP_OP_SEND_DOCUMENT,
        IPP_OP_CANCEL_JOB, IPP_OP_GET_JOB_ATTRIBUTES, IPP_OP_GET_JOBS, IPP_OP_GET_PRINTER_ATTRIBUTES,
        IPP_OP_PAUSE_PRINTER, IPP_OP_RESUME_PRINTER, IPP_OP_SET_PRINTER_ATTRIBUTES,
        IPP_OP_GET_PRINTER_SUPPORTED_VALUES, IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS,
        IPP_OP_CREATE_JOB_SUBSCRIPTIONS, IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES, IPP_OP_GET_SUBSCRIPTIONS,
        IPP_OP_RENEW_SUBSCRIPTION, IPP_OP_CANCEL_SUBSCRIPTION, IPP_OP_GET_NOTIFICATIONS,
        IPP_OP_ENABLE_PRINTER, IPP_OP_DISABLE_PRINTER, IPP_OP_PAUSE_PRINTER_AFTER_CURRENT_JOB,
        IPP_OP_CANCEL_CURRENT_JOB, IPP_OP_CANCEL_JOBS, IPP_OP_CANCEL_MY_JOBS, IPP_OP_CLOSE_JOB,
        IPP_OP_IDENTIFY_PRINTER, IPP_OP_HOLD_JOB, IPP_OP_RELEASE_JOB, IPP_OP_HOLD_NEW_JOBS,
        IPP_OP_RELEASE_HELD_NEW_JOBS,
    };
    const int count = (int)(sizeof standard / sizeof standard[0]);
    assert(ops && ippGetValueTag(ops) == IPP_TAG_ENUM && ippGetCount(ops) == count + cut_supported);
    for (int i = 0; i < count; i++) assert(ippContainsInteger(ops, standard[i]));
    assert(ippContainsInteger(ops, PT_OP_CUT) == cut_supported);
}

/* One complete operations-supported per printer, honoring requested attributes. */
static void check_operations(pappl_printer_t *printer, bool cut_supported)
{
    const char *requests[] = {NULL, "operations-supported", "printer-description", "printer-name"};
    for (unsigned i = 0; i < 4; i++) {
        ipp_t *q = printer_request(printer, IPP_OP_GET_PRINTER_ATTRIBUTES);
        if (requests[i]) ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", NULL, requests[i]);
        ipp_t *reply = printer_response(printer, q);
        assert(ippGetStatusCode(reply) == IPP_STATUS_OK);
        assert(named(reply, "operations-supported") == (i < 3));
        if (i < 3) expect_operations(ippFindAttribute(reply, "operations-supported", IPP_TAG_ZERO), cut_supported);
        if (i == 1) assert(!named(reply, "printer-name"));
        ippDelete(reply);
    }
    /* Get-Printers merges each printer's operations into its own attribute. */
    char uri[128]; snprintf(uri, sizeof uri, "ipp://localhost:%d/ipp/system", port);
    ipp_t *q = ippNewRequest(IPP_OP_GET_PRINTERS);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_URI, "system-uri", NULL, uri);
    const char *names[] = {"printer-name", "operations-supported"};
    ippAddStrings(q, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", 2, NULL, names);
    ipp_t *reply = cupsDoRequest(http, q, "/ipp/system"); assert(reply);
    unsigned printers = named(reply, "printer-name");
    assert(printers > 1 && named(reply, "operations-supported") == printers);
    for (ipp_attribute_t *a = ippFirstAttribute(reply); a; a = ippNextAttribute(reply))
        if (ippGetName(a) && !strcmp(ippGetName(a), "operations-supported")) expect_operations(a, cut_supported);
    ippDelete(reply);
}

/* Remove driven fixtures so they are not counted as accepted print work. */
static pappl_job_t *cancel_list[512];
static int cancel_count;
static void collect_active_job(pappl_job_t *job, void *data)
{
    (void)data;
    if (cancel_count < (int)(sizeof cancel_list / sizeof cancel_list[0])) cancel_list[cancel_count++] = job;
}
static void cancel_active_jobs(pappl_printer_t *printer)
{
    cancel_count = 0;
    papplPrinterIterateActiveJobs(printer, collect_active_job, NULL, 1, 0);
    for (int i = 0; i < cancel_count; i++) papplJobCancel(cancel_list[i]);
}

/* PT-2730 neither advertises nor implements the action. */
static void cut_unsupported(pappl_printer_t *usb)
{
    check_operations(usb, false);
    pt_run_set(usb, PT_RUN_OPEN); reset_wire();
    unsigned opened = opens, reads = usb_reads;
    assert(cut(usb) == IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED);
    assert(wire_used == 0 && opens == opened && usb_reads == reads && pt_run_get(usb) == PT_RUN_OPEN);
    pt_run_set(usb, PT_RUN_CLOSED);
}

/* Ptouch-Cut through real IPP on live USB and Bluetooth fixtures. */
static void cut_cases(pappl_printer_t *usb, pappl_printer_t *bt, pappl_device_t *d)
{
    static unsigned char strip[sizeof wire];
    const int twelve = 12;
    cancel_active_jobs(usb);
    pappl_media_col_t media12; assert(media_col_for(12, 10000, 180, &media12));
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    check_operations(usb, true);
    /* A held chain label and a joined none strip are released by one white row:
     * no job, no replayed raster, and repeats send nothing. */
    const char *held[] = {"ptouch-chain", "none"};
    for (unsigned i = 0; i < 2; i++) {
        transport_batch(usb, held[i], d, strip);
        assert(pt_run_get(usb) == PT_RUN_OPEN);
        int jobs = papplPrinterGetNumberOfJobs(usb);
        unsigned opened = opens;
        queue_tapes(&twelve, 1, true); reset_wire();
        assert(cut(usb) == IPP_STATUS_OK);
        expect_cut_wire(true, 12);
        assert(status_used == status_size && opens == opened + 1);
        assert(pt_run_get(usb) == PT_RUN_OPEN_BLANK && papplPrinterGetNumberOfJobs(usb) == jobs);
        reset_wire(); unsigned reads = usb_reads;
        assert(cut(usb) == IPP_STATUS_OK && cut(usb) == IPP_STATUS_OK);
        assert(wire_used == 0 && usb_reads == reads && opens == opened + 3);
        assert(pt_run_get(usb) == PT_RUN_OPEN_BLANK);
    }
    /* The next batch continues the blank page without initialization; its FF
     * holds user content again, which a later cut releases. */
    reset_wire(); clean_snapshot();
    pappl_job_t *j = job(usb, "none", 1, false);
    pappl_pr_options_t *o = begin(j, d);
    assert(page_test(j, o, d)); ready_status(0, 32); end(j, o, d);
    expect_wire(0, 1, 1, 0); assert(pt_run_get(usb) == PT_RUN_OPEN);
    queue_tapes(&twelve, 1, true); reset_wire();
    assert(cut(usb) == IPP_STATUS_OK); expect_cut_wire(true, 12);
    assert(pt_run_get(usb) == PT_RUN_OPEN_BLANK);
    /* A trim batch after release closes the run; a cut on a closed run sends nothing. */
    reset_wire(); clean_snapshot();
    j = job(usb, "trim", 1, false); o = begin(j, d);
    assert(page_test(j, o, d)); ready_status(0, 32); end(j, o, d);
    expect_wire(0, 1, 0, 1); assert(pt_run_get(usb) == PT_RUN_CLOSED);
    reset_wire();
    assert(cut(usb) == IPP_STATUS_OK && wire_used == 0 && pt_run_get(usb) == PT_RUN_CLOSED);
    /* Rejected and canceled followers preserve the blank continuation. */
    pt_run_set(usb, PT_RUN_OPEN_BLANK); reset_wire();
    for (unsigned i = 0; i < 2; i++) {
        clean_snapshot();
        j = job(usb, "trim", 1, false); o = papplJobCreatePrintOptions(j, 1, false);
        assert(r_startjob(j, o, d));
        if (i) papplJobCancel(j);
        else o->header.cupsWidth++;
        assert(!r_startpage(j, o, d, 0)); end(j, o, d);
    }
    size_t other;
    assert(status_requests(&other) == 2 && !other && pt_run_get(usb) == PT_RUN_OPEN_BLANK);
    reset_wire(); assert(cut(usb) == IPP_STATUS_OK && wire_used == 0);
    /* A cassette change invalidates either open state, idle or during the action. */
    clean_snapshot(); statuses[10] = 24; reset_wire();
    assert(status_cb(usb) && pt_run_get(usb) == PT_RUN_CLOSED);
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    const int changed = 24;
    pt_run_set(usb, PT_RUN_OPEN); queue_tapes(&changed, 1, false); reset_wire();
    assert(cut(usb) == IPP_STATUS_OK && wire_used == 3 && pt_run_get(usb) == PT_RUN_CLOSED);
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    /* Faulted, missing, or empty live status refuses without the blank row. */
    for (unsigned i = 0; i < 3; i++) {
        pt_run_set(usb, PT_RUN_OPEN); clean_snapshot();
        if (i == 0) statuses[8] = 4;
        else if (i == 1) status_size = 0;
        else statuses[10] = 0;
        reset_wire();
        assert(cut(usb) == IPP_STATUS_ERROR_DEVICE && wire_used == 3);
        assert(pt_run_get(usb) == (i == 2 ? PT_RUN_CLOSED : PT_RUN_OPEN));
    }
    assert(papplPrinterSetReadyMedia(usb, 1, &media12));
    /* A stale ready notification precedes the snapshot, and fragmented
     * replies still reach the blank page's own receive-ready. */
    pt_run_set(usb, PT_RUN_OPEN); queue_tapes(&twelve, 1, true);
    memmove(statuses + 32, statuses, 128); memcpy(statuses, statuses + 128, 32);
    status_size = 160; status_chunk = 7; reset_wire();
    assert(cut(usb) == IPP_STATUS_OK && status_used == 160);
    expect_cut_wire(true, 12); assert(pt_run_get(usb) == PT_RUN_OPEN_BLANK);
    /* Completion without receive-ready, an error phase, a lost link, or a failed
     * transfer leaves output uncertain; repeats, polls and jobs then send nothing. */
    for (unsigned i = 0; i < 4; i++) {
        pt_run_set(usb, PT_RUN_OPEN); queue_tapes(&twelve, 1, true); reset_wire();
        if (i == 0) status_error_at = 96;
        else if (i == 1) statuses[96 + 8] = 4;
        else if (i == 2) status_error_at = 32;
        else fail_length = 57;
        assert(cut(usb) == IPP_STATUS_ERROR_DEVICE);
        if (i < 3) expect_cut_wire(true, 12);
        else assert(wire_used == 3);
        assert(strstr(cut_message, "output uncertain: power-cycle printer, then restart server"));
        assert(pt_run_get(usb) == PT_RUN_UNCERTAIN);
        status_error_at = SIZE_MAX; ready_status(0, 32); reset_wire();
        assert(cut(usb) == IPP_STATUS_ERROR_DEVICE && wire_used == 0 && status_used == 0);
        assert(strstr(cut_message, "Output uncertain: power-cycle printer, then restart server"));
        assert(status_cb(usb) && wire_used == 0);
        j = job(usb, "trim", 1, false); o = papplJobCreatePrintOptions(j, 1, false);
        assert(!r_startjob(j, o, d) && wire_used == 0); end(j, o, d);
        assert(pt_run_get(usb) == PT_RUN_UNCERTAIN);
    }
    /* A cut waits through device contention and returns its own result. */
    queue_order_case(usb);
    streaming_follower_case(usb);
    status_poll_wait_case(usb);
    held_png_before_streaming_case(usb);
    canceled_stream_behind_cut_case(usb);
    /* An open failure at the cut's turn is still busy, without a queued retry. */
    pt_run_set(usb, PT_RUN_OPEN); reset_wire();
    unsigned reads = usb_reads;
    can_open = false; assert(cut(usb) == IPP_STATUS_ERROR_BUSY); can_open = true;
    assert(strstr(cut_message, "busy or unavailable"));
    struct timespec settle = {0, 200000000L}; nanosleep(&settle, NULL);
    assert(wire_used == 0 && usb_reads == reads && pt_run_get(usb) == PT_RUN_OPEN);
    /* Explicit cuts ignore disabled or rate-limited polling and keep its slot. */
    pt_printer_state *state = pt_state(usb);
    for (int seconds = 0; seconds <= 300; seconds += 300) {
        state->idle_poll_seconds = seconds; state->idle_polled = seconds != 0;
        clock_gettime(CLOCK_MONOTONIC, &state->last_idle_poll);
        struct timespec slot = state->last_idle_poll;
        pt_run_set(usb, PT_RUN_OPEN); queue_tapes(&twelve, 1, true); reset_wire();
        unsigned opened = opens;
        assert(cut(usb) == IPP_STATUS_OK); expect_cut_wire(true, 12);
        assert(opens == opened + 1 && state->idle_polled == (seconds != 0));
        assert(!memcmp(&slot, &state->last_idle_poll, sizeof slot));
        assert(status_cb(usb) && opens == opened + 1);
    }
    state->idle_poll_seconds = -1;
    /* Bluetooth reconnects for the action with the USB bytes, and a lost link
     * leaves recovery required without any USB fallback. */
    unsigned enumerations = usb_enumerations, opened = opens, bt_before = bt_reads;
    reads = usb_reads;
    cancel_active_jobs(bt);
    pt_run_set(bt, PT_RUN_OPEN); queue_tapes(&twelve, 1, true); reset_wire();
    assert(cut(bt) == IPP_STATUS_OK); expect_cut_wire(true, 12);
    assert(opens == opened + 1 && bt_reads > bt_before && pt_run_get(bt) == PT_RUN_OPEN_BLANK);
    pt_run_set(bt, PT_RUN_OPEN); queue_tapes(&twelve, 1, true); status_error_at = 32; reset_wire();
    assert(cut(bt) == IPP_STATUS_ERROR_DEVICE); expect_cut_wire(true, 12);
    assert(pt_run_get(bt) == PT_RUN_UNCERTAIN);
    status_error_at = SIZE_MAX;
    assert(usb_enumerations == enumerations && usb_reads == reads);
    pt_run_set(bt, PT_RUN_CLOSED);
    /* Job and document data, other targets, and unknown queues are rejected
     * before the device is opened. */
    pt_run_set(usb, PT_RUN_OPEN); reset_wire(); opened = opens;
    ipp_t *q = printer_request(usb, PT_OP_CUT);
    ippAddInteger(q, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", 1);
    assert(send_cut("localhost", usb, q, NULL) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
    q = printer_request(usb, PT_OP_CUT);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/png");
    assert(send_cut("localhost", usb, q, NULL) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
    q = printer_request(usb, PT_OP_CUT);
    ippAddInteger(q, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", 1);
    assert(send_cut("localhost", usb, q, NULL) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
    char document[] = "/tmp/ptouch-cut-document-XXXXXX";
    write_png(document, 85, 0, 1);
    assert(send_cut("localhost", usb, NULL, document) == IPP_STATUS_ERROR_BAD_REQUEST);
    unlink(document);
    char uri[128]; snprintf(uri, sizeof uri, "ipp://localhost:%d/ipp/system", port);
    q = ippNewRequest(PT_OP_CUT);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_URI, "system-uri", NULL, uri);
    ippAddInteger(q, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "printer-id", papplPrinterGetID(usb));
    assert(send_cut("localhost", usb, q, NULL) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
    snprintf(uri, sizeof uri, "ipp://localhost:%d/ipp/print/missing", port);
    q = ippNewRequest(PT_OP_CUT);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    assert(send_cut("localhost", usb, q, NULL) == IPP_STATUS_ERROR_NOT_FOUND);
    assert(wire_used == 0 && opens == opened && pt_run_get(usb) == PT_RUN_OPEN);
    pt_run_set(usb, PT_RUN_CLOSED);
}

/* A non-localhost peer without authentication may print and cut. */
static void remote_cases(pappl_printer_t *printer, const char *address)
{
    http_t *remote = httpConnect2(address, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL);
    assert(remote);
    http_addr_t peer; socklen_t length = sizeof peer;
    assert(!getsockname(httpGetFd(remote), (struct sockaddr *)&peer, &length) && !httpAddrLocalhost(&peer));
    pt_run_set(printer, PT_RUN_CLOSED); reset_wire();
    char document[] = "/tmp/ptouch-remote-png-XXXXXX";
    write_png(document, 85, 40, 41);
    ipp_t *q = printer_request(printer, IPP_OP_PRINT_JOB);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/png");
    ippAddInteger(q, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", IPP_FINISHINGS_NONE);
    char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    ipp_t *reply = cupsDoFileRequest(remote, q, resource, document); unlink(document);
    assert(reply && ippGetStatusCode(reply) == IPP_STATUS_OK);
    pappl_job_t *printed = papplPrinterFindJob(printer, ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0));
    ippDelete(reply); httpClose(remote);
    assert(printed); await_terminal(printed);
    assert(papplJobGetState(printed) == IPP_JSTATE_COMPLETED && pt_run_get(printer) == PT_RUN_OPEN);
    reset_wire();
    assert(send_cut(address, printer, NULL, NULL) == IPP_STATUS_OK);
    expect_cut_wire(false, 12); assert(pt_run_get(printer) == PT_RUN_OPEN_BLANK);
}

/* First up non-loopback IPv4 address, so a request reaches PAPPL as a network peer. */
static bool network_address(char *out, size_t size)
{
    struct ifaddrs *list; assert(!getifaddrs(&list));
    bool found = false;
    for (struct ifaddrs *i = list; i && !found; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET &&
            (i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK))
            found = inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)size);
    freeifaddrs(list);
    return found;
}

static http_status_t deny_printing(pappl_client_t *client, const char *group, gid_t gid, void *data)
{
    (void)client; (void)group; (void)gid; (void)data;
    return HTTP_STATUS_FORBIDDEN;
}

/* A configured printing policy denies Print-Job and Ptouch-Cut alike, before
 * any job or device access. */
static void denied_cases(pappl_printer_t *printer)
{
    pt_run_set(printer, PT_RUN_OPEN); reset_wire();
    unsigned opened = opens;
    char document[] = "/tmp/ptouch-denied-png-XXXXXX";
    write_png(document, 85, 40, 41);
    ipp_t *q = printer_request(printer, IPP_OP_PRINT_JOB);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/png");
    char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    ipp_t *reply = cupsDoFileRequest(http, q, resource, document); unlink(document);
    assert(!reply && cupsLastError() == IPP_STATUS_ERROR_FORBIDDEN);
    assert(cut(printer) == IPP_STATUS_ERROR_FORBIDDEN);
    assert(wire_used == 0 && opens == opened && papplPrinterGetNumberOfJobs(printer) == 0);
    assert(pt_run_get(printer) == PT_RUN_OPEN);
}

int main(int argc, char **argv)
{
    /* An optional "deny" runs only the configured-printing-policy checks. */
    bool deny = argc == 3 && !strcmp(argv[2], "deny");
    assert(argc <= 2 || deny);
    selected_model = pt_usb_supported_model(argc >= 2 ? argv[1] : NULL); assert(selected_model);
    bool p710bt = selected_model->pid == 0x20af;
    snprintf(selected_id, sizeof selected_id, "MFG:Brother;MDL:%s;CMD:PT-CBP;", selected_model->name);
    snprintf(selected_uri, sizeof selected_uri, "ptouch://Brother/%s", selected_model->name);
    pappl_pr_driver_t driver = {"brother_ptouch", "Test", NULL, NULL};
    papplDeviceAddScheme("fixture", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, dev_open, dev_close,
                         NULL, dev_write, dev_status, dev_id);
    papplDeviceAddScheme("ptouch", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, dev_open, dev_close,
                         NULL, dev_write, dev_status, dev_id);
    papplDeviceAddScheme("ptouch-bt", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, dev_open, dev_close,
                         NULL, dev_write, dev_status, dev_id);
    port = 20000 + getpid() % 20000;
    pappl_system_t *sys = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "finishing-test", port,
                                          NULL, NULL, "/tmp/ptouch-driver-test.log", PAPPL_LOGLEVEL_ERROR, NULL, false);
    assert(sys);
    papplSystemSetOperationCallback(sys, pt_cut_op_cb, NULL);
    if (deny) papplSystemSetAuthCallback(sys, "Basic", deny_printing, NULL);
    /* Recognition metadata cannot select the PT-2730 protocol for another model. */
    const char *unsupported_ids[] = {"MFG:Brother;MDL:PT-P700;", "MFG:Brother;MDL:unknown;", ""};
    for (size_t i = 0; i < sizeof unsupported_ids / sizeof unsupported_ids[0]; i++) {
        pappl_pr_driver_data_t rejected = {0};
        ipp_t *attrs = NULL;
        assert(!pt_driver_cb(sys, "brother_ptouch", "socket://127.0.0.1:9",
                             unsupported_ids[i], &rejected, &attrs, NULL));
        assert(!rejected.rstartjob_cb && !rejected.printfile_cb && wire_used == 0);
    }
    /* Mismatched or malformed USB identities are rejected before commands. */
    const char *bad_uris[] = {
        selected_model->pid == 0x20af ? "ptouch://Brother/PT-2730" : "ptouch://Brother/PT-P710BT",
        "ptouch://Brother/PT-P710BT/extra", "ptouch://Brother/PT-unknown"
    };
    for (size_t i = 0; i < sizeof bad_uris / sizeof bad_uris[0]; i++) {
        pappl_pr_driver_data_t rejected = {0}; ipp_t *attrs = NULL;
        assert(!pt_driver_cb(sys, "brother_ptouch", bad_uris[i], selected_id, &rejected, &attrs, NULL));
        assert(!rejected.extension && wire_used == 0);
    }
    papplSystemSetPrinterDrivers(sys, 1, &driver, NULL, NULL, fixture_driver, NULL);
    pappl_printer_t *p = papplPrinterCreate(sys, 0, "test", "brother_ptouch",
                         selected_id, "socket://127.0.0.1:9"); assert(p);
    papplSystemAddMIMEFilter(sys, "image/png", "image/png", pt_filter_png, NULL);
    assert(papplSystemAddListeners(sys, "localhost"));
    char network[INET_ADDRSTRLEN] = "";
    if (p710bt && !deny) {
        if (!network_address(network, sizeof network))
            fprintf(stderr, "No non-loopback IPv4 address for the network-peer authorization check\n");
        assert(*network && papplSystemAddListeners(sys, network));
    }
    pthread_t server; assert(!pthread_create(&server, NULL, run_server, sys));
    http = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL);
    assert(http);
    pappl_device_t *d = papplDeviceOpen("fixture://test", "fixture", NULL, NULL); assert(d);
    if (deny) {
        assert(p710bt);
        pappl_printer_t *denied = papplPrinterCreate(sys, 0, "denied", "brother_ptouch",
                                  selected_id, "fixture://denied"); assert(denied);
        denied_cases(denied);
        papplDeviceClose(d); httpClose(http); papplSystemShutdown(sys);
        pthread_join(server, NULL); papplSystemDelete(sys);
        puts("driver printing-policy checks passed");
        return 0;
    }
    /* USB presence cannot change the transport selected for this printer. */
    reset_wire();
    unsigned enumeration_before = usb_enumerations, reads_before = usb_reads;
    assert(usb_present && status_cb(p));
    pt_live_read incompatible = pt_read_live_tape(d, "socket://127.0.0.1:9");
    assert(incompatible.n < 0 && !incompatible.parsed);
    assert(usb_enumerations == enumeration_before && usb_reads == reads_before && wire_used == 0);
    if (selected_model->pid == 0x20af) {
        /* Golden production-callback setup, then the first page of a held run.
         * Heights come from the destination header, independent of job totals. */
        pt_run_set(p, PT_RUN_CLOSED);
        for (unsigned continuation = 0; continuation < 2; continuation++) {
            pappl_job_t *setup_job = job(p, "none", 1, false);
            pappl_pr_options_t *setup_options = begin(setup_job, d);
            setup_options->header.cupsHeight = 180 + 36 * continuation;
            reset_wire(); assert(r_startpage(setup_job, setup_options, d, 0));
            unsigned char setup[] = {
                27, 'i', 'a', 1, 27, 'i', '!', 0,
                27, 'i', 'z', 0x84, 0, 12, 0, 0, 0, 0, 0, 0, 0,
                27, 'i', 'M', 0x40, 27, 'i', 'K', 0,
                27, 'i', 'd', 14, 0, 'M', 2
            };
            unsigned rows = setup_options->header.cupsHeight - 2u * PT_END_MARGIN_PX;
            for (unsigned i = 0; i < 4; i++) setup[15 + i] = (unsigned char)(rows >> (8 * i));
            setup[19] = (unsigned char)continuation;
            unsigned char expected[138] = {0}; size_t prefix = continuation ? 0 : 102;
            if (!continuation) { expected[100] = 27; expected[101] = '@'; }
            memcpy(expected + prefix, setup, sizeof setup);
            assert(wire_used == prefix + sizeof setup && !memcmp(wire, expected, wire_used));
            unsigned char pixels[32] = {0}; size_t before = wire_used;
            /* The first logical rows are feed margins; the first transmitted row is 14. */
            assert(r_writeline(setup_job, setup_options, d, PT_END_MARGIN_PX, pixels));
            unsigned char row[20] = {'G', 17, 0, 15};
            assert(wire_used == before + sizeof row && !memcmp(wire + before, row, sizeof row));
            assert(r_endpage(setup_job, setup_options, d, 0)); end(setup_job, setup_options, d);
            assert(pt_chain_get(p));
        }
        pt_run_set(p, PT_RUN_CLOSED); reset_wire();
    }
    setenv("PTOUCH_CUT_MODE", "each", 1); /* Must not override an explicit none. */
    setenv("PTOUCH_PRECUT", "0", 1); /* Retired: mandatory precut cannot be disabled. */
    const char *choices[] = {NULL, "none", "{finishing-template=none}"};
    for (int i = 0; i < 3; i++) {
        pt_run_set(p, PT_RUN_CLOSED); reset_wire();
        pappl_job_t *j = job(p, choices[i], 1, i == 2);
        pappl_pr_options_t *o = begin(j, d);
        assert(page_test(j, o, d)); end(j, o, d);
        expect_wire(1, 1, i ? 1 : 0, i ? 0 : 1);
        assert(pt_chain_get(p) == (i != 0));
    }
    /* A completed none job remains open across handle close/reopen. */
    assert(pt_chain_get(p));
    papplDeviceClose(d); d = papplDeviceOpen("fixture://test", "fixture", NULL, NULL); assert(d);
    reset_wire(); setenv("PTOUCH_CUT_MODE", "none", 1);
    pappl_job_t *trim = job(p, "trim", 3, false);
    pappl_pr_options_t *o = begin(trim, d); assert(o->finishings & PAPPL_FINISHINGS_TRIM);
    for (int i = 0; i < 3; i++) assert(page_test(trim, o, d));
    end(trim, o, d); expect_wire(0, 3, 2, 1); assert(!pt_chain_get(p));
    reset_wire(); trim = job(p, "{finishing-template=trim}", 1, true);
    o = begin(trim, d); assert(o->finishings & PAPPL_FINISHINGS_TRIM);
    assert(page_test(trim, o, d)); end(trim, o, d); expect_wire(1, 1, 0, 1);

    /* Buffered acceptance cannot hide failed raster or terminal transfers. */
    const int failures[] = {0x47, 0x0c, 0x1a};
    for (int i = 0; i < 3; i++) {
        pt_run_set(p, PT_RUN_CLOSED); reset_wire();
        pappl_job_t *j = job(p, i == 2 ? "trim" : "none", 1, false);
        o = begin(j, d); assert(r_startpage(j, o, d, 0));
        unsigned char pixels[32] = {0};
        unsigned wy = is_p710bt() ? PT_END_MARGIN_PX : 0;
        fail_opcode = failures[i];
        if (i == 0) {
            assert(!r_writeline(j, o, d, wy, pixels));
            end(j, o, d);
        } else {
            assert(r_writeline(j, o, d, wy, pixels)); assert(r_endpage(j, o, d, 0));
            assert(!r_endjob(j, o, d) && !papplJobGetData(j));
            size_t failed_output = wire_used;
            assert(r_endjob(j, o, d) && wire_used == failed_output);
            papplJobDeletePrintOptions(o);
        }
        assert(!strcmp(papplJobGetMessage(j),
                       "Write to printer failed; output uncertain: power-cycle printer, then restart server"));
        assert(pt_run_get(p) == PT_RUN_UNCERTAIN);
        reset_wire();
        pappl_job_t *follower = job(p, "trim", 1, false);
        pappl_pr_options_t *next = papplJobCreatePrintOptions(follower, 1, false);
        assert(!r_startjob(follower, next, d) && wire_used == 0);
        assert(status_cb(p) && pt_run_get(p) == PT_RUN_UNCERTAIN && wire_used == 0);
        end(follower, next, d);
    }
    /* Late chain-off is part of finishing; failure cannot proceed to EJECT. */
    pt_run_set(p, PT_RUN_CLOSED); reset_wire();
    pappl_job_t *late_job = job(p, "trim", 1, false);
    pappl_pr_options_t *late_options = begin(late_job, d); assert(page_test(late_job, late_options, d));
    size_t before_late = wire_used; fail_advanced = 8;
    assert(!r_endjob(late_job, late_options, d) && wire_used == before_late);
    assert(pt_run_get(p) == PT_RUN_UNCERTAIN);
    assert(r_endjob(late_job, late_options, d) && wire_used == before_late);
    papplJobDeletePrintOptions(late_options);
    /* A failed intermediate FF does not configure or raster the next label. */
    pt_run_set(p, PT_RUN_CLOSED); reset_wire();
    pappl_job_t *ff_job = job(p, "trim", 1, false);
    pappl_pr_options_t *ff_options = begin(ff_job, d); assert(page_test(ff_job, ff_options, d));
    size_t before_ff = wire_used; fail_opcode = 12;
    assert(!r_startpage(ff_job, ff_options, d, 1) && wire_used == before_ff);
    assert(pt_run_get(p) == PT_RUN_UNCERTAIN); end(ff_job, ff_options, d);
    pt_run_set(p, PT_RUN_CLOSED); reset_wire();
    pappl_job_t *j = job(p, "none", 1, false); o = begin(j, d);
    assert(r_startpage(j, o, d, 0)); papplJobCancel(j);
    assert(!r_endpage(j, o, d, 0)); end(j, o, d); expect_wire(1, 0, 0, 0);

    /* Cancellation after complete raster also leaves its print command pending. */
    pt_run_set(p, PT_RUN_CLOSED); reset_wire();
    pappl_job_t *pending_cancel = job(p, "trim", 1, false);
    pappl_pr_options_t *pending_options = begin(pending_cancel, d); assert(page_test(pending_cancel, pending_options, d));
    size_t pending_size = wire_used; papplJobCancel(pending_cancel);
    end(pending_cancel, pending_options, d);
    assert(wire_used == pending_size && pt_run_get(p) == PT_RUN_UNCERTAIN);
    /* Actual USB status parsing consumes all phases, including fragmented packets. */
    pappl_printer_t *usb = papplPrinterCreate(sys, 0, "usb", "brother_ptouch",
                       selected_id, selected_uri); assert(usb);
    j = job(usb, "none", 1, false);
    for (size_t chunk = 7; chunk <= 32; chunk += 25) {
        ready_status(0, chunk); assert(pt_wait_ready(j, d)); assert(status_used == status_size);
    }
    ready_status(4, 32); assert(!pt_wait_ready(j, d));
    ready_status(0, 32); statuses[0] = 0; assert(!pt_wait_ready(j, d));
    papplJobCancel(j); ready_status(0, 32); assert(!pt_wait_ready(j, d)); assert(status_used == 0);

    /* The deferred intermediate and final commands both consume complete
     * receive-ready phases, including fragmented USB packets. */
    pt_run_set(usb, PT_RUN_CLOSED); reset_wire();
    ready_status(0, 7); statuses[18] = statuses[19] = 0; status_size = 32;
    pappl_job_t *usb_batch = job(usb, "trim-after-job", 1, false);
    pappl_pr_options_t *usb_options = begin(usb_batch, d);
    assert(page_test(usb_batch, usb_options, d));
    ready_status(0, 7); assert(page_test(usb_batch, usb_options, d)); assert(status_used == status_size);
    ready_status(0, 7); end(usb_batch, usb_options, d); assert(status_used == status_size);
    assert(pt_run_get(usb) == PT_RUN_CLOSED);
    /* A readiness error after the print command still blocks the next batch. */
    ready_status(0, 32); statuses[18] = statuses[19] = 0; status_size = 32;
    usb_batch = job(usb, "none", 1, false); usb_options = begin(usb_batch, d);
    assert(page_test(usb_batch, usb_options, d)); ready_status(4, 7);
    assert(!r_endjob(usb_batch, usb_options, d) && pt_run_get(usb) == PT_RUN_UNCERTAIN);
    size_t after_status_failure = wire_used;
    assert(r_endjob(usb_batch, usb_options, d) && wire_used == after_status_failure);
    papplJobDeletePrintOptions(usb_options);
    pt_run_set(usb, PT_RUN_CLOSED); /* Explicit recovery simulated without hardware. */
    /* Existing offline-media refresh still happens before geometry is built. */
    usb_present = false; pt_chain_set(usb, true); assert(status_cb(usb));
    assert(!pt_chain_get(usb) && !pt_media_confirmed(usb));
    assert(papplPrinterGetReasons(usb) & PAPPL_PREASON_OFFLINE);
    usb_present = true; ready_status(0, 32);
    /* A stale notification with the old width precedes the requested snapshot. */
    statuses[19] = 0;
    statuses[32 + 10] = 24; statuses[32 + 18] = statuses[32 + 19] = 0;
    status_size = 64;
    reset_wire(); j = job(usb, "none", 1, false);
    assert(pt_refresh_ready_media(j, d));
    assert(status_used == 64);
    pappl_pr_driver_data_t dd; papplPrinterGetDriverData(usb, &dd);
    assert(dd.media_default.size_width == 2400 && pt_media_confirmed(usb));
    o = papplJobCreatePrintOptions(j, 1, false); assert(o->header.cupsWidth == 170);
    o->header.cupsWidth++; reset_wire();
    ready_status(0, 32); statuses[10] = 24; statuses[18] = statuses[19] = 0; status_size = 32;
    assert(r_startjob(j, o, d)); reset_wire();
    assert(!r_startpage(j, o, d, 0)); assert(wire_used == 0); end(j, o, d);
    assert(!pt_png_printfile(j, NULL, d)); assert(wire_used == 0);

    /* Every supported tape publishes a usable default and an exact window. */
    const int tapes[] = {6, 9, 12, 18, 24};
    for (unsigned i = 0; i < sizeof tapes / sizeof tapes[0]; i++) {
        pt_geometry g; pappl_media_col_t media;
        assert(pt_tape_geometry(tapes[i], &g) && media_col_for(tapes[i], 10000, 180, &media));
        assert(papplPrinterSetReadyMedia(p, 1, &media));
        pappl_job_t *geometry_job = job(p, "none", 1, false);
        pappl_pr_options_t *geometry_options = papplJobCreatePrintOptions(geometry_job, 1, false);
        int left = geometry_options->media.left_margin * 180 / 2540;
        int span = (int)geometry_options->header.cupsWidth -
            (geometry_options->media.left_margin + geometry_options->media.right_margin) * 180 / 2540;
        assert(left == g.left_px && span == g.imageable_px);
        pt_run_set(p, PT_RUN_CLOSED); reset_wire();
        assert(r_startjob(geometry_job, geometry_options, d));
        geometry_options->header.cupsWidth++;
        assert(!r_startpage(geometry_job, geometry_options, d, 0) && wire_used == 0);
        assert(pt_run_get(p) == PT_RUN_CLOSED);
        end(geometry_job, geometry_options, d);
    }
    pappl_media_col_t original_media;
    assert(media_col_for(12, 10000, 180, &original_media));
    assert(papplPrinterSetReadyMedia(p, 1, &original_media));

    /* Invalid/canceled followers before setup preserve a confirmed open run. */
    pt_run_set(p, PT_RUN_OPEN); reset_wire();
    pappl_job_t *invalid_follower = job(p, "trim", 1, false);
    pappl_pr_options_t *invalid_options = papplJobCreatePrintOptions(invalid_follower, 1, false);
    assert(r_startjob(invalid_follower, invalid_options, d));
    invalid_options->header.cupsWidth++;
    assert(!r_startpage(invalid_follower, invalid_options, d, 0));
    assert(pt_run_get(p) == PT_RUN_OPEN && wire_used == 0);
    end(invalid_follower, invalid_options, d);
    pappl_job_t *canceled_follower = job(p, "trim", 1, false);
    pappl_pr_options_t *canceled_options = papplJobCreatePrintOptions(canceled_follower, 1, false);
    assert(r_startjob(canceled_follower, canceled_options, d));
    papplJobCancel(canceled_follower);
    assert(!r_startpage(canceled_follower, canceled_options, d, 0));
    assert(pt_run_get(p) == PT_RUN_OPEN && wire_used == 0);
    end(canceled_follower, canceled_options, d);

    pt_run_set(p, PT_RUN_CLOSED); reset_wire();
    pappl_job_t *bad_next = job(p, "trim", 1, false);
    pappl_pr_options_t *bad_options = begin(bad_next, d); assert(page_test(bad_next, bad_options, d));
    size_t before_bad = wire_used; bad_options->header.cupsWidth++;
    assert(!r_startpage(bad_next, bad_options, d, 1) && wire_used == before_bad);
    end(bad_next, bad_options, d); assert(wire_used == before_bad && pt_run_get(p) == PT_RUN_UNCERTAIN);
    /* Fault publication cannot turn present hardware into a ready printer. */
    usb_matches = 2; reset_wire(); assert(status_cb(usb));
    assert(wire_used == 0 && (papplPrinterGetReasons(usb) & PAPPL_PREASON_OFFLINE));
    usb_matches = 1; can_open = false; assert(status_cb(usb)); can_open = true;
    assert(!pt_media_confirmed(usb) && (papplPrinterGetReasons(usb) & PAPPL_PREASON_OFFLINE));
    /* A real job owns the device when a status poll races its scheduler.
     * OpenDevice returns NULL while processing, without a USB claim failure. */
    pappl_pr_driver_data_t before_busy; papplPrinterGetDriverData(usb, &before_busy);
    ready_status(0, 32); statuses[18] = statuses[19] = 0; status_size = 32;
    assert(status_cb(usb) && pt_media_confirmed(usb));
    ready_status(0, 32);
    memmove(statuses + 64, statuses, 96);
    memcpy(statuses + 32, statuses, 32);
    statuses[18] = statuses[19] = 0;
    statuses[32 + 18] = statuses[32 + 19] = 0;
    status_size = 160; reset_wire(); pt_run_set(usb, PT_RUN_CLOSED);
    pappl_job_t *busy_job = upload_png(usb, "none", 1, false);
    poll_during_job = usb; busy_poll_checked = false;
    assert(papplJobRelease(busy_job, "test"));
    for (int i = 0; i < 1000 && papplJobGetState(busy_job) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    poll_during_job = NULL;
    assert(busy_poll_checked && papplJobGetState(busy_job) == IPP_JSTATE_COMPLETED);
    assert(pt_media_confirmed(usb) && !(papplPrinterGetReasons(usb) & PAPPL_PREASON_OFFLINE));
    assert(papplPrinterSetReadyMedia(usb, 1, before_busy.media_ready));
    pt_run_set(usb, PT_RUN_CLOSED);
    const int fault_tapes[] = {0, 36, 12, 12};
    for (unsigned i = 0; i < sizeof fault_tapes / sizeof fault_tapes[0]; i++) {
        ready_status(i == 2 ? 4 : 0, 32);
        statuses[10] = (unsigned char)fault_tapes[i]; statuses[18] = statuses[19] = 0;
        if (i == 2) statuses[8] = 4;
        if (i == 3) statuses[0] = 0;
        status_size = 32; reset_wire();
        assert(status_cb(usb) && !pt_media_confirmed(usb));
        assert(papplPrinterGetReasons(usb) & (PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED | PAPPL_PREASON_OTHER | PAPPL_PREASON_OFFLINE));
        ready_status(i == 2 ? 4 : 0, 32);
        statuses[10] = (unsigned char)fault_tapes[i]; statuses[18] = statuses[19] = 0;
        if (i == 2) statuses[8] = 4;
        if (i == 3) statuses[0] = 0;
        status_size = 32;
        pappl_job_t *fault_job = job(usb, "none", 1, false);
        pappl_pr_options_t *fault_options = papplJobCreatePrintOptions(fault_job, 1, false);
        reset_wire(); assert(!r_startjob(fault_job, fault_options, d));
        assert(wire_used == 3 && !memcmp(wire, "\x1b\x69\x53", 3));
        end(fault_job, fault_options, d);
    }
    /* Neither clean status nor reopening clears an uncertain output latch. */
    pt_run_set(usb, PT_RUN_UNCERTAIN); ready_status(0, 32); reset_wire();
    assert(status_cb(usb) && wire_used == 0 && status_used == 0);
    pappl_job_t *blocked = job(usb, "trim", 1, false);
    pappl_pr_options_t *blocked_options = papplJobCreatePrintOptions(blocked, 1, false);
    assert(!r_startjob(blocked, blocked_options, d) && wire_used == 0);
    assert(pt_run_get(usb) == PT_RUN_UNCERTAIN);
    end(blocked, blocked_options, d);
    /* A fresh server/fixture state after the operator's power-cycle. */
    pt_run_set(usb, PT_RUN_CLOSED);
    wait_cases(usb, d);
    /* A failed status request names its cause and sends no page output. */
    reset_wire(); fail_opcode = 27;
    pappl_job_t *unsent = job(usb, "none", 1, false);
    pappl_pr_options_t *unsent_options = papplJobCreatePrintOptions(unsent, 1, false);
    assert(!r_startjob(unsent, unsent_options, d) && wire_used == 0);
    assert(!strcmp(papplJobGetMessage(unsent), "Status request to printer failed; nothing was printed"));
    end(unsent, unsent_options, d); reset_wire();

    /* Cassette detection must survive an open none run without ESC @. */
    pappl_printer_t *poll = papplPrinterCreate(sys, 0, "poll", "brother_ptouch",
                       selected_id, selected_uri); assert(poll);
    const int widths[] = {12, 24, 0};
    for (int i = 0; i < 3; i++) {
        ready_status(0, 32); statuses[10] = widths[i]; statuses[18] = statuses[19] = 0;
        status_size = 32; reset_wire(); pt_chain_set(poll, true);
        assert(status_cb(poll)); assert(status_used == 32);
        assert(wire_used == 3 && !memcmp(wire, "\x1b\x69\x53", 3));
        assert(pt_chain_get(poll) == (i == 0));
        if (widths[i]) {
            papplPrinterGetDriverData(poll, &dd);
            assert(dd.media_ready[0].size_width == widths[i] * 100);
        } else {
            assert(papplPrinterGetReasons(poll) & PAPPL_PREASON_MEDIA_EMPTY);
            assert(!pt_media_confirmed(poll));
        }
    }
    /* Query before PNG geometry too, even when the previous run was confirmed. */
    ready_status(0, 32); statuses[10] = 12; statuses[18] = statuses[19] = 0;
    status_size = 32; reset_wire(); pt_chain_set(usb, true);
    j = job(usb, "none", 1, false);
    assert(pt_refresh_ready_media(j, d)); assert(status_used == 32);
    assert(wire_used == 3 && !memcmp(wire, "\x1b\x69\x53", 3));
    assert(!pt_chain_get(usb));
    o = papplJobCreatePrintOptions(j, 1, false); assert(o->header.cupsWidth == 85);
    papplJobDeletePrintOptions(o);

    if (p710bt) {
        cut_cases(usb, bluetooth_cases(sys, usb, d), d);
        pappl_printer_t *remote = papplPrinterCreate(sys, 0, "remote", "brother_ptouch",
                                  selected_id, "fixture://remote"); assert(remote);
        remote_cases(remote, network);
    } else {
        cut_unsupported(usb);
    }

    /* State belongs to each queue, with no arbitrary eight-printer ceiling. */
    for (int i = 0; i < 10; i++) {
        char name[32]; snprintf(name, sizeof name, "owned-%d", i);
        pappl_printer_t *owned = papplPrinterCreate(sys, 0, name, "brother_ptouch",
                               selected_id, "socket://127.0.0.1:9");
        assert(owned && pt_state(owned) && pt_state(owned) != pt_state(p));
        assert(pt_run_get(owned) == PT_RUN_CLOSED && !pt_media_confirmed(owned));
    }
    /* PNG rendering and actual IPP enum/collection resolution on a TCP sink. */
    sink_listener = socket(AF_INET, SOCK_STREAM, 0); assert(sink_listener >= 0);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(!bind(sink_listener, (struct sockaddr *)&address, sizeof address));
    socklen_t length = sizeof address;
    assert(!getsockname(sink_listener, (struct sockaddr *)&address, &length));
    assert(!listen(sink_listener, 1));
    char socket_uri[128]; snprintf(socket_uri, sizeof socket_uri, "socket://127.0.0.1:%u", ntohs(address.sin_port));
    pappl_printer_t *capture = papplPrinterCreate(sys, 0, "capture", "brother_ptouch",
                              selected_id, socket_uri); assert(capture);
    png_socket_case(capture, NULL, 1, false, 1, 0, 1);
    png_socket_case(capture, "none", 3, false, 1, 3, 0);
    png_socket_case(capture, "malformed", 1, false, 0, 0, 0);
    assert(pt_chain_get(capture));
    pappl_job_t *canceled = job(capture, "trim", 1, false); papplJobCancel(canceled);
    assert(pt_chain_get(capture)); reject_unsupported_finishing(capture);
    assert(pt_chain_get(capture));
    sleep(4); /* Beyond the removed idle cutter's old three-second threshold. */
    png_socket_case(capture, "{finishing-template=none}", 1, true, 0, 1, 0);
    png_socket_case(capture, "trim", 3, false, 0, 2, 1);
    png_socket_case(capture, "{finishing-template=trim}", 1, true, 1, 0, 1);
    /* Both jobs are runnable before printing starts: trim must still cut. */
    papplPrinterPause(capture);
    pappl_job_t *first = upload_png(capture, "trim", 1, false);
    pappl_job_t *follower = upload_png(capture, "trim", 1, false);
    assert(papplJobRelease(first, "test") && papplJobRelease(follower, "test"));
    assert(papplJobGetState(first) == IPP_JSTATE_PENDING);
    assert(papplJobGetState(follower) == IPP_JSTATE_PENDING);
    reset_wire(); pthread_t queued_sink;
    assert(!pthread_create(&queued_sink, NULL, socket_sink, &sink_listener));
    papplPrinterResume(capture);
    /* PAPPL starts the newest pending job first, so either may finish last. */
    for (int i = 0; i < 1000 && (papplJobGetState(first) < IPP_JSTATE_CANCELED ||
                                 papplJobGetState(follower) < IPP_JSTATE_CANCELED); i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(first) == IPP_JSTATE_COMPLETED);
    assert(papplJobGetState(follower) == IPP_JSTATE_COMPLETED);
    pthread_join(queued_sink, NULL); expect_wire(2, tx(180) * 2, 0, 2);
    const char *presets[] = {"ptouch-chain", "trim-after-pages", "none", "trim-after-job"};
    const pt_cut_mode modes[] = {PT_CUT_CHAIN, PT_CUT_EACH, PT_CUT_NONE, PT_CUT_END};
    check_capabilities(capture);
    check_media_database(capture);
    reject_finishing_requests(capture);
    ipp_t *unchanged_default = printer_response(capture, printer_request(capture, IPP_OP_GET_PRINTER_ATTRIBUTES));
    assert(ippGetInteger(ippFindAttribute(unchanged_default, "finishings-default", IPP_TAG_ENUM), 0) == 11);
    ippDelete(unchanged_default);
    for (unsigned i = 0; i < 4; i++) {
        for (int copies = 1; copies <= 3; copies += 2) {
            pt_run_set(capture, PT_RUN_CLOSED);
            bool final = i == 1 || i == 3;
            png_socket_case(capture, presets[i], copies, i == 0, 1,
                             copies - (int)final, (int)final);
            const unsigned lengths[] = {180, 180, 180};
            expect_policy((unsigned)copies, lengths, modes[i]);
        }
        for (int pages = 1; pages <= 3; pages++) {
            pt_run_set(capture, PT_RUN_CLOSED);
            raster_socket_case(capture, presets[i], i == 0, pages, 1, 1, CUPS_RASTER_WRITE_PWG, false);
        }
        pt_run_set(capture, PT_RUN_CLOSED);
        raster_socket_case(capture, presets[i], i == 0, 3, 1, 8, CUPS_RASTER_WRITE_PWG, false);
        pt_run_set(capture, PT_RUN_CLOSED);
        raster_socket_case(capture, presets[i], i == 0, 3, 1, 8, CUPS_RASTER_WRITE_APPLE, false);
        pt_run_set(capture, PT_RUN_CLOSED);
        raster_socket_case(capture, presets[i], i == 0, 2, 1, 1, CUPS_RASTER_WRITE_PWG, 2);
        pt_run_set(capture, PT_RUN_CLOSED); /* Simulate explicit recovery after partial output. */
        /* Registered collection templates must execute the same policy. */
        if (i != 0) {
            pt_run_set(capture, PT_RUN_CLOSED);
            png_socket_case(capture, presets[i], 3, true, 1, 3 - (int)(i != 2), i != 2);
            const unsigned lengths[] = {180, 180, 180}; expect_policy(3, lengths, modes[i]);
        }
    }
    pt_run_set(capture, PT_RUN_CLOSED);
    png_socket_case(capture, "combined", 3, false, 1, 2, 1);
    const unsigned combined_lengths[] = {180, 180, 180}; expect_policy(3, combined_lengths, PT_CUT_EACH);
    /* Every preset transition: final modes reset once; continuing modes do not. */
    for (unsigned from = 0; from < 4; from++) for (unsigned to = 0; to < 4; to++) {
        pt_run_set(capture, PT_RUN_CLOSED);
        png_socket_case(capture, presets[from], 1, from == 0, 1, from == 0 || from == 2, from == 1 || from == 3);
        png_socket_case(capture, presets[to], 1, to == 0, from == 1 || from == 3, to == 0 || to == 2, to == 1 || to == 3);
        const unsigned length[] = {180}; expect_policy(1, length, modes[to]);
    }
    set_default(capture, 63, "trim-after-job"); pt_run_set(capture, PT_RUN_CLOSED);
    png_socket_case(capture, NULL, 3, false, 1, 2, 1);
    const unsigned defaults_lengths[] = {180, 180, 180}; expect_policy(3, defaults_lengths, PT_CUT_END);
    set_default(capture, 3, NULL); pt_run_set(capture, PT_RUN_CLOSED);
    png_socket_case(capture, NULL, 3, false, 1, 3, 0); expect_policy(3, defaults_lengths, PT_CUT_NONE);
    set_default(capture, 60, NULL); pt_run_set(capture, PT_RUN_CLOSED);
    png_socket_case(capture, NULL, 3, false, 1, 2, 1); expect_policy(3, defaults_lengths, PT_CUT_EACH);
    set_default(capture, 11, NULL);
    pt_run_set(capture, PT_RUN_CLOSED);
    raster_socket_case(capture, "trim", false, 1, 2, 1, CUPS_RASTER_WRITE_PWG, false);
    /* Malformed EOF cannot release the pending complete first label. */
    pt_run_set(capture, PT_RUN_CLOSED);
    raster_socket_case(capture, "trim", false, 1, 1, 1, CUPS_RASTER_WRITE_PWG, true);
    pt_run_set(capture, PT_RUN_CLOSED); /* Simulate documented explicit recovery. */
    /* 36 mm and 50 mm pages reconstruct their physical length and marker row. */
    raster_length_case(capture, 255);
    raster_length_case(capture, 354);
    if (is_p710bt()) {
        const unsigned end_ink_rows[] = {0, 179}; /* leading and trailing margins */
        for (unsigned i = 0; i < 2; i++) {
            pt_run_set(capture, PT_RUN_CLOSED);
            raster_end_ink_case(capture, end_ink_rows[i]);
            pt_run_set(capture, PT_RUN_CLOSED); /* Simulate documented explicit recovery. */
        }
    }
    pappl_printer_t *counter = papplPrinterCreate(sys, 0, "counter", "brother_ptouch",
                              selected_id, "fixture://counter"); assert(counter);
    counting_png_case(counter, "none", 65, PT_CUT_NONE);
    counting_png_case(counter, "trim", 999, PT_CUT_EACH);
    close(sink_listener);
    papplDeviceClose(d); httpClose(http); papplSystemShutdown(sys);
    /* Let the listener poll finish: opening a wake connection creates a detached
     * PAPPL client that can still access sys after the server thread exits. */
    pthread_join(server, NULL); papplSystemDelete(sys);
    puts("driver finishing and transfer checks passed");
    return 0;
}
