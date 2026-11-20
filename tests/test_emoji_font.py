#!/usr/bin/env python3
"""checks app/fonts/AppleColorEmoji.ttf: family name, color strike, and that sequences keep their own glyphs

needs fonttools, for example the venv of ios/exteria/tools/venv-assets. run: python3 -B tests/test_emoji_font.py [font]
"""
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

font = TTFont(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "app/fonts/AppleColorEmoji.ttf")
cmap = font.getBestCmap()
ligatures = {}
for first, group in font["GSUB"].table.LookupList.Lookup[0].SubTable[0].ligatures.items():
    for ligature in group:
        ligatures[(first, *ligature.Component)] = ligature.LigGlyph


def glyph(*points):
    names = tuple(cmap[p] for p in points)
    return cmap[points[0]] if len(points) == 1 else ligatures[names]


assert font["name"].getDebugName(1) == "Apple Color Emoji"
assert font["CBLC"].strikes[0].bitmapSizeTable.bitDepth == 32
bitmaps = font["CBDT"].strikeData[0]
assert all(data.imageData[:8] == b"\x89PNG\r\n\x1a\n" for data in bitmaps.values()), "a strike is not a png"
assert glyph(0x1F600) in bitmaps
# a ligature matches by glyph, so the joiners and the regional indicators must not share one blank glyph
assert len({cmap[p] for p in (0x200D, 0xFE0F, 0x20E3, 0x1F1F7, 0x1F1FA, 0x31)}) == 6
shown = {
    "heart with selector": glyph(0x2764, 0xFE0F),
    "flag ru": glyph(0x1F1F7, 0x1F1FA),
    "flag us": glyph(0x1F1FA, 0x1F1F8),
    "keycap 1": glyph(0x31, 0xFE0F, 0x20E3),
    "keycap 2": glyph(0x32, 0xFE0F, 0x20E3),
    "family": glyph(0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467),
    "thumbs up tone": glyph(0x1F44D, 0x1F3FD),
}
assert len(set(shown.values())) == len(shown), shown
assert all(name in bitmaps for name in shown.values())
# the longest sequence wins over its prefix: the family is not the first man followed by joined leftovers
assert glyph(0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467) != glyph(0x1F468)
print("emoji font ok: %d pictures, %d sequences" % (len(bitmaps), len(ligatures)))
