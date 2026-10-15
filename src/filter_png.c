#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <pappl/pappl.h>
#include "filter_png.h"
#include "driver.h"
#include "tables.h"

/* Clients are documented to send 180 dpi (README); a PNG with no pHYs chunk is
 * read as such so its pixel height still means a real physical length. */
#define PT_DEFAULT_PPI 180

static void png_error_func(png_structp pp, png_const_charp message)
{
    pappl_job_t *job = png_get_error_ptr(pp);
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "PNG: %s", message);
    papplJobSetMessage(job, "PNG decode failed: %s", message);
}

static void png_warning_func(png_structp pp, png_const_charp message)
{
    papplLogJob((pappl_job_t *)png_get_error_ptr(pp), PAPPL_LOGLEVEL_WARN, "PNG: %s", message);
}

/* Move every length-derived header field to `length_centimm` (1/100 mm).
 * They must move together or the header contradicts itself. Width-derived
 * fields (cupsWidth, cupsBytesPerLine, ImageBoxLeft/Right) are NOT touched:
 * the tape and the #39 margins own the cross-tape geometry.
 * Formulas mirror cupsRasterInitPWGHeader + PAPPL's ImageBox fixups. */
static void pt_set_page_length(pappl_pr_options_t *o, int length_centimm)
{
    unsigned ydpi = o->header.HWResolution[1] ? o->header.HWResolution[1] : PT_DEFAULT_PPI;

    o->media.size_length = length_centimm;
    o->header.cupsHeight = (unsigned)((int64_t)length_centimm * ydpi / 2540);
    o->header.cupsPageSize[1] = 72.0f * (float)length_centimm / 2540.0f;
    o->header.PageSize[1] = (unsigned)(72 * length_centimm / 2540);
    o->header.ImagingBoundingBox[3] =
        (unsigned)(72 * (length_centimm - o->media.top_margin) / 2540);
    o->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxTop] =
        (unsigned)o->media.top_margin * ydpi / 2540;
    o->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxBottom] =
        o->header.cupsHeight - (unsigned)o->media.bottom_margin * ydpi / 2540 - 1;
}

/* Physical length (1/100 mm) of `along_tape_px` at `ppi`, or -1 when it exceeds
 * the roll maximum. CEIL so the header's truncating px conversion round-trips to
 * exactly along_tape_px; 64-bit because px * 2540 overflows int at ~845k px.
 * Under-length is clamped UP to the roll minimum, which only adds centred blank
 * tape; over-length is NOT clamped down, because papplJobFilterImage clips
 * CENTRED and would silently eat both ends of the design (#19-style fault). */
static int pt_label_length(int along_tape_px, int ppi)
{
    int64_t centimm = ((int64_t)along_tape_px * 2540 + ppi - 1) / ppi;

    if (centimm > PT_ROLL_MAX_CENTIMM)
        return -1;
    if (centimm < PT_ROLL_MIN_CENTIMM)
        centimm = PT_ROLL_MIN_CENTIMM;
    return (int)centimm;
}

/* Pin the orientation and report which image axis ends up running along the tape.
 * papplJobFilterImage auto-rotates by comparing the image aspect with the PAGE
 * aspect, which is circular once the page length follows the image: a wide image
 * that rotated to landscape on the old 100 mm page would stay portrait on a page
 * sized to it, i.e. come out turned 90 degrees and clipped. So decide from the
 * image alone - the same answer PAPPL gave while cupsHeight was always the larger
 * side - and pin it so papplJobFilterImage cannot flip it back. An explicit
 * client "orientation-requested" is honoured untouched. */
static bool pt_pin_orientation(pappl_pr_options_t *o, int width, int height)
{
    if (o->orientation_requested == IPP_ORIENT_NONE)
        o->orientation_requested = width > height ? IPP_ORIENT_REVERSE_LANDSCAPE : IPP_ORIENT_PORTRAIT;

    return o->orientation_requested == IPP_ORIENT_LANDSCAPE ||
           o->orientation_requested == IPP_ORIENT_REVERSE_LANDSCAPE;
}

/* Submitted dimensions or a resolved size name suppress the PNG convenience. */
static bool pt_explicit_length(pappl_job_t *job)
{
    ipp_attribute_t *attr = papplJobGetAttribute(job, "media-col");
    if (attr && ippGetValueTag(attr) == IPP_TAG_BEGIN_COLLECTION) {
        ipp_t *col = ippGetCollection(attr, 0);
        ipp_attribute_t *size_name = ippFindAttribute(col, "media-size-name", IPP_TAG_ZERO);
        const char *name = size_name ? ippGetString(size_name, 0, NULL) : NULL;
        pwg_media_t *media = name ? pwgMediaForPWG(name) : NULL;
        /* PAPPL prefers a collection's size name over its dimensions. */
        if (media && media->length > 0)
            return true;
        ipp_attribute_t *size = ippFindAttribute(col, "media-size", IPP_TAG_BEGIN_COLLECTION);
        if (size && ippFindAttribute(ippGetCollection(size, 0), "y-dimension", IPP_TAG_INTEGER))
            return true;
    }
    attr = papplJobGetAttribute(job, "media");
    const char *name = attr ? ippGetString(attr, 0, NULL) : NULL;
    pwg_media_t *media = name ? pwgMediaForPWG(name) : NULL;
    return media && media->length > 0;
}

/* Preflight the oriented physical content before PAPPL's centered placement can
 * clip it. A full tape canvas may include blank physical margins; compact only
 * those verified margins, keeping the same orientation, DPI, and content. */
static bool pt_preflight_png(pappl_job_t *job, pappl_pr_options_t *o,
                             unsigned char *pixels, int *width, int *height,
                             int depth, int ppi, bool landscape)
{
    if (o->print_scaling != PAPPL_SCALING_NONE)
        return true; /* Explicit standard scaling remains PAPPL's responsibility. */

    pt_geometry g;
    if (!pt_tape_geometry((o->media.size_width + 50) / 100, &g))
        return false;
    int cross = landscape ? *height : *width;
    int along = landscape ? *width : *height;
    int cross_px = (int)((int64_t)cross * PT_DEFAULT_PPI / ppi);
    int along_px = (int)((int64_t)along * PT_DEFAULT_PPI / ppi);
    int available = (int)o->header.cupsHeight -
        (o->media.top_margin + o->media.bottom_margin) * PT_DEFAULT_PPI / 2540;
    int printable = (int)o->header.cupsWidth -
        (o->media.left_margin + o->media.right_margin) * PT_DEFAULT_PPI / 2540;
    if (along_px < 1 || cross_px < 1 || along_px > available) {
        papplJobSetMessage(job, "PNG content needs %d along-tape dots; page allows %d", along_px, available);
        goto reject;
    }
    if (cross_px <= g.imageable_px && cross_px <= printable)
        return true;
    if (cross_px != g.nominal_px) {
        papplJobSetMessage(job, "PNG content is %d dots across tape; printable width is %d", cross_px,
                           printable < g.imageable_px ? printable : g.imageable_px);
        goto reject;
    }

    bool reverse = o->orientation_requested == IPP_ORIENT_REVERSE_PORTRAIT ||
                   o->orientation_requested == IPP_ORIENT_REVERSE_LANDSCAPE;
    int first_dot = reverse ? g.nominal_px - g.right_px : g.left_px;
    int last_dot = first_dot + g.imageable_px;
    int first = (int)(((int64_t)first_dot * ppi + PT_DEFAULT_PPI - 1) / PT_DEFAULT_PPI);
    int last = (int)((int64_t)last_dot * ppi / PT_DEFAULT_PPI);
    if (first >= last || (int64_t)(last - first) * PT_DEFAULT_PPI / ppi > printable) {
        papplJobSetMessage(job, "PNG content does not fit the selected media margins");
        goto reject;
    }
    for (int y = 0; y < *height; y++) {
        for (int x = 0; x < *width; x++) {
            int pos = landscape ? y : x;
            if (pos >= first && pos < last)
                continue;
            for (int c = 0; c < depth; c++) {
                if (pixels[((size_t)y * *width + x) * depth + c] != 255) {
                    papplJobSetMessage(job, "PNG has nonblank content outside the physical printable window");
                    goto reject;
                }
            }
        }
    }
    if (landscape) {
        memmove(pixels, pixels + (size_t)first * *width * depth,
                (size_t)(last - first) * *width * depth);
        *height = last - first;
    } else {
        for (int y = 0; y < *height; y++)
            memmove(pixels + (size_t)y * (last - first) * depth,
                    pixels + ((size_t)y * *width + first) * depth,
                    (size_t)(last - first) * depth);
        *width = last - first;
    }
    return true;

reject:
    papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "%s", papplJobGetMessage(job));
    return false;
}

bool pt_filter_png(pappl_job_t *job, pappl_device_t *device, void *data)
{
    const char *filename;
    FILE *fp;

    png_structp pp = NULL;
    png_infop info = NULL;

    png_color_16 bg;
    int i, color_type, width, height, depth, xdpi, ydpi, ppi;
    int along_tape_px, length_centimm;
    bool landscape;
    int max_width = 0, max_height = 0;
    size_t max_size;

    volatile bool ret = false;

    (void)data;

    max_size = papplSystemGetMaxImageSize(papplPrinterGetSystem(papplJobGetPrinter(job)),
                                          &max_width, &max_height);

    filename = papplJobGetFilename(job);
    if (!filename) {
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_FORMAT_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(job, "PNG job has no document file");
        return false;
    }
    if ((fp = fopen(filename, "rb")) == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to open PNG file '%s': %s",
                    filename, strerror(errno));
        return false;
    }

    /* Mutable cleanup ownership lives outside the setjmp stack frame, so a
     * libpng longjmp during rows/IEND leaves every allocation well defined. */
    struct {
        pappl_pr_options_t *options;
        png_bytep *rows;
        unsigned char *pixels;
    } *storage = calloc(1, sizeof *storage);
    if (!storage) {
        fclose(fp);
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        return false;
    }

    if ((pp = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)job,
                                     png_error_func, png_warning_func)) == NULL ||
        (info = png_create_info_struct(pp)) == NULL) {
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_FORMAT_ERROR, PAPPL_JREASON_NONE);
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to allocate memory for PNG file '%s': %s",
                    filename, strerror(errno));
        goto finish_png;
    }

    if (setjmp(png_jmpbuf(pp))) {
        /* PNG loading failed; the error was logged by png_error_func. */
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_FORMAT_ERROR, PAPPL_JREASON_NONE);
        goto finish_png;
    }

    png_init_io(pp, fp);

#if defined(PNG_SKIP_sRGB_CHECK_PROFILE) && defined(PNG_SET_OPTION_SUPPORTED)
    /* Don't throw errors with "invalid" sRGB profiles produced by Adobe apps. */
    png_set_option(pp, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
#endif

    png_read_info(pp, info);

    width = (int)png_get_image_width(pp, info);
    height = (int)png_get_image_height(pp, info);
    color_type = png_get_color_type(pp, info);
    depth = (color_type & PNG_COLOR_MASK_COLOR) ? 3 : 1;

    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "PNG image dimensions are %dx%dx%d", width, height, depth);

    if (width < 1 || width > max_width || height < 1 || height > max_height ||
        (size_t)width * (size_t)height * (size_t)depth > max_size) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "PNG image is too large to print.");
        papplJobSetMessage(job, "PNG is too large to print (%dx%d)", width, height);
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        goto finish_png;
    }

    xdpi = (int)png_get_x_pixels_per_inch(pp, info);
    ydpi = (int)png_get_y_pixels_per_inch(pp, info);

    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "PNG image resolution is %dx%ddpi", xdpi, ydpi);

    /* papplJobFilterImage scales both axes by one ppi, so a non-square PNG would
     * change the cross-tape scale as well; fault it, as PAPPL's own filter does. */
    if (xdpi != ydpi) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "PNG image has non-square aspect ratio - not currently supported.");
        papplJobSetMessage(job, "PNG has non-square resolution %dx%d dpi", xdpi, ydpi);
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        goto finish_png;
    }
    ppi = ydpi > 0 ? ydpi : PT_DEFAULT_PPI;

    /* #43: a job queued while the printer was off would be built from the stale
     * 12 mm default and then faulted by the strict width guard against the live
     * tape. This is the last point where the media can still be corrected: we hold
     * the device and the page geometry does not exist yet. */
    if (!pt_refresh_ready_media(job, device))
        goto finish_png;   /* the job is already faulted */

    /* #40: the label is as long as the image is, not as long as the media
     * default. Do this before any decode work so an over-length job faults
     * cheaply and before a single byte reaches the device. */
    storage->options = papplJobCreatePrintOptions(job, 1, depth == 3);
    if (!storage->options)
        goto finish_png;
    landscape = pt_pin_orientation(storage->options, width, height);
    along_tape_px = landscape ? width : height;

    bool explicit_length = pt_explicit_length(job);
    length_centimm = explicit_length ? storage->options->media.size_length : pt_label_length(along_tape_px, ppi);
    if (length_centimm < 0) {
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        papplJobSetMessage(job, "label too long: %d px at %d dpi needs %d.%02d mm (max %d mm)",
                           along_tape_px, ppi, along_tape_px * 2540 / ppi / 100,
                           along_tape_px * 2540 / ppi % 100, PT_ROLL_MAX_CENTIMM / 100);
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "label too long: %d px at %d dpi needs %d.%02d mm (max %d mm)",
                    along_tape_px, ppi, along_tape_px * 2540 / ppi / 100,
                    along_tape_px * 2540 / ppi % 100, PT_ROLL_MAX_CENTIMM / 100);
        goto finish_png;
    }

    if (!explicit_length)
        pt_set_page_length(storage->options, length_centimm);
    /* Preflight and PAPPL's placement read margins before the driver can wait. */
    pt_resolve_page_margins(job, storage->options);
    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "%s image, %d px along the tape at %d dpi -> media length %d (1/100 mm), "
                "header.cupsHeight=%u PWG_ImageBox=[%u %u %u %u]",
                landscape ? "landscape" : "portrait", along_tape_px, ppi, length_centimm,
                storage->options->header.cupsHeight,
                storage->options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxLeft],
                storage->options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxTop],
                storage->options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxRight],
                storage->options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxBottom]);

    /* Set decoding options... */
    if (png_get_valid(pp, info, PNG_INFO_tRNS)) {
        /* Map transparency to alpha */
        png_set_tRNS_to_alpha(pp);
        color_type |= PNG_COLOR_MASK_ALPHA;
    }

#ifdef PNG_TRANSFORM_SCALE_16
    if (png_get_bit_depth(pp, info) > 8) {
        /* Scale 16-bit values to 8-bit gamma-corrected ones */
        png_set_scale_16(pp);
        papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Scaling 16-bit PNG data to 8-bits.");
    }
#else
    if (png_get_bit_depth(pp, info) > 8) {
        /* Strip the bottom bits of 16-bit values */
        png_set_strip_16(pp);
        papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Stripping 16-bit PNG data to 8-bits.");
    }
#endif

    if (png_get_bit_depth(pp, info) < 8) {
        /* Expand 1, 2, and 4-bit values to 8 bits */
        if (depth == 1)
            png_set_expand_gray_1_2_4_to_8(pp);
        else
            png_set_packing(pp);
    }
    if (color_type & PNG_COLOR_MASK_PALETTE)
        png_set_palette_to_rgb(pp);

    /* Remove alpha by compositing over white... */
    memset(&bg, 0, sizeof(bg));
    bg.gray = bg.red = bg.green = bg.blue = 65535;
    png_set_background(pp, &bg, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1);

    if ((storage->pixels = calloc(1, (size_t)width * (size_t)height * (size_t)depth)) == NULL ||
        (storage->rows = calloc((size_t)height, sizeof(png_bytep))) == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to allocate memory for PNG image: %s",
                    strerror(errno));
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        goto finish_png;
    }

    for (i = 0; i < height; i++)
        storage->rows[i] = storage->pixels + (size_t)i * (size_t)width * (size_t)depth;

    for (i = png_set_interlace_handling(pp); i > 0; i--)
        png_read_rows(pp, storage->rows, NULL, (png_uint_32)height);

    png_read_end(pp, info); /* Validate the complete PNG before output. */

    if (!pt_preflight_png(job, storage->options, storage->pixels, &width, &height, depth, ppi, landscape))
        goto finish_png;

    /* Drives the whole raster lifecycle through the driver's callbacks (width
     * guard, cutter, offline handling all still apply). */
    ret = papplJobFilterImage(job, device, storage->options, storage->pixels, width, height, depth, ppi, false);

finish_png:

    png_destroy_read_struct(&pp, &info, NULL);
    fclose(fp);
    papplJobDeletePrintOptions(storage->options);
    free(storage->pixels);
    free(storage->rows);
    free(storage);

    return ret;
}

/* Raw PNG and the registered PNG filter share exactly one decoding/copy path. */
bool pt_png_printfile(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
    (void)options;
    return pt_filter_png(job, device, NULL);
}
