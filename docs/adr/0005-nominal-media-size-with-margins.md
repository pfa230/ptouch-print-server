# 5. Nominal tape width and one canonical imageable window

**Status:** Accepted (supersedes the original M3 geometry decision)

## Context

The PT-2730's 128-dot head is narrower than 24 mm tape. Advertising only the
printable width as media made 24 mm tape appear as roughly 18 mm media. Integer
conversions of symmetric physical margins also gave the renderer and driver
different printable windows, clipping edge pixels on otherwise valid designs.

## Decision

Advertise nominal physical width as `media-size`. Use one shared tape geometry
helper at 180 dpi to derive nominal pixel width, the exact imageable pixel count,
an exclusive canonical pixel interval, and publishable physical margins.
Supported widths are 6/9/12/18/24 mm, with 32/52/76/120/128 imageable dots.

Left and right IPP margins may differ slightly so conversion back to printer
pixels agrees with the canonical interval and PAPPL's image-placement arithmetic.
The printer-wide minimum margin permits every supported tape; each actual
`media-ready` and default carries its own margins. Selected margins cannot expand
the physical printable window. Per-width custom ranges admit only ±0.01 mm
rounding around the nominal width, retaining the submitted dimensions and
physical imageable guard. This accommodates CUPS metric-to-point conversion.
CUPS 2.4 PPDs still apply one global maximum custom margin, so full-width designs
need a named preset or standard media-col with the published tape margins;
explicit client margins are never silently replaced.

`cupsWidth` remains the full nominal page width. The adapter extracts the
canonical physical window from each full raster line and centers it in the head.
Physical tape comparison therefore uses full nominal page width, while PNG source
preflight uses the physical imageable width. Do not mix these coordinate spaces.

## Consequences

Clients see the tape size they bought, including 18 mm ready-media that was
previously rejected by the printer-wide margin bound. One definition serves
publication, validation, and extraction. 36 mm remains unadvertised because its
imageable width exceeds the head.

## Alternatives considered

Printable-as-media-size requires every client to compensate for a false physical
size. Relying on a CUPS version change does not correct the geometry: CUPS computes
full media width and represents margins through its image box.
