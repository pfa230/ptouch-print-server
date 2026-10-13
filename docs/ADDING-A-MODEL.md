# Adding a P-touch model

This guide explains how to add a Brother P-touch model to the device table. A new
entry makes the model **recognized in metadata**. Discovery and printing remain
restricted to the explicitly supported PT-2730 and PT-P710BT paths. PT-2730
remains the default; P710BT requires `PTOUCH_MODEL=PT-P710BT`. A table row does
not grant print support. P710BT integrated USB acceptance passed on 9/12/18 mm tape;
6/24 mm remain software-only coverage.
See the support gate below.

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

- `FLAG_NONE`: plain uncompressed raster, used by PT-2730.
- `FLAG_RASTER_PACKBITS`: `sendraster` must wrap each line in a single
  uncompressed PackBits run (`pt_cmd_sendraster_packbits` in `src/protocol.h`).
- `FLAG_P700_INIT`: `rasterstart` uses the P700 variant (`1b 69 61 01`) instead of
  the default.
- `FLAG_PLITE`: the model is in P-Lite mode, which this project does not drive.

Flags describe encoding and raster-mode commands, not print support. The driver
uses them only after resolving an explicitly supported model. P710BT has both
`FLAG_RASTER_PACKBITS` and `FLAG_P700_INIT`; it also needs the model-specific
extended page setup below. Sharing these flags does not enable another model.

## The implemented P710BT command path

P710BT metadata is `04f9:20af`, 128 dots, normal 180 dpi. Its page setup differs
from PT-2730's legacy eight-byte `ESC i c` command:

| Operation | Builder / command |
| --- | --- |
| Fresh-run preparation | `pt_cmd_invalidate`: 100 zero bytes, then `pt_cmd_init`: `1b 40`. Omit both when continuing a healthy held run. |
| Raster mode | `pt_cmd_rasterstart` with `FLAG_P700_INIT`: `1b 69 61 01`. |
| Automatic status | `pt_cmd_auto_status(o, true)`: `1b 69 21 00`. |
| Extended page information | `pt_cmd_printinfo_extended`: `1b 69 7a 84 00 mm 00 r0 r1 r2 r3 page 00`. `r0..r3` are the validated destination page's row count, little-endian. |
| Compression | `pt_cmd_compression(o, 2)`: `4d 02` selects TIFF PackBits. |
| Raster line | `pt_cmd_sendraster_packbits` wraps each complete 16-byte head line as `47 11 00 0f` followed by those bytes, a single literal run. |
| Feed margin | `pt_cmd_margin(o, 14)`: `1b 69 64 0e 00`. |

The extended page command uses width-valid/recovery flags `84`, unspecified
media kind, zero fixed-length and reserved fields. `page` is 0 for a freshly
initialized printer run and 1 for subsequent pages, including the first page of
an IPP batch continuing a healthy held run. Cross-batch indication and finishing
were verified by the integrated physical gate; separate-label probes did not
verify them. Normal resolution and finishing use the existing mode/advanced
builders and batch lifecycle, without half-cut or cut-every-N commands.

Direct USB geometry probes on 2026-10-12 used this setup with first-page
indication 0, normal resolution, autocut, chain-off, and terminal EJECT. The
operator confirmed complete 52-, 76-, and 120-dot rectangles on 9, 12, and 18 mm
tape. The existing 32/52/76/120/128-dot table is shared by both supported paths;
6 and 24 mm remain software/documentation coverage for P710BT. These observations
establish usable geometry, not integrated finishing, continuation, or recovery
certification. The change's validation summary records retained probe evidence
and any unavailable files separately.

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
   cmake -S . -B build-core -DPTOUCH_CORE_ONLY=ON
   cmake --build build-core
   ctest --test-dir build-core --output-on-failure
   ```

## Hard rule: do not add wide-head models yet

**Do not add any model whose head is not 128 dots** (for example the PT-3600 at
384 px) until the raster geometry is generalized.

The print path is fixed at **16 bytes / 128 dots**. `pt_pack_line` and
`pt_pack_window` (`src/raster.h`) pack into a 16-byte line.

The supported-model gate rejects other models before discovery or print output,
including models with wider heads. Supporting one requires a validated protocol,
generalized packing and imageable geometry, and an explicit support-gate change;
do not enable it merely by adding metadata.

## Caveats: recognized is not supported

A new entry only adds lookup metadata. Enabling another model requires an
implemented protocol path, consistent queue/USB identity and status handling,
software regressions, and an explicit supported-model gate change. PT-2730
(`04f9:2041`) and P710BT (`04f9:20af`) are hardware-certified. P710BT's integrated
gate covers 9/12/18 mm USB printing, finishing, continuation, and recovery;
6/24 mm remain software-only. Recognition, pure command tests, and separate
geometry probes alone do not certify another model.

`pt_usb_supported_model` and the driver identity check must remain restricted
to these two implemented paths. Other catalog entries, including PackBits/P700
models, remain unsupported and must receive no output. Do not broaden the gate
based on flags or silently fall back to an attached model.

## Source of new PIDs

The canonical source of new model ids and their flags is upstream
[hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(`ptdevs[]` in `src/libptouch.c`). Transcribe the vid/pid/name/flags from there and
confirm `max_px` and `dpi` against the device spec.
