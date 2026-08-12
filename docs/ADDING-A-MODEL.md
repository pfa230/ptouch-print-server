# Adding a P-touch model

This guide explains how to add a Brother P-touch model to the device table. A new
entry makes the model **recognized and discoverable**. It does **not** make it
print-supported. See the caveats at the end.

## The `pt_dev` fields

Each device is a `pt_dev` row in `g_devs[]` (`src/tables.c`). The struct is in
`src/tables.h`:

```c
typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    const char *name;
    int         max_px;   /* print-head width in dots */
    int         dpi;
    uint32_t    flags;
} pt_dev;
```

Where the values come from:

| Field | Source |
|-------|--------|
| `vid` | Brother's USB vendor id, always `0x04f9`. |
| `pid` | The model's USB product id. Take it from upstream `ptdevs[]` in [hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print) `src/libptouch.c`, or read it off the hardware with `lsusb` (the `04f9:XXXX` pair). |
| `name` | The model string, e.g. `"PT-2730"`. Match the `MDL:` value the device reports in its IEEE-1284 device id so name lookup resolves. |
| `max_px` | Print-head width in dots, from the device spec / upstream. Every model in the table today is `128`. |
| `dpi` | Print resolution, `180` for every current model. |
| `flags` | Per-model protocol flags, OR-ed together (see below). |

## The four flags

Copied from `src/tables.h`:

```c
#define FLAG_NONE            0x00u
#define FLAG_RASTER_PACKBITS 0x01u  /* sendraster wraps a fake-PackBits run */
#define FLAG_P700_INIT       0x02u  /* rasterstart uses "1b 69 61 01" */
#define FLAG_PLITE           0x04u  /* P-Lite mode (not driven) */
```

- `FLAG_NONE`: plain uncompressed raster, PT-2730-style. This is the only path
  the print code drives today.
- `FLAG_RASTER_PACKBITS`: `sendraster` must wrap each line in a single
  uncompressed PackBits run (`pt_cmd_sendraster_packbits` in `src/protocol.h`).
- `FLAG_P700_INIT`: `rasterstart` uses the P700 variant (`1b 69 61 01`) instead of
  the default.
- `FLAG_PLITE`: the model is in P-Lite mode, which this project does not drive.

Set the flags to describe the model accurately even though the driver does not yet
dispatch on them. They document the protocol the model needs once per-model
dispatch lands.

## Adding the entry

1. Add a row to `g_devs[]` in `src/tables.c`, in pid order:

   ```c
   {0x04f9, 0xXXXX, "PT-XXXX", 128, 180, FLAG_NONE},
   ```

2. Add a matching assertion to `tests/test_tables.c` so lookup is covered. Follow
   the existing pattern: look the device up by id and by name and assert the
   fields:

   ```c
   const pt_dev *d = pt_lookup_dev(0x04f9, 0xXXXX);
   assert(d && strcmp(d->name, "PT-XXXX") == 0);
   assert(d->max_px == 128 && d->dpi == 180);
   assert(pt_lookup_name("PT-XXXX") == d);
   ```

3. Rebuild and run the core tests:

   ```bash
   cmake -S . -B build && cmake --build build && ctest --test-dir build
   ```

## Hard rule: do not add wide-head models yet

**Do not add any model whose head is not 128 dots** (for example the PT-3600 at
384 px) until the raster geometry is generalized.

The print path is fixed at **16 bytes / 128 dots**. `pt_pack_line` and
`pt_pack_window` (`src/raster.h`) pack into a 16-byte line.

Note this is no longer a simple "reject anything wider" check. Since media is
advertised at the nominal tape width with real margins (ADR-0005), `cupsWidth` is
the **full page** width - 170 px for 24 mm tape - and `r_writeline` extracts the
printable window `[left, left + win)` and centres it in the head, clamping `win`
to 128. So a wide-head model would not fail loudly; it would silently print only
the middle 128 dots of its page. Adding one requires generalizing the packing
width, the clamp in `r_writeline`, and the `px > 128` eligibility test in
`media_col_for` together.

## Caveats: recognized is not supported

A new entry is **untested and not print-supported** until:

- The model is verified on real hardware. Only the PT-2730 (`04f9:2041`) is
  hardware-certified. The status read, media detection, and print geometry are all
  validated against it and nothing else.
- For PackBits or P700 models (`FLAG_RASTER_PACKBITS`, `FLAG_P700_INIT`), per-model
  flag dispatch exists in the driver. Today the print callbacks emit PT-2730
  protocol (`FLAG_NONE`) unconditionally, so a flagged model would receive the
  wrong byte stream.

Until then a new row only adds discovery and metadata.

## Source of new PIDs

The canonical source of new model ids and their flags is upstream
[hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(`ptdevs[]` in `src/libptouch.c`). Transcribe the vid/pid/name/flags from there and
confirm `max_px` and `dpi` against the device spec.
