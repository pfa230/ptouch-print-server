# 5. Advertise nominal tape width with real margins

**Status:** Accepted (supersedes the original M3 geometry decision)

## Context

The print head is 128 dots wide (~18.07 mm at 180 dpi), narrower than the 24 mm tape it can carry.
The first implementation advertised media at the **printable** width with zero margins, because that
made `cupsWidth` equal the imageable pixel count exactly.

The consequence surfaced in practice: a 24 mm tape announced itself as `om_18.07x100mm`, and any
client reasoning in tape sizes rejected it ("template requires 24mm media but 18.07mm is loaded").
That conflates IPP's `media-size` (the physical medium) with the printable area.

## Decision

Advertise the **nominal** tape width as `media-size`, and express the unprintable edge as symmetric
left/right margins:

```
size_width  = tape_mm * 100              (24 mm -> 2400 centimm)
printable   = imageable px in centimm     (128 px -> 1807)
left/right  = (size_width - printable)/2  (-> 296 each)
```

## Consequences

- Media reads as `om_24x100mm`, `om_12x100mm`. Clients match the tape they bought.
- **`cupsWidth` becomes the full media width, on every CUPS version.** `cupsRasterInitHeader` computes
  `media->width * xdpi / 2540` and applies margins only to `ImageBox*`. CUPS 2.5 does not change this
  (verified in source after an incorrect assumption that it would).
- Therefore the driver must extract the printable window `[left, left+width)` from each raster line
  and centre it in the head. This is not a workaround; it is how PWG raster is designed, and it is
  what every real printer driver does.
- The width guard now compares full-page px to full-page px (see ADR-0004).
- 36 mm tape stays unadvertised: its imageable width exceeds the head.

## Alternatives considered

- **Keep printable-as-media-size** and have clients adapt. Rejected: every client would need the same
  workaround, and the declaration would stay wrong.
- **Wait for CUPS 2.5.** Rejected once its raster code was actually read - it changes the API shape,
  not the width semantics.
