# 6. Own the `image/png` filter to derive label length from the image

**Status:** Accepted

## Context

Clients send a PNG and no media length. PAPPL renders it onto a page whose size comes from
`media-size` and, with `print-scaling=none`, centres it. With a fixed 100 mm default media length, a
short label printed with large blank runs before and after it.

PAPPL does not auto-size pages for continuous media: `cupsHeight` comes straight from the media, and
the image filter only places and clips within it. By the time the driver's raster callbacks run, the
page is fixed and the PNG's own dimensions are gone.

## Decision

Register our own `image/png` filter and build the page from the submitted image.

PAPPL's `papplSystemAddMIMEFilter` compares only source and destination MIME types and ignores a
duplicate registration, so `image/png -> image/pwg-raster` cannot be overridden. Instead the driver
declares a private `driver_data.format` and registers `image/png -> <private type>`, which job
processing tries first. `validate_driver` requires a `printfile_cb` alongside a non-null `format`, so
one exists and simply faults the job.

The filter reads the PNG, derives the label length from the image and its resolution (falling back to
180 dpi), rewrites every length-derived header field together, then calls `papplJobFilterImage`,
which continues to drive the existing raster callbacks.

## Consequences

- A label is exactly as long as the submitted image, including deliberate white padding.
- Over-length faults rather than clamping: `papplJobFilterImage` clips centred, so a clamp would
  silently eat both ends of the design.
- **Orientation must be pinned from the image.** PAPPL auto-rotates by comparing the image aspect to
  the page aspect, which becomes circular once the page length is derived from the image. Real
  clients send landscape labels; without pinning they print rotated and clipped. Landscape images
  pin `reverse-landscape` (90° CW) so that the start of the design emerges first and prints upright (#44).
- The private MIME type appears in `document-format-supported`.
- Owning this filter later provided the only place able to fix media staleness (ADR-0007).
