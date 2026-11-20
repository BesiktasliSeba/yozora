#!/usr/bin/env python3
"""writes the fallback fonts of the catalog into app/fonts: the scripts and symbols Roboto lacks, so a page shows letters and not boxes.

small scripts are copied from a Noto directory (SIL OFL 1.1 or Apache 2.0, see app/NOTICE-Noto.txt). the four regions of Noto Sans CJK are cut down
to the everyday character sets of their national standards (JIS X 0208 rows 1 to 47, GB2312 level 1, Big5 common, KS X 1001 hangul), since the catalog keeps
every font file in memory and the whole collection is 19 MB. needs fonttools (pip install fonttools brotli)

usage: make_fallback_fonts.py [--noto DIR] [--cjk FILE] [--out DIR]"""
import argparse
import shutil
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTCollection

SMALL = [
    "NotoSans-Regular", "NotoSansArabic-Regular", "NotoSansHebrew-Regular", "NotoSansDevanagari-Regular", "NotoSansBengali-Regular",
    "NotoSansTamil-Regular", "NotoSansTelugu-Regular", "NotoSansKannada-Regular", "NotoSansMalayalam-Regular", "NotoSansGujarati-Regular",
    "NotoSansGurmukhi-Regular", "NotoSansOriya-Regular", "NotoSansSinhala-Regular", "NotoSansThai-Regular", "NotoSansLao-Regular",
    "NotoSansKhmer-Regular", "NotoSansMyanmar-Regular", "NotoSansGeorgian-Regular", "NotoSansArmenian-Regular", "NotoSansEthiopic-Regular",
    "NotoSansSymbols-Regular", "NotoSansSymbols2-Regular", "NotoSansMath-Regular",
]


def codes(codec, leads, trails=range(0x21, 0xff)):
    found = set()
    for lead in leads:
        for trail in trails:
            try:
                text = bytes([lead, trail]).decode(codec)
            except UnicodeDecodeError:
                continue
            if len(text) == 1:
                found.add(ord(text))
    return found


def ascii_and_punctuation():
    found = set(range(0x20, 0x7f))
    found |= set(range(0x3000, 0x3040)) | set(range(0xff00, 0xfff0)) | set(range(0x2010, 0x2030))
    return found


def region_characters(index):
    base = ascii_and_punctuation()
    if index == 0:  # JP: kana, symbols and the first level of kanji
        return base | set(range(0x3040, 0x3100)) | codes("euc_jp", list(range(0xa1, 0xa9)) + list(range(0xb0, 0xd0)), range(0xa1, 0xff))
    if index == 1:  # KR: hangul syllables of KS X 1001 and its symbols
        return base | set(range(0x3130, 0x3190)) | codes("euc_kr", list(range(0xa1, 0xad)) + list(range(0xb0, 0xc9)), range(0xa1, 0xff))
    if index == 2:  # SC: GB2312 symbols and level 1
        return base | codes("gb2312", list(range(0xa1, 0xaa)) + list(range(0xb0, 0xd8)), range(0xa1, 0xff))
    # TC: Big5 symbols and the common characters
    return base | codes("big5", list(range(0xa1, 0xa4)) + list(range(0xa4, 0xc7)), list(range(0x40, 0x7f)) + list(range(0xa1, 0xff)))


def cut_cjk(source, out):
    collection = TTCollection(str(source))
    # faces 0 to 3 are JP, KR, SC and TC, the collection goes on with HK and the mono faces
    for index, font in enumerate(collection.fonts[:4]):
        family = font["name"].getDebugName(1)
        if not family or not family.startswith("Noto Sans CJK"):
            raise SystemExit(f"unexpected face {index} in {source}: {family}")
        options = subset.Options()
        options.layout_features = ["*"]
        options.hinting = False
        options.desubroutinize = True
        options.notdef_outline = True
        options.name_IDs = [0, 1, 2, 3, 4, 6, 13, 14]
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=sorted(region_characters(index)))
        subsetter.subset(font)
        name = "NotoSansCJK-" + family.rsplit(" ", 1)[1] + ".otf"
        font.flavor = None
        font.save(str(out / name))
        print(f"{name}: {(out / name).stat().st_size} bytes, {len(font.getGlyphOrder())} glyphs")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--noto", default="/usr/share/fonts/noto")
    parser.add_argument("--cjk", default="/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc")
    parser.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "app" / "fonts"))
    arguments = parser.parse_args()
    out = Path(arguments.out)
    out.mkdir(parents=True, exist_ok=True)
    for name in SMALL:
        source = Path(arguments.noto) / f"{name}.ttf"
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        shutil.copyfile(source, out / f"{name}.ttf")
    cut_cjk(Path(arguments.cjk), out)


if __name__ == "__main__":
    sys.exit(main())
