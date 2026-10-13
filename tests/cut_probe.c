/* Test-only PT-2730 finalization probe. Never used by the production app. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cutter.h"
#include "protocol.h"
#include "raster.h"
#include "status.h"
#include "tables.h"
#ifdef PT_PROBE_TRANSPORT
#include "device_usb.h"
#endif

typedef struct {
    FILE *capture;
    size_t bytes;
#ifdef PT_PROBE_TRANSPORT
    pappl_device_t *device;
    const char *device_uri;
#endif
} output;

static void usage(const char *name)
{
    fprintf(stderr,
            "Usage: %s --capture FILE --sequence batch|baseline|drain|tail-eject|tail-chain-off "
            "--labels 1|2|3 --mode chain|each|none|end --precut 0|1 --tape-mm N "
            "[--device ptouch://Brother/PT-2730|socket://HOST:PORT]\n"
            "Batch requires --precut 1; --continue 1 omits reset for a known healthy open run.\n"
            "Batch also accepts --batches chain:3,chain:2,each:1 instead of --labels/--mode.\n"
            "Add --wait-ready 1 to read USB completion status between pages.\n"
            "Omitting --device only writes a capture; it never contacts a printer.\n"
            "Use --status-only to query the PT-2730 without initialization or printing.\n",
            name);
}

static int number(const char *text)
{
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    return errno || end == text || *end || n < 0 || n > 255 ? -1 : (int)n;
}

#ifdef PT_PROBE_TRANSPORT
static bool device_error;
static void report_error(const char *message, void *data)
{
    (void)data;
    device_error = true;
    fprintf(stderr, "probe: device error: %s\n", message);
}
#endif

#ifdef PT_PROBE_TRANSPORT
static int64_t milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
#endif

/* Diagnostic only: do not send another page while the printer is busy.
 * Read unsolicited packets, never issue a status request during printing. */
static bool read_ready(output *out, bool wait, int page_index)
{
#ifdef PT_PROBE_TRANSPORT
    if (!out->device) return true;
    uint8_t packet[32];
    size_t used = 0;
    int64_t started = milliseconds();
    if (started < 0) return false;
    /* Leave room for the final bounded 300 ms read within twenty seconds. */
    while (milliseconds() >= started && milliseconds() - started < 19700) {
        ssize_t n = pt_usb_read_status(out->device, out->device_uri,
                                       packet + used, sizeof packet - used);
        if (n < 0) {
            fprintf(stderr, "probe: status read failed after page %d\n", page_index + 1);
            return false;
        }
        if (!n && !wait && !used) return true;
        used += (size_t)n;
        if (used != sizeof packet) continue;
        pt_status status;
        if (pt_parse_status(packet, &status)) return false;
        fprintf(stderr, "probe: %s page=%d error=0x%04x type=0x%02x phase=0x%02x raw=",
                wait ? "completion" : "initial-status", page_index + 1,
                status.error, packet[18], packet[19]);
        for (size_t i = 0; i < sizeof packet; i++) fprintf(stderr, "%02x", packet[i]);
        fputc('\n', stderr);
        if (status.error) return false;
        /* Completion (type 01) precedes receiving-ready on this PT-2730.
         * Consuming only completion leaves the old ready event for page two. */
        if (wait && packet[18] == 6 && packet[19] == 0 &&
            packet[20] == 0 && packet[21] == 0) return true;
        used = 0;
    }
    fprintf(stderr, "probe: no completion status after page %d; stopping\n", page_index + 1);
    return false;
#else
    (void)out; (void)wait; (void)page_index;
    return true;
#endif
}

static bool emit(output *out, const uint8_t *bytes, size_t n)
{
    if (fwrite(bytes, 1, n, out->capture) != n) {
        perror("probe: capture write");
        return false;
    }
#ifdef PT_PROBE_TRANSPORT
    if (out->device) {
        pappl_devmetrics_t before, after;
        papplDeviceGetMetrics(out->device, &before);
        ssize_t accepted = papplDeviceWrite(out->device, bytes, n);
        papplDeviceFlush(out->device);
        papplDeviceGetMetrics(out->device, &after);
        if (device_error || accepted != (ssize_t)n ||
            after.write_bytes - before.write_bytes != n) {
            fprintf(stderr, "probe: transfer failed after %zu bytes; stopping\n", out->bytes);
            return false;
        }
    }
#endif
    out->bytes += n;
    return true;
}

/* Edge bars and page-number stripes: 180/216/252 rows = 25.4/30.48/35.56 mm.
 * Use the same packing and command builder as the driver, with no image decoder. */
static bool page(output *out, int index, int width)
{
    int rows = 180 + index * 36;
    for (int y = 0; y < rows; y++) {
        uint8_t pixels[16] = {0}, packed[16], command[19];
        for (int x = 0; x < width; x++) {
            bool black = y < 2 || y >= rows - 2 || x < 2 || x >= width - 2 ||
                         (x >= 6 && x < 6 + (index + 1) * 4 && (x - 6) % 4 < 2);
            if (black)
                pixels[x / 8] |= (uint8_t)(0x80u >> (x % 8));
        }
        pt_pack_line(packed, pixels, width);
        if (!emit(out, command, pt_cmd_sendraster(command, packed, sizeof packed)))
            return false;
    }
    return true;
}

typedef struct { pt_cut_mode mode; int labels; } batch;

static int parse_mode(const char *value)
{
    if (!strcmp(value, "chain")) return PT_CUT_CHAIN;
    if (!strcmp(value, "each")) return PT_CUT_EACH;
    if (!strcmp(value, "none")) return PT_CUT_NONE;
    if (!strcmp(value, "end")) return PT_CUT_END;
    return -1;
}

/* At most three batches and six labels: enough for both continuation gates. */
static int parse_batches(const char *value, batch *batches)
{
    const char *p = value;
    int count = 0, total = 0;
    while (*p && count < 3) {
        const char *colon = strchr(p, ':');
        char mode[6];
        if (!colon || colon == p || colon - p >= (int)sizeof mode) return -1;
        memcpy(mode, p, (size_t)(colon - p));
        mode[colon - p] = 0;
        int parsed = parse_mode(mode);
        if (parsed < 0 || colon[1] < '1' || colon[1] > '3' ||
            (colon[2] && colon[2] != ',')) return -1;
        batches[count++] = (batch){parsed, colon[1] - '0'};
        total += colon[1] - '0';
        if (!colon[2]) return total <= 6 ? count : -1;
        p = colon + 3;
    }
    return -1;
}

/* Model the proposed callbacks: rendpage leaves one pending print command;
 * the next valid label releases it with FF, successful rendjob selects FF or
 * late chain-off/EJECT. No operation array or count-dependent page setup. */
static bool run_batch(output *out, int labels, int tape, pt_cut_mode mode,
                      bool continuing, bool wait_ready)
{
    uint8_t bytes[8];
    bool pending = false;
    pt_cut_policy policy = pt_cut_policy_for(mode);
    if ((!continuing && !emit(out, bytes, pt_cmd_init(bytes))) ||
        !emit(out, bytes, pt_cmd_rasterstart(bytes, FLAG_NONE))) return false;
    for (int i = 0; i < labels; i++) {
        if (pending && (!emit(out, bytes, pt_cmd_ff(bytes)) ||
                        (wait_ready && !read_ready(out, true, i - 1)))) return false;
        pending = false;
        if (!emit(out, bytes, pt_cmd_printinfo(bytes, (uint8_t)tape)) ||
            !emit(out, bytes, pt_cmd_setmode(bytes, pt_cut_autocut(policy, i == 0) ? 0x40 : 0)) ||
            !emit(out, bytes, pt_cmd_setadvanced(bytes, 0)) ||
            !page(out, i, pt_tape_px(tape))) return false;
        pending = true;
    }
    if (pending) {
        if (policy.cut_at_end && !emit(out, bytes, pt_cmd_setadvanced(bytes, 8))) return false;
        if (!emit(out, bytes, policy.cut_at_end ? pt_cmd_eject(bytes) : pt_cmd_ff(bytes)) ||
            (wait_ready && !read_ready(out, true, labels - 1))) return false;
    }
    return true;
}

/* Preserve the historical diagnostic bytes, including optional precut and
 * the empty drain EJECT. These commands are test-local, not production policy. */
static bool run(output *out, int labels, int tape, pt_cut_mode mode, int precut, int sequence,
                bool wait_ready)
{
    uint8_t bytes[8];
    if ((wait_ready && !read_ready(out, false, -1)) ||
        !emit(out, bytes, pt_cmd_init(bytes)) ||
        !emit(out, bytes, pt_cmd_rasterstart(bytes, FLAG_NONE))) return false;

    for (int i = 0; i < labels; i++) {
        bool autocut = precut || (mode == PT_CUT_EACH && i > 0);
        bool terminal = i == labels - 1 && sequence != 1 && sequence != 2;
        if ((autocut && !emit(out, bytes, pt_cmd_printinfo(bytes, (uint8_t)tape))) ||
            !emit(out, bytes, pt_cmd_setmode(bytes, autocut ? 0x40 : 0)) ||
            !emit(out, bytes, pt_cmd_setadvanced(bytes, terminal && sequence != 3 ? 8 : 0)) ||
            !page(out, i, pt_tape_px(tape))) return false;
        if (terminal && sequence == 3 &&
            !emit(out, bytes, pt_cmd_setadvanced(bytes, 8))) return false;
        /* tail-eject replaces the final FF instead of appending an empty EJECT. */
        bool eject = terminal || (sequence == 2 && i == labels - 1);
        if (!emit(out, bytes, eject ? pt_cmd_eject(bytes) : pt_cmd_ff(bytes)) ||
            (wait_ready && !read_ready(out, true, i))) return false;
    }
    return sequence != 1 || emit(out, bytes, pt_cmd_eject(bytes));
}

#ifdef PT_PROBE_TRANSPORT
static int status_only(void)
{
    output out = {0};
    uint8_t command[3], response[32];
    pt_usb_register();
    out.capture = tmpfile();
    if (!out.capture)
        return 1;
    out.device = papplDeviceOpen("ptouch://Brother/PT-2730", "cut-probe-status", report_error, NULL);
    out.device_uri = "ptouch://Brother/PT-2730";
    bool ok = out.device && emit(&out, command, pt_cmd_status_request(command));
    size_t received = 0;
    for (int i = 0; ok && i < 20 && received < sizeof response; i++) {
        ssize_t n = pt_usb_read_status(out.device, out.device_uri,
                                       response + received, sizeof response - received);
        if (n < 0) ok = false;
        else received += (size_t)n;
    }
    pt_status status;
    ok = ok && received == sizeof response && pt_parse_status(response, &status) == 0;
    if (ok) {
        fprintf(stderr, "probe: status error=0x%04x tape=%dmm type=0x%02x phase=0x%02x raw=",
                status.error, status.tape_mm, response[18], response[19]);
        for (size_t i = 0; i < sizeof response; i++) fprintf(stderr, "%02x", response[i]);
        fputc('\n', stderr);
    } else fprintf(stderr, "probe: status query failed (%zu bytes)\n", received);
    if (out.device) papplDeviceClose(out.device);
    fclose(out.capture);
    return ok && status.error == 0 ? 0 : 1;
}
#endif

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--status-only")) {
#ifdef PT_PROBE_TRANSPORT
        return status_only();
#else
        fprintf(stderr, "probe: status query requires a PAPPL/libusb build\n");
        return 2;
#endif
    }
    const char *capture = NULL, *uri = NULL;
    int labels = -1, tape = -1, precut = -1, wait_ready = 0, continuing = 0;
    int mode = -1;
    const char *batch_list = NULL;
    int drain = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (i + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        const char *key = argv[i], *value = argv[++i];
        if (!strcmp(key, "--capture")) capture = value;
        else if (!strcmp(key, "--device")) uri = value;
        else if (!strcmp(key, "--labels")) labels = number(value);
        else if (!strcmp(key, "--tape-mm")) tape = number(value);
        else if (!strcmp(key, "--precut")) precut = number(value);
        else if (!strcmp(key, "--wait-ready")) wait_ready = number(value);
        else if (!strcmp(key, "--continue")) continuing = number(value);
        else if (!strcmp(key, "--batches")) batch_list = value;
        else if (!strcmp(key, "--sequence") && !strcmp(value, "baseline")) drain = 0;
        else if (!strcmp(key, "--sequence") && !strcmp(value, "drain")) drain = 1;
        else if (!strcmp(key, "--sequence") && !strcmp(value, "tail-eject")) drain = 2;
        else if (!strcmp(key, "--sequence") && !strcmp(value, "tail-chain-off")) drain = 3;
        else if (!strcmp(key, "--sequence") && !strcmp(value, "batch")) drain = 4;
        else if (!strcmp(key, "--mode")) mode = parse_mode(value);
        else {
            usage(argv[0]);
            return 2;
        }
    }
    batch batches[3];
    int batch_count = batch_list ? parse_batches(batch_list, batches) : 1;
    if (!batch_list) batches[0] = (batch){mode, labels};
    if (!capture || pt_tape_px(tape) <= 0 ||
        (precut != 0 && precut != 1) || (wait_ready != 0 && wait_ready != 1) ||
        (continuing != 0 && continuing != 1) ||
        (wait_ready && uri && strcmp(uri, "ptouch://Brother/PT-2730")) ||
        drain < 0 || batch_count < 1 ||
        (batch_list && (drain != 4 || labels != -1 || mode != -1)) ||
        (!batch_list && (mode < 0 || labels < 1 || labels > 3)) ||
        (drain == 4 && precut != 1) ||
        (drain != 4 && (batch_list || continuing ||
                       (mode != PT_CUT_EACH && mode != PT_CUT_END) || labels == 2)) ||
        (drain == 4 && uri && !strcmp(uri, "ptouch://Brother/PT-2730") && !wait_ready) ||
        (uri && strcmp(uri, "ptouch://Brother/PT-2730") && strncmp(uri, "socket://", 9))) {
        usage(argv[0]);
        return 2;
    }
#ifndef PT_PROBE_TRANSPORT
    if (uri) {
        fprintf(stderr, "probe: live transport requires a PAPPL/libusb build\n");
        return 2;
    }
#endif
    output out = {0};
    if (!(out.capture = fopen(capture, "wb"))) {
        perror("probe: open capture");
        return 1;
    }
#ifdef PT_PROBE_TRANSPORT
    if (uri) {
        out.device_uri = uri;
        pt_usb_register();
        out.device = papplDeviceOpen(uri, "cut-probe", report_error, NULL);
        if (!out.device) {
            fprintf(stderr, "probe: unable to open %s\n", uri);
            fclose(out.capture);
            return 1;
        }
    }
#endif
    bool ok;
    if (drain == 4) {
        ok = !wait_ready || read_ready(&out, false, -1);
        for (int i = 0; ok && i < batch_count; i++) {
            fprintf(stderr, "probe: batch=%d labels=%d continuing=%d\n",
                    i + 1, batches[i].labels, continuing);
            ok = run_batch(&out, batches[i].labels, tape, batches[i].mode,
                           continuing, wait_ready);
            continuing = !pt_cut_policy_for(batches[i].mode).cut_at_end;
        }
    } else {
        ok = run(&out, labels, tape, (pt_cut_mode)mode, precut, drain, wait_ready);
    }
#ifdef PT_PROBE_TRANSPORT
    if (out.device)
        papplDeviceClose(out.device);
#endif
    if (fclose(out.capture)) {
        perror("probe: close capture");
        ok = false;
    }
    fprintf(stderr, "probe: %s; %zu bytes %s; %s\n", ok ? "success" : "failed",
            out.bytes, uri ? "transferred" : "captured only", capture);
    return ok ? 0 : 1;
}
