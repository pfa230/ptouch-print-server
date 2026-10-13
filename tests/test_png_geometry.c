/* Real IPP image jobs and the production raster callbacks, captured without USB. */
/* Tests must execute assertions in optimized builds too. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <arpa/inet.h>
#include <png.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "driver.h"
#include "filter_png.h"
#include "tables.h"

static unsigned char wire[512000];
static size_t used;
static unsigned fail_row;
static unsigned written_rows;
static bool truncate_png;
static pappl_job_t *last_job;
static const char *selected_model;
static bool packbits;
const pt_dev *pt_usb_supported_model(const char *name)
{
    if (!name || !*name) name = "PT-2730";
    return !strcmp(name, "PT-2730") || !strcmp(name, "PT-P710BT") ? pt_lookup_name(name) : NULL;
}
const pt_dev *pt_usb_model_from_uri(const char *uri) { (void)uri; return NULL; }
bool pt_usb_present(uint16_t vid, uint16_t pid) { (void)vid; (void)pid; return false; }
ssize_t pt_usb_count(uint16_t vid, uint16_t pid) { (void)vid; (void)pid; return 0; }
bool pt_usb_is_uri(const char *uri) { (void)uri; return false; }
ssize_t pt_usb_read_status(pappl_device_t *d, const char *uri, void *buffer, size_t bytes)
{ (void)d; (void)uri; (void)buffer; (void)bytes; assert(!"Unexpected USB read"); return -1; }
static bool device_open(pappl_device_t *d, const char *uri, const char *name)
{ (void)d; (void)uri; (void)name; return true; }
static void device_close(pappl_device_t *d) { (void)d; }
static ssize_t device_write(pappl_device_t *d, const void *buffer, size_t n)
{
    (void)d;
    const unsigned char *p = buffer;
    if (n && p[0] == 'G' && ++written_rows == fail_row) return -1;
    assert(used + n <= sizeof wire); memcpy(wire + used, buffer, n); used += n; return n;
}
static pappl_preason_t device_status(pappl_device_t *d) { (void)d; return PAPPL_PREASON_NONE; }
static char *device_id(pappl_device_t *d, char *b, size_t n)
{ (void)d; snprintf(b, n, "MFG:Brother;MDL:%s;", selected_model); return b; }
static bool printer_status(pappl_printer_t *p) { (void)p; return true; }
static bool driver(pappl_system_t *s, const char *name, const char *uri, const char *id,
                   pappl_pr_driver_data_t *data, ipp_t **attrs, void *cb)
{
    bool ok = pt_driver_cb(s, name, uri, id, data, attrs, cb);
    data->status_cb = printer_status; return ok;
}
static void *server(void *s) { papplSystemRun(s); return NULL; }
static http_t *http;
static int port;
static const char resource[] = "/ipp/print/png";
static void operation(ipp_t *request)
{
    char uri[128]; snprintf(uri, sizeof uri, "ipp://localhost:%d%s", port, resource);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "test");
}
/* Command lengths bound raster payloads, including opcode bytes in PackBits. */
static unsigned rows(unsigned char image[][128], unsigned *finishes)
{
    unsigned y = 0, page_rows = 0, expected_rows = 0;
    bool compressed = false, fresh = false;
    *finishes = 0;
    for (size_t pos = 0; pos < used;) {
        size_t n;
        if (wire[pos] == 'G') {
            assert(used - pos >= 3 && wire[pos + 2] == 0 && y < 7000);
            n = 3 + wire[pos + 1]; assert(n <= used - pos);
            unsigned char head[16]; size_t decoded = 0;
            if (compressed) {
                for (size_t i = pos + 3; i < pos + n;) {
                    unsigned control = wire[i++];
                    size_t count = control < 128 ? control + 1 : 257 - control;
                    if (control == 128) continue;
                    assert(decoded + count <= sizeof head);
                    if (control < 128) {
                        assert(count <= pos + n - i);
                        memcpy(head + decoded, wire + i, count); i += count;
                    } else {
                        assert(i < pos + n); memset(head + decoded, wire[i++], count);
                    }
                    decoded += count;
                }
            } else {
                assert(!packbits && n == 19); memcpy(head, wire + pos + 3, 16); decoded = 16;
            }
            assert(decoded == sizeof head);
            if (image) for (unsigned x = 0; x < 128; x++)
                image[y][x] = !!(head[15 - x / 8] & (1u << (x % 8)));
            y++; page_rows++;
        } else if (wire[pos] == 12 || wire[pos] == 26) {
            if (packbits) assert(page_rows == expected_rows);
            page_rows = 0; ++*finishes; n = 1;
        } else if (wire[pos] == 0) {
            assert(packbits); n = 1; /* Fresh-run invalidation. */
        } else if (wire[pos] == 'M') {
            assert(packbits && used - pos >= 2 && wire[pos + 1] == 2);
            compressed = true; n = 2;
        } else {
            assert(wire[pos] == 27 && used - pos >= 2);
            if (wire[pos + 1] == '@') { n = 2; fresh = true; compressed = false; }
            else {
                assert(used - pos >= 3 && wire[pos + 1] == 'i');
                unsigned op = wire[pos + 2];
                n = op == 'c' ? 8 : op == 'z' ? 13 : op == 'S' ? 3 : op == 'd' ? 5 : 4;
                assert(n <= used - pos);
                if (op == 'z') {
                    assert(packbits && wire[pos + 3] == 0x84);
                    expected_rows = (unsigned)wire[pos + 7] | (unsigned)wire[pos + 8] << 8 |
                        (unsigned)wire[pos + 9] << 16 | (unsigned)wire[pos + 10] << 24;
                    assert(wire[pos + 11] == (fresh ? 0 : 1)); fresh = false;
                } else if (op == 'c' || op == 'R') assert(!packbits);
                else if (op == 'a') assert(packbits && wire[pos + 3] == 1);
                else if (op == '!') assert(packbits && wire[pos + 3] == 0);
                else assert(op == 'M' || op == 'K' || op == 'S' || op == 'd');
            }
        }
        assert(n <= used - pos); pos += n;
    }
    return y;
}
/* Generated all-white 76x180 grayscale JPEG with 180 dpi metadata. */
static const unsigned char jpeg_fixture[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0xb4,
    0x00, 0xb4, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0xb4,
    0x00, 0x4c, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00,
    0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
    0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda,
    0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0xfe, 0xfe, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a,
    0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28,
    0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2,
    0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a,
    0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28,
    0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2,
    0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a,
    0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28,
    0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2,
    0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a,
    0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28,
    0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xaf, 0xff, 0xd9,
};
static void write_png(const char *path, int width, int height, int xppi, int yppi,
                      const unsigned char *pixels)
{
    FILE *file = fopen(path, "wb"); assert(file);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png); assert(png && info && !setjmp(png_jmpbuf(png)));
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (xppi) png_set_pHYs(png, info, (unsigned)(xppi / .0254 + .5),
                         (unsigned)(yppi / .0254 + .5), PNG_RESOLUTION_METER);
    png_write_info(png, info);
    for (int y = 0; y < height; y++) png_write_row(png, (png_bytep)(pixels + (size_t)y * width));
    png_write_end(png, info); png_destroy_write_struct(&png, &info); fclose(file);
}
/* media 0 omitted, 1 named50mm, 2 dimensions50mm, 3 source-only, 4 margin-only,
 * 5 explicit10mm, 6 wrong tape18mm, 7 collection named50mm,
 * 8 collection named25mm (too short), 9 collection named18mm (wrong tape),
 * 10/11 custom12x40mm quantized by -/+1 centimm with explicit297 margins. */
static void run_image(pappl_printer_t *printer, int width, int height, int xppi, int yppi,
                      const unsigned char *pixels, int orientation, int media, int copies,
                      const char *scaling, bool success, unsigned expected_rows)
{
    char path[] = "/tmp/ptouch-png-geometry-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    if (pixels) write_png(path, width, height, xppi, yppi, pixels);
    else { FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(jpeg_fixture, 1, sizeof jpeg_fixture, f) == sizeof jpeg_fixture); fclose(f); }
    if (truncate_png) {
        FILE *f = fopen(path, "rb"); assert(f); assert(!fseek(f, 0, SEEK_END));
        long size = ftell(f); fclose(f); assert(size > 12 && !truncate(path, size - 12));
    }
    ipp_t *request = ippNewRequest(IPP_OP_CREATE_JOB); operation(request);
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-hold-until", NULL, "indefinite");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", copies);
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", IPP_FINISHINGS_NONE);
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-scaling", NULL, scaling);
    if (orientation) ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "orientation-requested", orientation);
    if (media == 1) ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media", NULL, "om_12x50mm_12x50mm");
    else if (media) {
        ipp_t *col = ippNew();
        if (media >= 7 && media <= 9) ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-size-name", NULL,
                                    media == 8 ? "om_12x25mm_12x25mm" :
                                    media == 9 ? "om_18x50mm_18x50mm" : "om_12x50mm_12x50mm");
        else if (media == 3) ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-source", NULL, "main-roll");
        else if (media == 4) ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-top-margin", 0);
        else {
            ipp_t *size = ippNew();
            ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", media == 6 ? 1800 : media == 10 ? 1199 : media == 11 ? 1201 : 1200);
            ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", media == 5 ? 1000 : media >= 10 ? 3986 : 5000);
            ippAddCollection(col, IPP_TAG_ZERO, "media-size", size); ippDelete(size);
            if (media >= 10) {
                ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-left-margin", 297);
                ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-right-margin", 297);
            }
        }
        ippAddCollection(request, IPP_TAG_JOB, "media-col", col); ippDelete(col);
    }
    ipp_t *response = cupsDoRequest(http, request, resource);
    if (!response || ippGetStatusCode(response) > IPP_STATUS_OK_EVENTS_COMPLETE)
        fprintf(stderr, "create geometry job: %s\n", cupsLastErrorString());
    assert(response && ippGetStatusCode(response) <= IPP_STATUS_OK_EVENTS_COMPLETE);
    int id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0); ippDelete(response);
    pappl_job_t *j = papplPrinterFindJob(printer, id); assert(j); last_job = j;
    request = ippNewRequest(IPP_OP_SEND_DOCUMENT); operation(request);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", id);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, pixels ? "image/png" : "image/jpeg");
    ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", true);
    response = cupsDoFileRequest(http, request, resource, path); unlink(path);
    assert(response && ippGetStatusCode(response) <= IPP_STATUS_OK_EVENTS_COMPLETE); ippDelete(response);
    used = written_rows = 0; assert(papplJobRelease(j, "test"));
    for (int i = 0; i < 1000 && papplJobGetState(j) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000}; nanosleep(&delay, NULL);
    }
    if (papplJobGetState(j) != (success ? IPP_JSTATE_COMPLETED : IPP_JSTATE_ABORTED))
        fprintf(stderr, "PNG %dx%d media%d orientation%d: state%d message%s\n", width, height, media, orientation, papplJobGetState(j), papplJobGetMessage(j));
    assert(papplJobGetState(j) == (success ? IPP_JSTATE_COMPLETED : IPP_JSTATE_ABORTED));
    unsigned finishes;
    unsigned actual_rows = rows(NULL, &finishes);
    if (actual_rows != expected_rows)
        fprintf(stderr, "PNG media%d: got %u raster rows, expected %u\n", media, actual_rows, expected_rows);
    assert(actual_rows == expected_rows);
    if (!success) assert(!finishes);
}
static void run_model(const char *model, int index)
{
    selected_model = model; packbits = !strcmp(model, "PT-P710BT");
    assert(!setenv("PTOUCH_MODEL", model, 1));
    used = fail_row = written_rows = 0;
    pappl_pr_driver_t drv = {"ptouch", "Test", NULL, NULL};
    papplDeviceAddScheme("pngfixture", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, device_open,
        device_close, NULL, device_write, device_status, device_id);
    port = 30000 + getpid() % 15000 + index;
    pappl_system_t *sys = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "png-geometry", port,
        NULL, NULL, "/tmp/ptouch-png-geometry.log", PAPPL_LOGLEVEL_ERROR, NULL, false); assert(sys);
    papplSystemSetPrinterDrivers(sys, 1, &drv, NULL, NULL, driver, NULL);
    papplSystemAddMIMEFilter(sys, "image/png", "image/png", pt_filter_png, NULL);
    char id[128]; snprintf(id, sizeof id, "MFG:Brother;MDL:%s;", model);
    pappl_printer_t *p = papplPrinterCreate(sys, 0, "png", "ptouch", id, "pngfixture://capture"); assert(p);
    assert(papplSystemAddListeners(sys, "localhost"));
    pthread_t thread; assert(!pthread_create(&thread, NULL, server, sys));
    http = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL); assert(http);
    ipp_t *query = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES); operation(query);
    ipp_t *reply = cupsDoRequest(http, query, resource); assert(reply);
    ipp_attribute_t *formats = ippFindAttribute(reply, "document-format-supported", IPP_TAG_MIMETYPE);
    assert(formats); int png_count = 0;
    for (int i = 0; i < ippGetCount(formats); i++) {
        const char *format = ippGetString(formats, i, NULL);
        png_count += !strcmp(format, "image/png");
        assert(strcmp(format, "image/vnd.ptouch-raster"));
    }
    assert(png_count == 1); ippDelete(reply);
    run_image(p, 76, 180, 180, 180, NULL, 3, 1, 1, "none", true, 354);
    run_image(p, 76, 180, 180, 180, NULL, 3, 2, 1, "none", true, 354);
    run_image(p, 76, 180, 180, 180, NULL, 3, 0, 1, "none", true, 708);
    unsigned char *pixels = malloc(3000 * 300); assert(pixels);
    memset(pixels, 255, 3000 * 300);
    pixels[5 * 76 + 2] = pixels[17 * 76 + 70] = 0;
    for (int m = 0; m <= 4; m++) run_image(p, 76, 180, 180, 180, pixels, 3, m, 1, "none", true, m == 1 || m == 2 ? 354 : 180);
    run_image(p, 76, 180, 180, 180, pixels, 3, 7, 1, "none", true, 354);
    run_image(p, 76, 180, 180, 180, pixels, 3, 8, 1, "none", false, 0);
    run_image(p, 76, 180, 180, 180, pixels, 3, 9, 1, "none", false, 0);
    run_image(p, 42, 180, 180, 180, pixels, 3, 10, 1, "none", true, 282);
    run_image(p, 42, 180, 180, 180, pixels, 3, 11, 1, "none", true, 282);
    run_image(p, 76, 180, 180, 180, pixels, 3, 10, 1, "none", false, 0);
    assert(strstr(papplJobGetMessage(last_job), "printable width is 42"));
    run_image(p, 76, 180, 0, 0, pixels, 3, 0, 3, "none", true, 540);
    run_image(p, 76, 180, 180, 180, pixels, 3, 5, 1, "none", false, 0);
    run_image(p, 76, 180, 180, 180, pixels, 3, 6, 1, "none", false, 0);
    run_image(p, 76, 180, 180, 360, pixels, 3, 0, 1, "none", false, 0);
    memset(pixels, 255, 3000 * 300); pixels[0] = pixels[200 * 299 + 199] = 0;
    run_image(p, 200, 300, 180, 180, pixels, 3, 0, 1, "none", false, 0);
    memset(pixels, 0, 200 * 300);
    const char *scales[] = {"fit", "auto-fit", "auto", "fill"};
    static unsigned char scaled[7000][128];
    for (int c = 0; c < 4; c++) {
        run_image(p, 200, 300, 180, 180, pixels, 3, 2, 1, scales[c], true, 354);
        unsigned finished; assert(rows(scaled, &finished) == 354);
        for (unsigned y = 0; y < 354; y++) for (unsigned x = 0; x < 128; x++) {
            bool black = x >= 26 && x < 102 && (c == 3 || (y >= 120 && y < 234));
            assert(scaled[y][x] == black);
        }
    }
    /* Compare every head dot of every raster row for asymmetric orientation markers. */
    static unsigned char actual[7000][128];
    const int orientations[] = {3, 6, 4, 5, 0};
    for (int c = 0; c < 5; c++) {
        int o = orientations[c], w = c >= 2 ? 180 : 76, h = c >= 2 ? 76 : 180;
        memset(pixels, 255, w * h);
        int mx[] = {2, w - 3, 11}, my[] = {5, h - 7, 19};
        for (int i = 0; i < 3; i++) pixels[my[i] * w + mx[i]] = 0;
        run_image(p, w, h, 180, 180, pixels, o, 0, 1, "none", true, 180);
        unsigned finishes; assert(rows(actual, &finishes) == 180);
        unsigned char expected[180][128] = {{0}};
        for (int i = 0; i < 3; i++) {
            int x, y;
            if (o == 3) { x = mx[i]; y = my[i]; }
            else if (o == 6) { x = w - 1 - mx[i]; y = h - 1 - my[i]; }
            else if (o == 4) { x = my[i]; y = w - 1 - mx[i]; }
            else { x = h - 1 - my[i]; y = mx[i]; }
            expected[y][26 + x] = 1;
        }
        if (memcmp(actual, expected, sizeof expected))
            for (unsigned y = 0; y < 180; y++) for (unsigned x = 0; x < 128; x++)
                if (actual[y][x] != expected[y][x]) fprintf(stderr, "orientation%d dot%u,%u actual%d expected%d\n", o, x, y, actual[y][x], expected[y][x]);
        assert(!memcmp(actual, expected, sizeof expected));
    }
    unsigned finishes;
    pt_geometry g; assert(pt_tape_geometry(12, &g));
    for (int c = 0; c < 4; c++) {
        int o = orientations[c], w = c < 2 ? g.nominal_px : 180, h = c < 2 ? 180 : g.nominal_px;
        int first = o == 6 || o == 5 ? g.nominal_px - g.right_px : g.left_px;
        memset(pixels, 255, w * h);
        int mx[] = {c < 2 ? first : 5, c < 2 ? first + g.imageable_px - 1 : 171};
        int my[] = {c < 2 ? 5 : first, c < 2 ? 171 : first + g.imageable_px - 1};
        for (int i = 0; i < 2; i++) pixels[my[i] * w + mx[i]] = 0;
        run_image(p, w, h, 180, 180, pixels, o, 0, 1, "none", true, 180);
        rows(actual, &finishes);
        unsigned char expected[180][128] = {{0}};
        for (int i = 0; i < 2; i++) {
            int x, y;
            if (o == 3) { x = mx[i] - first; y = my[i]; }
            else if (o == 6) { x = g.imageable_px - 1 - (mx[i] - first); y = h - 1 - my[i]; }
            else if (o == 4) { x = my[i] - first; y = w - 1 - mx[i]; }
            else { x = g.imageable_px - 1 - (my[i] - first); y = mx[i]; }
            expected[y][26 + x] = 1;
        }
        assert(!memcmp(actual, expected, sizeof expected));
        pixels[0] = 0;
        run_image(p, w, h, 180, 180, pixels, o, 0, 1, "none", false, 0);
    }
    memset(pixels, 255, 152 * 360);
    for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
        pixels[(10 + dy) * 152 + 4 + dx] = 0;
    run_image(p, 152, 360, 360, 360, pixels, 3, 0, 1, "none", true, 180);
    rows(actual, &finishes); assert(actual[5][28]);
    memset(pixels, 255, 3000 * 300);
    run_image(p, 76, 1, 180, 180, pixels, 3, 0, 1, "none", true, 35);
    run_image(p, 76, 2125, 180, 180, pixels, 3, 0, 1, "none", true, 2125);
    run_image(p, 76, 2126, 180, 180, pixels, 3, 0, 1, "none", false, 0);
    /* Decoded full-width outlines exercise the common window on all five tapes. */
    pappl_pr_driver_data_t ready; papplPrinterGetDriverData(p, &ready);
    pappl_media_col_t original_media = ready.media_ready[0];
    const int tapes[] = {6, 9, 12, 18, 24};
    for (unsigned i = 0; i < sizeof tapes / sizeof tapes[0]; i++) {
        pt_geometry geometry; assert(pt_tape_geometry(tapes[i], &geometry));
        pappl_media_col_t media = original_media;
        media.size_width = tapes[i] * 100;
        media.left_margin = geometry.left_margin_centimm;
        media.right_margin = geometry.right_margin_centimm;
        snprintf(media.size_name, sizeof media.size_name, "custom_%dx100mm_%dx100mm", tapes[i], tapes[i]);
        assert(papplPrinterSetReadyMedia(p, 1, &media));
        int width = geometry.imageable_px;
        memset(pixels, 255, width * 180);
        for (int y = 0; y < 180; y++) for (int x = 0; x < width; x++)
            if (!y || y == 179 || !x || x == width - 1) pixels[y * width + x] = 0;
        run_image(p, width, 180, 180, 180, pixels, 3, 0, 1, "none", true, 180);
        assert(rows(actual, &finishes) == 180);
        int start = (128 - width) / 2;
        for (int y = 0; y < 180; y++) for (int x = 0; x < 128; x++)
            assert(actual[y][x] == (x >= start && x < start + width &&
                (!y || y == 179 || x == start || x == start + width - 1)));
    }
    assert(papplPrinterSetReadyMedia(p, 1, &original_media));
    truncate_png = true;
    run_image(p, 76, 180, 180, 180, pixels, 3, 0, 1, "none", false, 0);
    truncate_png = false;
    fail_row = 7;
    run_image(p, 76, 180, 180, 180, pixels, 3, 0, 1, "none", false, 6);
    assert(written_rows == 7); /* No later raster follows the failing callback. */
    size_t before_cleanup = used;
    pappl_pr_driver_data_t dd; papplPrinterGetDriverData(p, &dd);
    pappl_pr_options_t *options = papplJobCreatePrintOptions(last_job, 1, false); assert(options);
    assert(dd.rendjob_cb(last_job, options, NULL));
    assert(dd.rendjob_cb(last_job, options, NULL));
    assert(used == before_cleanup && !papplJobGetData(last_job));
    papplJobDeletePrintOptions(options);
    free(pixels); httpClose(http); papplSystemShutdown(sys);
    /* Do not add a detached PAPPL client while the system is shutting down. */
    pthread_join(thread, NULL); papplSystemDelete(sys);
    puts("PNG IPP geometry, copies, blank margins, scaling, and complete raster markers passed");
}
int main(void)
{
    run_model("PT-2730", 0);
    run_model("PT-P710BT", 1);
    return 0;
}
