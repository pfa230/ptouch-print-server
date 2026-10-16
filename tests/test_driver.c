/* Actual PAPPL options/objects and buffered device API, with an injected wire.
 * Include the adapter to exercise its private raster callbacks without a server. */
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <png.h>
#include <fcntl.h>
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

static unsigned char wire[65536], statuses[160];
static size_t wire_used, status_used, status_size, status_chunk = 32;
static int fail_opcode = -1, fail_advanced = -1;
static bool counting;
static const pt_dev *selected_model;
static char selected_id[128], selected_uri[128];
static struct { size_t rows, ff, eject, modes, cuts, final, infos, inits; } counts;
static bool usb_present = true, can_open = true;
static ssize_t usb_matches = 1;
static unsigned usb_enumerations, usb_reads;
static pappl_printer_t *poll_during_job;
static bool busy_poll_checked;

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
ssize_t pt_usb_read_status(pappl_device_t *d, const char *uri, void *buffer, size_t bytes)
{
    (void)d; (void)uri;
    usb_reads++;
    size_t n = status_size - status_used;
    if (n > bytes) n = bytes;
    if (n > status_chunk) n = status_chunk;
    memcpy(buffer, statuses + status_used, n);
    status_used += n;
    return (ssize_t)n;
}
static bool dev_open(pappl_device_t *d, const char *uri, const char *name)
{ (void)d; (void)uri; (void)name; return can_open; }
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
    if (n && p[0] == fail_opcode) return -1;
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
    assert(wire_used + n <= sizeof wire);
    memcpy(wire + wire_used, buffer, n);
    wire_used += n;
    return (ssize_t)n;
}
static pappl_preason_t dev_status(pappl_device_t *d) { (void)d; return PAPPL_PREASON_NONE; }
static char *dev_id(pappl_device_t *d, char *buffer, size_t n)
{ (void)d; snprintf(buffer, n, "%s", selected_id); return buffer; }

static void reset_wire(void) { wire_used = 0; fail_opcode = fail_advanced = -1; }
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

static pappl_job_t *job(pappl_printer_t *p, const char *finishing, int copies, bool collection)
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
    ipp_t *response = cupsDoRequest(http, request, resource);
    if (!response || ippGetStatusCode(response) > IPP_STATUS_OK_EVENTS_COMPLETE)
        fprintf(stderr, "Create-Job failed: %s (%s)\n", cupsLastErrorString(), resource);
    assert(response && ippGetStatusCode(response) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);
    ippDelete(response);
    pappl_job_t *j = papplPrinterFindJob(p, id); assert(j);
    return j;
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

/* Real IPP upload, Release-Job scheduling, PNG filter, and TCP device output.
 * Only media/status are fixed; every printing callback is the production one. */
static pappl_job_t *upload_png(pappl_printer_t *p, const char *finishing, int copies,
                               bool collection)
{
    bool malformed = finishing && !strcmp(finishing, "malformed");
    pappl_job_t *j = job(p, finishing, copies, collection);
    char filename[] = "/tmp/ptouch-finishing-png-XXXXXX";
    int fd = mkstemp(filename); assert(fd >= 0);
    FILE *file = fdopen(fd, "wb"); assert(file);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png); assert(png && info);
    assert(!setjmp(png_jmpbuf(png)));
    png_init_io(png, file);
    png_set_IHDR(png, info, 85, 180, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    unsigned char pixels[85]; memset(pixels, 255, sizeof pixels);
    pixels[40] = pixels[41] = 0;
    for (int i = 0; i < 180; i++) png_write_row(png, pixels);
    png_write_end(png, info); png_destroy_write_struct(&png, &info); fclose(file);
    if (malformed) {
        file = fopen(filename, "wb"); assert(file);
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

static void raster_socket_case(pappl_printer_t *printer, const char *finishing,
                                bool collection, int pages, int copies, int bits, cups_mode_t mode,
                                int damage)
{
    /* The P710BT's third page must stay above its 24.5 mm physical minimum. */
    const unsigned lengths[] = {180, 360, is_p710bt() ? 200 : 90};
    char path[] = "/tmp/ptouch-batch-raster-XXXXXX";
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
            if (damage == 2) row[0] = y == 0 || y == header.cupsHeight - 1 ? 128 : 0;
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
        /* The P710BT rejects ink in a replaced end row before any raster leaves. */
        if (is_p710bt())
            expect_wire(1, damage == 1 ? tx(180) : 0, 0, 0);
        else
            expect_wire(1, damage == 1 ? 180 : 539, damage == 1 ? 0 : 1, 0);
        assert(pt_run_get(printer) == PT_RUN_UNCERTAIN);
    } else {
        pt_cut_mode selected = !strcmp(finishing, "none") ? PT_CUT_NONE :
                               !strcmp(finishing, "ptouch-chain") ? PT_CUT_CHAIN :
                               !strcmp(finishing, "trim-after-job") ? PT_CUT_END : PT_CUT_EACH;
        expect_policy((unsigned)pages, lengths, selected);
    }
}

/* A raster page below the model's physical minimum fails before any output,
 * leaving no uncertain run because page setup was never sent. */
static void raster_below_min_case(pappl_printer_t *printer, unsigned height)
{
    char path[] = "/tmp/ptouch-batch-short-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    cups_raster_t *raster = cupsRasterOpen(fd, CUPS_RASTER_WRITE_PWG); assert(raster);
    cups_page_header2_t header;
    pwg_media_t media = {"custom", NULL, NULL, 1200, (int)height * 2540 / 180};
    assert(cupsRasterInitPWGHeader(&header, &media, "black_1", 180, 180, "one-sided", "normal"));
    header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 0;
    header.cupsHeight = height;
    assert(cupsRasterWriteHeader2(raster, &header));
    unsigned char row[256] = {0}; assert(header.cupsBytesPerLine <= sizeof row);
    for (unsigned y = 0; y < height; y++)
        assert(cupsRasterWritePixels(raster, row, header.cupsBytesPerLine) == header.cupsBytesPerLine);
    cupsRasterClose(raster); close(fd);
    char resource[128], uri[256];
    snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
    snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ipp_t *request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/pwg-raster");
    reset_wire();
    ipp_t *reply = cupsDoFileRequest(http, request, resource, path); unlink(path);
    assert(reply && ippGetStatusCode(reply) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(reply, "job-id", IPP_TAG_INTEGER), 0); ippDelete(reply);
    pappl_job_t *job = papplPrinterFindJob(printer, id); assert(job);
    for (unsigned i = 0; i < 1000 && papplJobGetState(job) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(job) == IPP_JSTATE_ABORTED);
    assert(wire_used == 0 && pt_run_get(printer) == PT_RUN_CLOSED);
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
    int expected_min = is_p710bt() ? PT_P710BT_MIN_CENTIMM : PT_ROLL_MIN_CENTIMM;
    int expected_end = is_p710bt() ? PT_END_MARGIN_CENTIMM : 0;
    ipp_t *request = printer_request(printer, IPP_OP_GET_PRINTER_ATTRIBUTES);
    const char *requested[] = {"media-col-database", "media-size-supported", "media-left-margin-supported", "media-right-margin-supported",
                               "media-top-margin-supported", "media-bottom-margin-supported"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", 6, NULL, requested);
    ipp_t *reply = printer_response(printer, request);
    ipp_attribute_t *db = ippFindAttribute(reply, "media-col-database", IPP_TAG_BEGIN_COLLECTION);
    ipp_attribute_t *sizes = ippFindAttribute(reply, "media-size-supported", IPP_TAG_BEGIN_COLLECTION);
    assert(db && ippGetCount(db) == 20 && sizes && ippGetCount(sizes) == 5);
    unsigned db_names = 0;
    for (ipp_attribute_t *a = ippFirstAttribute(reply); a; a = ippNextAttribute(reply))
        if (ippGetName(a) && !strcmp(ippGetName(a), "media-col-database")) db_names++;
    assert(db_names == 1);
    /* End margins are model-wide and must agree with the media entries. */
    assert(ippContainsInteger(ippFindAttribute(reply, "media-top-margin-supported", IPP_TAG_INTEGER), expected_end));
    assert(ippContainsInteger(ippFindAttribute(reply, "media-bottom-margin-supported", IPP_TAG_INTEGER), expected_end));
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
            assert(ippGetInteger(ippFindAttribute(col, "media-top-margin", IPP_TAG_INTEGER), 0) == expected_end);
            assert(ippGetInteger(ippFindAttribute(col, "media-bottom-margin", IPP_TAG_INTEGER), 0) == expected_end);
            ipp_attribute_t *length = ippFindAttribute(size, "y-dimension", IPP_TAG_ZERO); assert(length);
            if (ippGetValueTag(length) == IPP_TAG_RANGE) {
                int high; assert(ippGetRange(length, 0, &high) == expected_min && high == PT_ROLL_MAX_CENTIMM); roll++;
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
        assert(ippGetRange(ippFindAttribute(range, "y-dimension", IPP_TAG_RANGE), 0, &high) == expected_min && high == PT_ROLL_MAX_CENTIMM);
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
    /* The usable length range rejects a below-minimum explicit page length. */
    const int length_cases[] = {expected_min, expected_min - 100};
    for (unsigned i = 0; i < 2; i++) {
        ipp_t *q = printer_request(printer, IPP_OP_VALIDATE_JOB);
        ippAddBoolean(q, IPP_TAG_OPERATION, "ipp-attribute-fidelity", true);
        ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/png");
        ipp_t *col = ippNew(), *size = ippNew();
        ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", 1200);
        ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", length_cases[i]);
        ippAddCollection(col, IPP_TAG_ZERO, "media-size", size);
        ippAddCollection(q, IPP_TAG_JOB, "media-col", col); ippDelete(size); ippDelete(col);
        char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", papplPrinterGetName(printer));
        ipp_t *response = cupsDoRequest(http, q, resource); assert(response);
        assert(ippGetStatusCode(response) == (i == 0 ? IPP_STATUS_OK : IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES));
        ippDelete(response);
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

int main(int argc, char **argv)
{
    assert(argc <= 2);
    selected_model = pt_usb_supported_model(argc == 2 ? argv[1] : NULL); assert(selected_model);
    snprintf(selected_id, sizeof selected_id, "MFG:Brother;MDL:%s;CMD:PT-CBP;", selected_model->name);
    snprintf(selected_uri, sizeof selected_uri, "ptouch://Brother/%s", selected_model->name);
    pappl_pr_driver_t driver = {"brother_ptouch", "Test", NULL, NULL};
    papplDeviceAddScheme("fixture", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, dev_open, dev_close,
                         NULL, dev_write, dev_status, dev_id);
    papplDeviceAddScheme("ptouch", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, dev_open, dev_close,
                         NULL, dev_write, dev_status, dev_id);
    port = 20000 + getpid() % 20000;
    pappl_system_t *sys = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "finishing-test", port,
                                          NULL, NULL, "/tmp/ptouch-driver-test.log", PAPPL_LOGLEVEL_ERROR, NULL, false);
    assert(sys);
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
    pthread_t server; assert(!pthread_create(&server, NULL, run_server, sys));
    http = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL);
    assert(http);
    pappl_device_t *d = papplDeviceOpen("fixture://test", "fixture", NULL, NULL); assert(d);
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
        assert(pt_model_geometry(selected_model, tapes[i], &g) && media_col_for(selected_model, tapes[i], 10000, 180, &media));
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
    /* An explicit P710BT end margin below the required 198 centimm is rejected,
     * never silently replaced. */
    if (is_p710bt()) {
        const int bad_margins[] = {0, PT_END_MARGIN_CENTIMM - 1};
        for (unsigned m = 0; m < 2; m++) {
            pappl_job_t *margin_job = job(p, "none", 1, false);
            pappl_pr_options_t *margin_options = papplJobCreatePrintOptions(margin_job, 1, false);
            pt_run_set(p, PT_RUN_CLOSED); reset_wire();
            assert(r_startjob(margin_job, margin_options, d));
            margin_options->media.top_margin = bad_margins[m];
            margin_options->media.bottom_margin = PT_END_MARGIN_CENTIMM;
            assert(!r_startpage(margin_job, margin_options, d, 0) && wire_used == 0);
            assert(pt_run_get(p) == PT_RUN_CLOSED);
            end(margin_job, margin_options, d);
        }
    }
    pappl_media_col_t original_media;
    assert(media_col_for(selected_model, 12, 10000, 180, &original_media));
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
    for (int i = 0; i < 1000 && papplJobGetState(follower) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    assert(papplJobGetState(first) == IPP_JSTATE_COMPLETED);
    assert(papplJobGetState(follower) == IPP_JSTATE_COMPLETED);
    pthread_join(queued_sink, NULL); expect_wire(2, 360, 0, 2);
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
    raster_below_min_case(capture, is_p710bt() ? 173 : 34);
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
