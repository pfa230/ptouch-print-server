#!/usr/bin/env python3
"""Write label.png: a 76x180-dot outline at 180 dpi, the full 12 mm imageable width."""
import sys
from PIL import Image, ImageDraw

image = Image.new("L", (76, 180), 255)
ImageDraw.Draw(image).rectangle((0, 0, 75, 179), outline=0, width=2)
image.save(sys.argv[1] if len(sys.argv) > 1 else "label.png", dpi=(180, 180))
