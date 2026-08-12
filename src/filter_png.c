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

/* Clients are documented to send 180 dpi (README); a PNG with no pHYs chunk is
 * read as such so its pixel height still means a real physical length. */
#define PT_DEFAULT_PPI 180

static void png_error_func(png_structp pp, png_const_charp message)
{
    papplLogJob((pappl_job_t *)png_get_error_ptr(pp), PAPPL_LOGLEVEL_ERROR, "PNG: %s", message);
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
        o->orientation_requested = width > height ? IPP_ORIENT_LANDSCAPE : IPP_ORIENT_PORTRAIT;

    return o->orientation_requested == IPP_ORIENT_LANDSCAPE ||
           o->orientation_requested == IPP_ORIENT_REVERSE_LANDSCAPE;
}

bool pt_filter_png(pappl_job_t *job, pappl_device_t *device, void *data)
{
    const char *filename;
    FILE *fp;
    pappl_pr_options_t *options = NULL;
    png_structp pp = NULL;
    png_infop info = NULL;
    png_bytep *rows = NULL;
    png_color_16 bg;
    int i, color_type, width, height, depth, xdpi, ydpi, ppi;
    int along_tape_px, length_centimm;
    bool landscape;
    int max_width = 0, max_height = 0;
    size_t max_size;
    unsigned char *pixels = NULL;
    bool ret = false;

    (void)data;

    max_size = papplSystemGetMaxImageSize(papplPrinterGetSystem(papplJobGetPrinter(job)),
                                          &max_width, &max_height);

    filename = papplJobGetFilename(job);
    if ((fp = fopen(filename, "rb")) == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to open PNG file '%s': %s",
                    filename, strerror(errno));
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
    options = papplJobCreatePrintOptions(job, 1, depth == 3);
    landscape = pt_pin_orientation(options, width, height);
    along_tape_px = landscape ? width : height;

    if ((length_centimm = pt_label_length(along_tape_px, ppi)) < 0) {
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

    pt_set_page_length(options, length_centimm);
    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "%s image, %d px along the tape at %d dpi -> media length %d (1/100 mm), "
                "header.cupsHeight=%u PWG_ImageBox=[%u %u %u %u]",
                landscape ? "landscape" : "portrait", along_tape_px, ppi, length_centimm,
                options->header.cupsHeight,
                options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxLeft],
                options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxTop],
                options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxRight],
                options->header.cupsInteger[CUPS_RASTER_PWG_ImageBoxBottom]);

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
    bg.red = bg.green = bg.blue = 65535;
    png_set_background(pp, &bg, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1);

    if ((pixels = calloc(1, (size_t)width * (size_t)height * (size_t)depth)) == NULL ||
        (rows = calloc((size_t)height, sizeof(png_bytep))) == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to allocate memory for PNG image: %s",
                    strerror(errno));
        papplJobSetReasons(job, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR, PAPPL_JREASON_NONE);
        goto finish_png;
    }

    for (i = 0; i < height; i++)
        rows[i] = pixels + (size_t)i * (size_t)width * (size_t)depth;

    for (i = png_set_interlace_handling(pp); i > 0; i--)
        png_read_rows(pp, rows, NULL, (png_uint_32)height);

    /* Drives the whole raster lifecycle through the driver's callbacks (width
     * guard, cutter, offline handling all still apply). */
    ret = papplJobFilterImage(job, device, options, pixels, width, height, depth, ppi, false);

finish_png:

    png_destroy_read_struct(&pp, &info, NULL);
    fclose(fp);
    papplJobDeletePrintOptions(options);
    free(pixels);
    free(rows);

    return ret;
}
