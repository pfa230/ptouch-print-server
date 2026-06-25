#!/bin/sh
# Generates M0 test fixtures (PNG) into this script's directory.
# TAPE_PX = loaded tape's imageable px (default 76 = 12mm, per Probe A --info).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
TAPE_PX="${TAPE_PX:-76}"
python3 - "$DIR" "$TAPE_PX" <<'PY'
import sys
from PIL import Image, ImageDraw
d, tape = sys.argv[1], int(sys.argv[2])
def save(img, name): img.save(f"{d}/{name}.png")

# arrow.png 300x64: asymmetric arrow pointing +x, with a tail-top block so BOTH
# axes are distinguishable (catches transpose AND flip).
im = Image.new("L", (300, 64), 255); dr = ImageDraw.Draw(im)
dr.line((10, 32, 260, 32), fill=0, width=8)        # shaft along +x
dr.polygon((260, 12, 295, 32, 260, 52), fill=0)    # arrowhead at +x
dr.rectangle((10, 6, 44, 22), fill=0)              # tail-TOP block (breaks y-symmetry)
save(im, "arrow")

save(Image.new("L", (200, tape), 0), "exact")      # height == tape px (fills head)
save(Image.new("L", (200, tape + 40), 0), "tall")  # too tall
save(Image.new("L", (200, 40), 0), "short")        # too short

g = Image.new("L", (200, 64))                       # left black -> right white
for x in range(200):
    v = int(x * 255 / 199)
    for y in range(64):
        g.putpixel((x, y), v)
save(g, "gradient")

# tick.png: vertical tick marks at cross-tape offsets 0/24/48/72 px for the leader test
t = Image.new("L", (200, tape), 255); td = ImageDraw.Draw(t)
for off in (0, 24, 48, 72):
    if off < tape:
        td.line((0, off, 30, off), fill=0, width=3)
save(t, "tick")

print("fixtures written to", d, "tape_px", tape)
PY
