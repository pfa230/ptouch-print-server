# 6. Standard PNG route with optional image-derived length

**Status:** Accepted

## Context

PAPPL places PNGs on selected/default media. Without a concrete page length,
continuous tape labels formerly received the fixed 100 mm default. The first
owned filter corrected that case but overwrote explicit lengths and exposed a
private raster MIME that could never be printed directly.

## Decision

Keep libpng decoding and PAPPL's existing image renderer. Set the driver's format
to `image/png`, register `image/png -> image/png` for the owned filter, and provide
a real PNG `printfile_cb` using the same path. PAPPL selects the matching driver
filter before its built-in PNG-to-PWG filter, and deduplicates the public PNG MIME.
No private destination type or always-failing raw callback is needed.

Build options after loaded-media confirmation. Preserve submitted named media or
`media-col.media-size.y-dimension`. Only when concrete length is omitted, derive
length from the oriented image and its square DPI metadata, falling back to 180
dpi. Source-only/margin-only collections do not constitute an explicit size.
Update all length-derived header fields together; use the 5 to 300 mm limits.

Pin automatic orientation from the source aspect before deriving page length:
tall/square images use portrait and wide images use reverse-landscape (90 degrees
clockwise), while explicit orientation remains authoritative. Decode the complete
PNG, verify 1:1 physical content and blank nominal margins as described in ADR-0004,
and then call `papplJobFilterImage`. Its copy loop expands the job's copies exactly
once and drives the production raster callbacks.

## Consequences

Explicit 50 mm PNG pages stay 50 mm. Omitted lengths retain the PNG convenience,
including deliberate white padding and minimum-length padding. Overlong or
oversized 1:1 content fails before page output. Standard scaling requests remain
PAPPL's responsibility. JPEG keeps its standard selected/default media sizing;
this decision adds no JPEG decoder or automatic length promise.

Real IPP tests verify MIME routing, copy counts, named and collection geometry,
asymmetric orientation markers, physical margins, and length boundaries.
