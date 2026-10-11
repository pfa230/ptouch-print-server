/* Actual PAPPL options/objects and buffered device API, with an injected wire.
 * Include the adapter to exercise its private raster callbacks without a server. */
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <png.h>
#include "filter_png.h"
#include "../src/driver.c"

static unsigned char wire[65536], statuses[128];
static size_t wire_used, status_used, status_size, status_chunk = 32;
static int fail_opcode = -1;
static bool usb_present = true;

bool pt_usb_present(uint16_t vid, uint16_t pid) { (void)vid; (void)pid; return usb_present; }
ssize_t pt_usb_read_status(pappl_device_t *d, void *buffer, size_t bytes)
{
    (void)d;
    size_t n = status_size - status_used;
    if (n > bytes) n = bytes;
    if (n > status_chunk) n = status_chunk;
    memcpy(buffer, statuses + status_used, n);
    status_used += n;
    return (ssize_t)n;
}
static bool dev_open(pappl_device_t *d, const char *uri, const char *name)
{ (void)d; (void)uri; (void)name; return true; }
static void dev_close(pappl_device_t *d) { (void)d; }
static ssize_t dev_write(pappl_device_t *d, const void *buffer, size_t n)
{
    (void)d;
    const unsigned char *p = buffer;
    if (n && p[0] == fail_opcode) return -1;
    assert(wire_used + n <= sizeof wire);
    memcpy(wire + wire_used, buffer, n);
    wire_used += n;
    return (ssize_t)n;
}
static pappl_preason_t dev_status(pappl_device_t *d) { (void)d; return PAPPL_PREASON_NONE; }
static char *dev_id(pappl_device_t *d, char *buffer, size_t n)
{ (void)d; snprintf(buffer, n, "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;"); return buffer; }

static void reset_wire(void) { wire_used = 0; fail_opcode = -1; }
static void ready_status(int error, size_t chunk)
{
    memset(statuses, 0, sizeof statuses);
    /* Printing, then completed-but-still-printing, then receiving-ready. */
    for (int i = 0; i < 3; i++) {
        unsigned char *s = statuses + i * 32;
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
    if (finishing) {
        if (collection) {
            ipp_t *col = ippNew();
            ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL,
                         strstr(finishing, "trim") ? "trim" : "none");
            ippAddCollection(request, IPP_TAG_JOB, "finishings-col", col);
            ippDelete(col);
        } else {
            ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings",
                          !strcmp(finishing, "trim") ? IPP_FINISHINGS_TRIM : IPP_FINISHINGS_NONE);
        }
    }
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
    return r_startpage(j, o, d, 0) && r_writeline(j, o, d, 0, pixels) && r_endpage(j, o, d, 0);
}
static void end(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    size_t before = wire_used;
    assert(r_endjob(j, o, d)); assert(!papplJobGetData(j));
    assert(r_endjob(j, o, d)); assert(wire_used == before);
    papplJobDeletePrintOptions(o);
}
static void expect_wire(int inits, int rows, int ffs, int ejects, bool autocut)
{
    int actual_init = 0, actual_rows = 0, actual_ff = 0, actual_eject = 0;
    for (size_t pos = 0; pos < wire_used;) {
        if (wire[pos] == 0x47) { pos += 3 + wire[pos + 1]; actual_rows++; }
        else if (wire[pos] == 0x0c) { pos++; actual_ff++; }
        else if (wire[pos] == 0x1a) { pos++; actual_eject++; }
        else if (wire[pos] == 0x1b && wire[pos + 1] == 0x40) { pos += 2; actual_init++; }
        else {
            assert(wire[pos] == 0x1b && wire[pos + 1] == 0x69);
            if (wire[pos + 2] == 'M' && !autocut) assert(wire[pos + 3] == 0);
            pos += wire[pos + 2] == 'c' ? 8 : 4;
        }
        assert(pos <= wire_used);
    }
    if (actual_init != inits || actual_rows != rows || actual_ff != ffs || actual_eject != ejects)
        fprintf(stderr, "wire init/rows/FF/EJECT: %d/%d/%d/%d expected %d/%d/%d/%d\n",
                actual_init, actual_rows, actual_ff, actual_eject, inits, rows, ffs, ejects);
    assert(actual_init == inits && actual_rows == rows && actual_ff == ffs && actual_eject == ejects);
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
    expect_wire(inits, malformed ? 0 : 180 * copies, ffs, ejects, ejects != 0);
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

int main(void)
{
    pappl_pr_driver_t driver = {"brother_ptouch", "Test", NULL, NULL};
    papplDeviceAddScheme("fixture", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, dev_open, dev_close,
                         NULL, dev_write, dev_status, dev_id);
    port = 20000 + getpid() % 20000;
    pappl_system_t *sys = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "finishing-test", port,
                                          NULL, NULL, "/tmp/ptouch-driver-test.log", PAPPL_LOGLEVEL_ERROR, NULL, false);
    assert(sys);
    papplSystemSetPrinterDrivers(sys, 1, &driver, NULL, NULL, fixture_driver, NULL);
    pappl_printer_t *p = papplPrinterCreate(sys, 0, "test", "brother_ptouch",
                         "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;", "socket://127.0.0.1:9"); assert(p);
    papplSystemAddMIMEFilter(sys, "image/png", PT_RASTER_FORMAT, pt_filter_png, NULL);
    assert(papplSystemAddListeners(sys, "localhost"));
    pthread_t server; assert(!pthread_create(&server, NULL, run_server, sys));
    http = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL);
    assert(http);
    pappl_device_t *d = papplDeviceOpen("fixture://test", "fixture", NULL, NULL); assert(d);
    setenv("PTOUCH_CUT_MODE", "each", 1); /* Must not override an explicit none. */
    setenv("PTOUCH_PRECUT", "1", 1);
    const char *choices[] = {NULL, "none", "{finishing-template=none}"};
    for (int i = 0; i < 3; i++) {
        pt_chain_set(p, false); reset_wire();
        pappl_job_t *j = job(p, choices[i], 1, i == 2);
        pappl_pr_options_t *o = begin(j, d); assert(o->finishings == PAPPL_FINISHINGS_NONE);
        assert(page_test(j, o, d)); end(j, o, d);
        expect_wire(1, 1, 1, 0, false); assert(pt_chain_get(p));
    }
    /* A completed none job remains open across handle close/reopen and polling. */
    size_t before = wire_used;
    assert(status_cb(p)); assert(wire_used == before && pt_chain_get(p));
    papplDeviceClose(d); d = papplDeviceOpen("fixture://test", "fixture", NULL, NULL); assert(d);
    reset_wire(); setenv("PTOUCH_CUT_MODE", "none", 1);
    pappl_job_t *trim = job(p, "trim", 3, false);
    pappl_pr_options_t *o = begin(trim, d); assert(o->finishings & PAPPL_FINISHINGS_TRIM);
    for (int i = 0; i < 3; i++) assert(page_test(trim, o, d));
    end(trim, o, d); expect_wire(0, 3, 2, 1, true); assert(!pt_chain_get(p));
    reset_wire(); trim = job(p, "{finishing-template=trim}", 1, true);
    o = begin(trim, d); assert(o->finishings & PAPPL_FINISHINGS_TRIM);
    assert(page_test(trim, o, d)); end(trim, o, d); expect_wire(1, 1, 0, 1, true);

    /* Buffered acceptance cannot hide failed raster or terminal transfers. */
    const int failures[] = {0x47, 0x0c, 0x1a};
    for (int i = 0; i < 3; i++) {
        pt_chain_set(p, false); reset_wire();
        pappl_job_t *j = job(p, i == 2 ? "trim" : "none", 1, false);
        o = begin(j, d); assert(r_startpage(j, o, d, 0));
        unsigned char pixels[32] = {0};
        fail_opcode = failures[i];
        if (i == 0) assert(!r_writeline(j, o, d, 0, pixels));
        else { assert(r_writeline(j, o, d, 0, pixels)); assert(!r_endpage(j, o, d, 0)); }
        assert(pt_chain_get(p)); end(j, o, d);
    }
    pt_chain_set(p, false); reset_wire();
    pappl_job_t *j = job(p, "none", 1, false); o = begin(j, d);
    assert(r_startpage(j, o, d, 0)); papplJobCancel(j);
    assert(!r_endpage(j, o, d, 0)); end(j, o, d); expect_wire(1, 0, 0, 0, false);

    /* Actual USB status parsing consumes all phases, including fragmented packets. */
    pappl_printer_t *usb = papplPrinterCreate(sys, 0, "usb", "brother_ptouch",
                       "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;", "ptouch://Brother/PT-2730"); assert(usb);
    j = job(usb, "none", 1, false);
    for (size_t chunk = 7; chunk <= 32; chunk += 25) {
        ready_status(0, chunk); assert(pt_wait_ready(j, d)); assert(status_used == status_size);
    }
    ready_status(4, 32); assert(!pt_wait_ready(j, d));
    ready_status(0, 32); statuses[0] = 0; assert(!pt_wait_ready(j, d));
    papplJobCancel(j); ready_status(0, 32); assert(!pt_wait_ready(j, d)); assert(status_used == 0);

    /* Existing offline-media refresh still happens before geometry is built. */
    usb_present = false; pt_chain_set(usb, true); assert(status_cb(usb));
    assert(!pt_chain_get(usb) && !pt_media_confirmed(usb));
    assert(papplPrinterGetReasons(usb) & PAPPL_PREASON_OFFLINE);
    usb_present = true; ready_status(0, 32); statuses[10] = 24; status_size = 32;
    reset_wire(); j = job(usb, "none", 1, false);
    assert(pt_refresh_ready_media(j, d));
    pappl_pr_driver_data_t dd; papplPrinterGetDriverData(usb, &dd);
    assert(dd.media_default.size_width == 2400 && pt_media_confirmed(usb));
    o = papplJobCreatePrintOptions(j, 1, false); assert(o->header.cupsWidth == 170);
    o->header.cupsWidth++; reset_wire();
    assert(!r_startjob(j, o, d)); assert(wire_used == 0); end(j, o, d);
    assert(!pt_printfile_cb(j, NULL, d)); assert(wire_used == 0);

    /* State exhaustion is a failure before output, never an implicit cut. */
    memset(g_chain, 0, sizeof g_chain);
    for (int i = 0; i < PT_CHAIN_MAX; i++) g_chain[i].printer_id = 1000 + i;
    reset_wire(); j = job(p, "none", 1, false); o = papplJobCreatePrintOptions(j, 1, false);
    assert(!r_startjob(j, o, d)); assert(wire_used == 0); end(j, o, d);
    memset(g_chain, 0, sizeof g_chain);
    /* PNG rendering and actual IPP enum/collection resolution on a TCP sink. */
    sink_listener = socket(AF_INET, SOCK_STREAM, 0); assert(sink_listener >= 0);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(!bind(sink_listener, (struct sockaddr *)&address, sizeof address));
    socklen_t length = sizeof address;
    assert(!getsockname(sink_listener, (struct sockaddr *)&address, &length));
    assert(!listen(sink_listener, 1));
    char socket_uri[128]; snprintf(socket_uri, sizeof socket_uri, "socket://127.0.0.1:%u", ntohs(address.sin_port));
    pappl_printer_t *capture = papplPrinterCreate(sys, 0, "capture", "brother_ptouch",
                              "MFG:Brother;MDL:PT-2730;CMD:PT-CBP;", socket_uri); assert(capture);
    png_socket_case(capture, NULL, 1, false, 1, 1, 0);
    png_socket_case(capture, "none", 3, false, 0, 3, 0);
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
    pthread_join(queued_sink, NULL); expect_wire(2, 360, 0, 2, true);
    close(sink_listener);
    papplDeviceClose(d); httpClose(http); papplSystemShutdown(sys);
    pthread_join(server, NULL); papplSystemDelete(sys);
    puts("driver finishing and transfer checks passed");
    return 0;
}
