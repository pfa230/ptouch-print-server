/* Exercise pinned PAPPL through real IPP streaming and a TCP raster sink.
 * This deliberately uses a minimal driver: the regression belongs at PAPPL's
 * geometry/conversion boundary, independently of Brother finishing policy. */
#include <assert.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pappl/pappl.h>
#include <cups/raster.h>
#include <stddef.h>
#include <png.h>
#include <errno.h>

static unsigned job_starts, starts, rows, ends, clean_ends, abort_ends, end_calls;
static unsigned widths[4], heights[4], black_rows;
static int fail_row = -1, listener, port;
static bool fail_end;
static size_t sink_bytes;
static pappl_printer_t *printer;
static bool job_start(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{ (void)j; (void)o; (void)d; job_starts++; return true; }
static bool page_start(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{
    (void)j; (void)d; (void)p; assert(starts < 4);
    widths[starts] = o->header.cupsWidth; heights[starts++] = o->header.cupsHeight;
    assert(o->header.cupsBitsPerPixel == 1);
    assert(o->header.cupsBytesPerLine == (o->header.cupsWidth + 7) / 8);
    return true;
}
static bool line_write(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d,
                       unsigned y, const unsigned char *line)
{
    (void)j;
    if ((int)y == fail_row) return false;
    /* The raster fixtures place one black pixel at each longitudinal edge. */
    assert(line[0] == 0 || line[0] == 0x80);
    if (line[0]) assert(y == 0 || y == o->header.cupsHeight - 1);
    for (unsigned x = 1; x < o->header.cupsBytesPerLine; x++) assert(line[x] == 0);
    rows++; if (line[0] & 0x80) black_rows++;
    return papplDeviceWrite(d, line, o->header.cupsBytesPerLine) == o->header.cupsBytesPerLine;
}
static bool page_end(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{ (void)j; (void)o; (void)d; (void)p; ends++; return true; }
static bool job_end(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{
    (void)o; (void)d; end_calls++;
    if (papplJobIsCanceled(j) || papplJobGetState(j) == IPP_JSTATE_ABORTED) abort_ends++;
    else clean_ends++;
    return !fail_end;
}
static bool status(pappl_printer_t *p) { (void)p; return true; }
static bool driver(pappl_system_t *s, const char *n, const char *u, const char *id,
                   pappl_pr_driver_data_t *d, ipp_t **a, void *v)
{
    (void)s; (void)n; (void)u; (void)id; (void)a; (void)v;
    strcpy(d->make_and_model, "Raster boundary fixture"); d->kind = PAPPL_KIND_LABEL; d->ppm = 20;
    d->rstartjob_cb = job_start; d->rstartpage_cb = page_start; d->rwriteline_cb = line_write;
    d->rendpage_cb = page_end; d->rendjob_cb = job_end; d->status_cb = status;
    d->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
    d->color_default = PAPPL_COLOR_MODE_BI_LEVEL;
    d->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
    d->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;
    d->num_resolution = 1; d->x_resolution[0] = d->y_resolution[0] = 180;
    d->x_default = d->y_default = 180;
    d->num_source = 1; d->source[0] = "main-roll";
    d->num_type = 1; d->type[0] = "labels";
    d->num_media = 4; d->media[0] = "custom_12x100mm_12x100mm";
    d->media[1] = "roll_min_12x5mm"; d->media[2] = "roll_max_12x300mm";
    d->media[3] = "custom_12x25.4mm_12x25.4mm";
    strcpy(d->media_default.size_name, d->media[0]);
    strcpy(d->media_default.source, "main-roll");
    strcpy(d->media_default.type, "labels");
    d->media_default.tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;
    d->media_default.size_width = 1200; d->media_default.size_length = 10000;
    d->media_ready[0] = d->media_default;
    d->scaling_default = PAPPL_SCALING_NONE;
    return true;
}
static void *run(void *s) { papplSystemRun(s); return NULL; }
static void *sink(void *v)
{
    (void)v; int peer = accept(listener, NULL, NULL); assert(peer >= 0);
    char data[4096]; ssize_t n;
    while ((n = read(peer, data, sizeof data)) > 0) sink_bytes += (size_t)n;
    assert(n == 0); close(peer); return NULL;
}
static void fixture(const char *path, int bits, cups_mode_t mode, int pages, bool truncated)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600); assert(fd >= 0);
    cups_raster_t *r = cupsRasterOpen(fd, mode); assert(r);
    for (int p = 0; p < pages; p++) {
        cups_page_header2_t h;
        pwg_media_t media = {"custom_12x25.4mm", NULL, NULL, 1200, 2540 + p * 2540};
        assert(cupsRasterInitPWGHeader(&h, &media, bits == 1 ? "black_1" : bits == 8 ? "black_8" : "srgb_8", 180, 180, "one-sided", "normal"));
        if (mode == CUPS_RASTER_WRITE_APPLE) h.cupsColorSpace = CUPS_CSPACE_SW;
        h.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 0; /* Optional count omitted. */
        assert(cupsRasterWriteHeader2(r, &h));
        unsigned char line[256]; assert(h.cupsBytesPerLine <= sizeof line);
        for (unsigned y = 0; y < h.cupsHeight; y++) {
            memset(line, mode == CUPS_RASTER_WRITE_APPLE ? 255 : 0, h.cupsBytesPerLine);
            if (y == 0 || y == h.cupsHeight - 1)
                line[0] = mode == CUPS_RASTER_WRITE_APPLE ? 0 : bits == 1 ? 128 : 255;
            assert(cupsRasterWritePixels(r, line, h.cupsBytesPerLine) == h.cupsBytesPerLine);
        }
    }
    cupsRasterClose(r); close(fd);
    if (truncated) {
        fd = open(path, O_RDWR); assert(fd >= 0);
        off_t size = lseek(fd, 0, SEEK_END); assert(size > 8);
        assert(!ftruncate(fd, size - 3)); close(fd);
    }
}
static ssize_t failing_raster_read(void *ctx, unsigned char *buffer, size_t bytes)
{
    size_t n = fread(buffer, 1, bytes, ctx);
    if (n) return (ssize_t)n;
    errno = EIO;
    return -1;
}
static void decoder_io_failure(const char *path, cups_mode_t mode)
{
    fixture(path, 8, mode, 1, false);
    FILE *file = fopen(path, "rb"); assert(file);
    cups_raster_t *r = cupsRasterOpenIO(failing_raster_read, file, CUPS_RASTER_READ); assert(r);
    cups_page_header2_t header; assert(cupsRasterReadHeader2(r, &header));
    unsigned char line[256]; assert(header.cupsBytesPerLine <= sizeof line);
    for (unsigned y = 0; y < header.cupsHeight; y++)
        assert(cupsRasterReadPixels(r, line, header.cupsBytesPerLine) == header.cupsBytesPerLine);
    assert(!cupsRasterReadHeader2(r, &header));
    const char *error = cupsRasterErrorString();
    assert(error && strstr(error, "Unable to read raster stream"));
    cupsRasterClose(r); fclose(file);
}
static void execute(http_t *h, const char *path, const char *format, int explicit_size,
                    bool success, unsigned expected_rows, unsigned expected_pages)
{
    job_starts = starts = rows = ends = clean_ends = abort_ends = end_calls = black_rows = 0; sink_bytes = 0;
    ipp_t *q = ippNewRequest(IPP_OP_PRINT_JOB); char uri[256];
    snprintf(uri, sizeof uri, "ipp://localhost:%d/ipp/print/test", port);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "raster-test");
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
    if (explicit_size == 1) ippAddString(q, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media", NULL, "custom_12x100mm_12x100mm");
    if (explicit_size > 1) {
        ipp_t *media = ippNew(), *size = ippNew();
        if (explicit_size >= 4)
            ippAddString(media, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-size-name", NULL,
                         explicit_size == 5 ? "custom_12x25.4mm_12x25.4mm" : "custom_12x100mm_12x100mm");
        if (explicit_size < 4 || explicit_size == 6) {
            ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", 1200);
            ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", explicit_size == 2 ? 10000 : 2540);
            ippAddCollection(media, IPP_TAG_ZERO, "media-size", size);
        }
        ippAddCollection(q, IPP_TAG_JOB, "media-col", media);
        ippDelete(size); ippDelete(media);
    }
    pthread_t reader; assert(!pthread_create(&reader, NULL, sink, NULL));
    ipp_t *response = cupsDoFileRequest(h, q, "/ipp/print/test", path); assert(response);
    /* Streaming processing finishes before Print-Job returns. */
    pthread_join(reader, NULL);
    int id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);
    pappl_job_t *job = papplPrinterFindJob(printer, id); assert(job);
    for (int i = 0; i < 200 && papplJobGetState(job) < IPP_JSTATE_CANCELED; i++) {
        struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL);
    }
    fprintf(stderr, "%s: state=%d rows=%u pages=%u ends=%u/%u\n", path, papplJobGetState(job), rows, ends, clean_ends, abort_ends);
    assert(papplJobGetState(job) == (success ? IPP_JSTATE_COMPLETED : IPP_JSTATE_ABORTED));
    assert(rows == expected_rows && ends == expected_pages && end_calls == job_starts);
    if (explicit_size && !success && !expected_rows) assert(starts == 0);
    assert(clean_ends == (unsigned)(success || fail_end) && abort_ends == (unsigned)(!success && !fail_end && job_starts));
    if (success && strcmp(format, "image/png")) {
        assert(widths[0] == 85 && heights[0] == 180);
        assert(black_rows == 2 * expected_pages);
        if (expected_pages == 2) assert(widths[1] == 85 && heights[1] == 360);
    }
    assert(sink_bytes == rows * 11);
    ippDelete(response);
}
int main(void)
{
    char temp[] = "/tmp/pappl-raster-XXXXXX"; assert(mkdtemp(temp));
    char path[256]; snprintf(path, sizeof path, "%s/page.ras", temp);
    port = 22000 + getpid() % 15000;
    pappl_system_t *s = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "raster-test", port,
                        NULL, temp, "-", PAPPL_LOGLEVEL_ERROR, NULL, false); assert(s);
    pappl_pr_driver_t d = {"fixture", "Fixture", NULL, NULL};
    papplSystemSetPrinterDrivers(s, 1, &d, NULL, NULL, driver, NULL);
    listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(!bind(listener, (void *)&address, sizeof address));
    socklen_t len = sizeof address; assert(!getsockname(listener, (void *)&address, &len)); assert(!listen(listener, 1));
    char device[256]; snprintf(device, sizeof device, "socket://127.0.0.1:%d", ntohs(address.sin_port));
    printer = papplPrinterCreate(s, 0, "test", "fixture", "MFG:Test;MDL:Fixture;", device); assert(printer);
    assert(papplSystemAddListeners(s, "localhost"));
    pthread_t server; assert(!pthread_create(&server, NULL, run, s));
    http_t *h = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL); assert(h);
    for (int bits = 1; bits <= 8; bits += 7) {
        fixture(path, bits, CUPS_RASTER_WRITE_PWG, 1, false);
        execute(h, path, "image/pwg-raster", false, true, 180, 1);
        fixture(path, bits, CUPS_RASTER_WRITE_PWG, 2, false);
        execute(h, path, "image/pwg-raster", false, true, 540, 2);
        fixture(path, bits, CUPS_RASTER_WRITE_PWG, 1, true);
        execute(h, path, "image/pwg-raster", false, false, 179, 0);
        fixture(path, bits, CUPS_RASTER_WRITE_PWG, 1, false);
        execute(h, path, "image/pwg-raster", true, false, 0, 0);
        execute(h, path, "image/pwg-raster", 2, false, 0, 0);
        execute(h, path, "image/pwg-raster", 3, true, 180, 1);
        /* A concrete size-name is explicit geometry, and takes precedence
         * over nested dimensions when both representations are present. */
        execute(h, path, "image/pwg-raster", 4, false, 0, 0);
        execute(h, path, "image/pwg-raster", 5, true, 180, 1);
        execute(h, path, "image/pwg-raster", 6, false, 0, 0);
        fail_row = 3;
        execute(h, path, "image/pwg-raster", false, false, 3, 0);
        fail_row = -1;
    }
    for (int bits = 8; bits <= 8; bits += 7) {
        fixture(path, bits, CUPS_RASTER_WRITE_APPLE, 1, false);
        execute(h, path, "image/urf", false, true, 180, 1);
        execute(h, path, "image/urf", 4, false, 0, 0);
        execute(h, path, "image/urf", 5, true, 180, 1);
        execute(h, path, "image/urf", 6, false, 0, 0);
    }
    /* A monochrome driver never receives unconverted RGB raster. */
    fixture(path, 24, CUPS_RASTER_WRITE_PWG, 1, false);
    execute(h, path, "image/pwg-raster", false, false, 0, 0);
    /* Invalid resolution is rejected before start-page or raster output. */
    fixture(path, 8, CUPS_RASTER_WRITE_PWG, 1, false);
    int bad = open(path, O_WRONLY); assert(bad >= 0);
    unsigned zero = 0;
    assert(pwrite(bad, &zero, sizeof zero, 4 + offsetof(cups_page_header2_t, HWResolution)) == sizeof zero);
    close(bad);
    execute(h, path, "image/pwg-raster", false, false, 0, 0);
    /* Unsupported resolution must not reinterpret 75dpi pixels at 180dpi. */
    fixture(path, 8, CUPS_RASTER_WRITE_PWG, 1, false);
    bad = open(path, O_WRONLY); assert(bad >= 0); unsigned resolution[2] = {htonl(75), htonl(75)};
    assert(pwrite(bad, resolution, sizeof resolution, 4 + offsetof(cups_page_header2_t, HWResolution)) == sizeof resolution);
    close(bad); execute(h, path, "image/pwg-raster", false, false, 0, 0);
    /* Image cleanup must receive aborted state and must not repeat end-job. */
    FILE *image = fopen(path, "wb"); assert(image);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png); assert(png && info); assert(!setjmp(png_jmpbuf(png)));
    png_init_io(png, image); png_set_IHDR(png, info, 85, 180, 8, PNG_COLOR_TYPE_GRAY,
            PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info); unsigned char pixel[85]; memset(pixel, 255, sizeof pixel);
    for (int y = 0; y < 180; y++) png_write_row(png, pixel);
    png_write_end(png, info); png_destroy_write_struct(&png, &info); fclose(image);
    fail_row = 3; execute(h, path, "image/png", false, false, 3, 0); fail_row = -1;
    fail_end = true; execute(h, path, "image/png", false, false, 708, 1); fail_end = false;
    /* Every malformed-termination case is part of the required CTest suite. */
    const cups_mode_t modes[] = {CUPS_RASTER_WRITE_PWG, CUPS_RASTER_WRITE_PWG, CUPS_RASTER_WRITE_APPLE};
    const int depths[] = {1, 8, 8};
    for (unsigned i = 0; i < 3; i++) {
        const char *format = modes[i] == CUPS_RASTER_WRITE_APPLE ? "image/urf" : "image/pwg-raster";
        /* The next header can already be buffered by a compressed row read. */
        const unsigned fragments[] = {1, 17};
        for (unsigned f = 0; f < 2; f++) {
            fixture(path, depths[i], modes[i], 1, false);
            int fd = open(path, O_WRONLY | O_APPEND); assert(fd >= 0);
            unsigned char partial[17] = {0};
            assert(write(fd, partial, fragments[f]) == fragments[f]); close(fd);
            execute(h, path, format, false, false, 180, 1);
        }
        /* A complete but invalid next header must also fail, not become EOF. */
        fixture(path, depths[i], modes[i], 1, false);
        int fd = open(path, O_WRONLY | O_APPEND); assert(fd >= 0);
        unsigned char invalid[sizeof(cups_page_header2_t)] = {0};
        unsigned bytes = modes[i] == CUPS_RASTER_WRITE_APPLE ? 32 : sizeof invalid;
        assert(write(fd, invalid, bytes) == bytes); close(fd);
        execute(h, path, format, false, false, 180, 1);
        /* A truncated first header emits no page setup or pixels. */
        fixture(path, depths[i], modes[i], 1, false);
        fd = open(path, O_WRONLY); assert(fd >= 0);
        assert(!ftruncate(fd, (modes[i] == CUPS_RASTER_WRITE_APPLE ? 12 : 4) + 1)); close(fd);
        execute(h, path, format, false, false, 0, 0);
        /* Failure in the second page cannot replay the first completed page. */
        fixture(path, depths[i], modes[i], 2, true);
        execute(h, path, format, false, false, 539, 1);
        fixture(path, depths[i], modes[i], 2, false);
        execute(h, path, format, false, true, 540, 2);
    }
    httpClose(h); close(listener); papplSystemShutdown(s);
    /* Let existing detached clients drain before deleting the fixture system. */
    pthread_join(server, NULL); papplSystemDelete(s);
    /* A failed input callback is not normal EOF, with or without read-ahead. */
    decoder_io_failure(path, CUPS_RASTER_WRITE_PWG);
    decoder_io_failure(path, CUPS_RASTER_WRITE_APPLE);
    decoder_io_failure(path, CUPS_RASTER_WRITE);
    unlink(path); rmdir(temp); return 0;
}
