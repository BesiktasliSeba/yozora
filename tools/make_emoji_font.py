#!/usr/bin/env python3
"""writes app/fonts/AppleColorEmoji.ttf, a color bitmap font (CBDT, png strikes) from the emoji pack of exteria

usage: make_emoji_font.py --data ios/exteria/core/emoji/exteria_emoji_data.c \
                          --pack ios/exteria/resources/emoji/emoji.pack --out app/fonts/AppleColorEmoji.ttf

the pictures are the ones exteria draws, so a page shows the same set as the chats. every sequence the table lists maps
to the glyph of its picture: single code points through the cmap, longer sequences (zwj, flags, keycaps, skin tones)
through ccmp ligatures, longest first. webcore asks the font manager for the family "Apple Color Emoji" when a cluster
is emoji, so the family name is fixed. needs fonttools and pillow with webp support.
"""
import argparse
import re
import struct
import sys
from io import BytesIO

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables.BitmapGlyphMetrics import SmallGlyphMetrics
from fontTools.ttLib.tables.C_B_D_T_ import cbdt_bitmap_format_17
from fontTools.ttLib.tables.C_B_L_C_ import table_C_B_L_C_
from fontTools.ttLib.tables.E_B_L_C_ import BitmapSizeTable, SbitLineMetrics, Strike, eblc_index_sub_table_1
from fontTools.ttLib.tables import otTables
from PIL import Image

UNITS = 2048
# strike ppem below the picture side: a 56 pixel picture then draws 1.17 em wide, the size Apple's emoji have
PPEM = 48
ASCENT = 1950
DESCENT = 520
BEARING_Y = 45
JOINERS = (0x200D, 0xFE0F, 0x20E3, 0x1F3FB, 0x1F3FC, 0x1F3FD, 0x1F3FE, 0x1F3FF)
REGIONAL = range(0x1F1E6, 0x1F200)
TAGS = range(0xE0020, 0xE0080)


def read_table(path):
    text = open(path, encoding="utf-8").read()
    pool = [int(v, 16) for v in re.search(r"exteria_emoji_points\[\d+\] = \{(.*?)\};", text, re.S).group(1).replace("\n", " ").replace(",", " ").split()]
    rows = re.search(r"exteria_emoji_entries\[\d+\] = \{(.*?)\n\};", text, re.S).group(1)
    entries = []
    for row in re.findall(r"\{([^{}]*)\}", rows):
        offset, length, _status, _version, _group, canonical, image, _tone = (int(v) for v in row.split(","))
        entries.append((tuple(pool[offset:offset + length]), canonical, image))
    return entries


def read_pack(path):
    blob = open(path, "rb").read()
    magic, count, side, _ = struct.unpack_from("<4sIHH", blob)
    if magic != b"EXEM":
        sys.exit("not an emoji pack: " + path)
    pictures = []
    for i in range(count):
        start, size = struct.unpack_from("<II", blob, 12 + 8 * i)
        if start + size > len(blob):
            sys.exit("the pack is truncated")
        pictures.append(blob[start:start + size])
    return pictures, side


def to_png(webp, side):
    image = Image.open(BytesIO(webp)).convert("RGBA")
    if image.size != (side, side):
        sys.exit("a picture is not %d pixels square" % side)
    # the font manager keeps the whole file in memory, a 256 color palette with alpha cuts the png to a third
    image = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    out = BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--pack", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    entries = read_table(args.data)
    pictures, side = read_pack(args.pack)
    if len(pictures) + 1 >= 0xFFFF:
        sys.exit("too many pictures for the glyph index")

    # a glyph per picture: .notdef is glyph 0, picture i is glyph i + 1
    names = [".notdef"] + ["e%d" % i for i in range(len(pictures))]
    advance = round(UNITS * side / PPEM)

    # the picture an entry draws is the one of its canonical entry
    picture_of = {}
    for points, canonical, image in entries:
        shown = image if image != 0xFFFF else entries[canonical][2]
        if shown != 0xFFFF:
            picture_of[points] = shown

    cmap = {}
    ligatures = {}
    for points, image in picture_of.items():
        if len(points) == 1:
            cmap[points[0]] = names[image + 1]
        else:
            ligatures[points] = names[image + 1]
    # a joiner, selector or regional indicator needs a blank glyph of its own: ligature components match by
    # glyph, so a shared blank would make every sequence built from them the same
    blanks = set()
    used = set()
    for points in ligatures:
        used.update(points)
    for point in sorted(used):
        if point not in cmap:
            cmap[point] = "u%X" % point
            blanks.add(cmap[point])
            names.append(cmap[point])

    fb = FontBuilder(UNITS, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    empty = TTGlyphPen(None).glyph()
    fb.setupGlyf({name: empty for name in names})
    metrics = {name: (0 if name == ".notdef" or name in blanks else advance, 0) for name in names}
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    fb.setupNameTable({"familyName": "Apple Color Emoji", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASCENT, sTypoDescender=-DESCENT, sTypoLineGap=0, usWinAscent=ASCENT, usWinDescent=DESCENT,
                usWeightClass=400, fsType=0)
    fb.setupPost()

    # ccmp keeps the sequences on whatever features the page turns off, letter-spacing disables liga
    by_first = {}
    for points, glyph in ligatures.items():
        by_first.setdefault(cmap[points[0]], []).append((points, glyph))
    subtable = otTables.LigatureSubst()
    subtable.ligatures = {}
    for first, group in by_first.items():
        group.sort(key=lambda item: -len(item[0]))
        subtable.ligatures[first] = []
        for points, glyph in group:
            ligature = otTables.Ligature()
            ligature.Component = [cmap[p] for p in points[1:]]
            ligature.LigGlyph = glyph
            subtable.ligatures[first].append(ligature)
    lookup = otTables.Lookup()
    lookup.LookupType = 4
    lookup.LookupFlag = 0
    lookup.SubTable = [subtable]
    lookup.SubTableCount = 1
    gsub = newTable("GSUB")
    gsub.table = otTables.GSUB()
    gsub.table.Version = 0x00010000
    scripts = otTables.ScriptList()
    script = otTables.ScriptRecord()
    script.ScriptTag = "DFLT"
    script.Script = otTables.Script()
    script.Script.DefaultLangSys = otTables.LangSys()
    script.Script.DefaultLangSys.ReqFeatureIndex = 0xFFFF
    script.Script.DefaultLangSys.FeatureIndex = [0]
    script.Script.LangSysRecord = []
    scripts.ScriptRecord = [script]
    features = otTables.FeatureList()
    feature = otTables.FeatureRecord()
    feature.FeatureTag = "ccmp"
    feature.Feature = otTables.Feature()
    feature.Feature.LookupListIndex = [0]
    features.FeatureRecord = [feature]
    lookups = otTables.LookupList()
    lookups.Lookup = [lookup]
    gsub.table.ScriptList = scripts
    gsub.table.FeatureList = features
    gsub.table.LookupList = lookups
    fb.font["GSUB"] = gsub

    # CBDT image format 17: small metrics and a png, one strike for every picture
    cbdt = newTable("CBDT")
    cbdt.version = 3.0
    cbdt.strikeData = [{}]
    cblc = table_C_B_L_C_()
    cblc.version = 3.0
    strike_metrics = SbitLineMetrics()
    for field in ("ascender", "descender", "widthMax", "caretSlopeNumerator", "caretSlopeDenominator", "caretOffset",
                  "minOriginSB", "minAdvanceSB", "maxBeforeBL", "minAfterBL", "pad1", "pad2"):
        setattr(strike_metrics, field, 0)
    strike_metrics.ascender = BEARING_Y
    strike_metrics.descender = BEARING_Y - side
    strike_metrics.widthMax = side
    strike_metrics.caretSlopeNumerator = 1
    strike = Strike()
    size = BitmapSizeTable()
    size.hori = strike_metrics
    size.vert = SbitLineMetrics()
    for field in strike_metrics.__dict__:
        setattr(size.vert, field, getattr(strike_metrics, field))
    size.colorRef = 0
    size.startGlyphIndex = 1
    size.endGlyphIndex = len(pictures)
    size.ppemX = PPEM
    size.ppemY = PPEM
    size.bitDepth = 32
    size.flags = 1
    strike.bitmapSizeTable = size
    strike.indexSubTables = []
    for i, webp in enumerate(pictures):
        bitmap = cbdt_bitmap_format_17(b"", fb.font)
        bitmap.metrics = SmallGlyphMetrics()
        bitmap.metrics.height = side
        bitmap.metrics.width = side
        bitmap.metrics.BearingX = 0
        bitmap.metrics.BearingY = BEARING_Y
        bitmap.metrics.Advance = side
        bitmap.imageData = to_png(webp, side)
        cbdt.strikeData[0][names[i + 1]] = bitmap
    # one index subtable per picture would bloat the table, a format 1 subtable lists every offset together
    sub = eblc_index_sub_table_1(b"", fb.font)
    sub.indexFormat = 1
    sub.imageFormat = 17
    sub.firstGlyphIndex = 1
    sub.lastGlyphIndex = len(pictures)
    sub.names = names[1:len(pictures) + 1]
    sub.imageSize = None
    strike.indexSubTables = [sub]
    cblc.strikes = [strike]
    fb.font["CBDT"] = cbdt
    fb.font["CBLC"] = cblc

    fb.save(args.out)
    total = sum(len(b.imageData) for b in cbdt.strikeData[0].values())
    print("glyphs %d, cmap %d, ligatures %d, png bytes %d" % (len(names), len(cmap), len(ligatures), total))


if __name__ == "__main__":
    main()
