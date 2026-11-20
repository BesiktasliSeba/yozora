#!/usr/bin/env python3
"""writes the 96 pixel alpha masks of app/icons/ui from the svg files of tools/material-symbols

usage: make_ui_icons.py [SVG_DIRECTORY [OUTPUT_DIRECTORY]]
the colored marks of the search engines come from the png files of the logos directory beside SVG_DIRECTORY. needs rsvg-convert and pillow. every glyph is drawn black on a transparent ground, the app tints the alpha channel with the color of the theme
"""
import os
import subprocess
import sys

from PIL import Image

# four times the 24 dp grid, enough for a 3x screen at 28 points
PIXELS = 96
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) > 3:
        raise SystemExit("usage: make_ui_icons.py [SVG_DIRECTORY [OUTPUT_DIRECTORY]]")
    source = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tools", "material-symbols")
    output = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "app", "icons", "ui")
    logos = os.path.join(os.path.dirname(source), "logos")
    names = sorted(name[:-4] for name in os.listdir(source) if name.endswith(".svg"))
    if not names:
        raise SystemExit("make_ui_icons: no svg file in " + source)
    os.makedirs(output, exist_ok=True)
    for name in names:
        target = os.path.join(output, "yz-" + name + ".png")
        subprocess.run(["rsvg-convert", "-w", str(PIXELS), "-h", str(PIXELS), "-o", target, os.path.join(source, name + ".svg")], check=True)
    # the marks of the search engines keep their colors, the app draws them as they are
    for name in sorted(name[:-4] for name in os.listdir(logos) if name.endswith(".png")):
        mark = Image.open(os.path.join(logos, name + ".png")).convert("RGBA")
        mark.resize((PIXELS, PIXELS), Image.LANCZOS).save(os.path.join(output, "yz-logo-" + name + ".png"), optimize=True)

if __name__ == "__main__":
    main()
