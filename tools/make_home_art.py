#!/usr/bin/env python3
"""writes the art of the start page: the sticker of tools/art without its black ground (app/icons/yozora-hero.png and @2x) and the name set in
Metropolis Bold as an alpha mask (app/icons/ui/yz-wordmark.png) that the app tints with the color of the theme.
needs Pillow. Metropolis is the face of the Firefox wordmark; the file comes from @fontsource/metropolis, see tools/art/LICENSE-fontsource-metropolis.txt"""
import os
import sys

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont
except ImportError:
    sys.exit("make_home_art: Pillow is required")

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
STICKER = os.path.join(ROOT, "tools", "art", "yozora-sticker.png")
FONT = os.path.join(ROOT, "tools", "art", "metropolis-700.woff")
HERO_SIDES = {"yozora-hero.png": 240, "yozora-hero@2x.png": 480}
MARKER = (255, 0, 255)
NAME = "Yozora"


def enclosed_ground(source, ground):
    """pockets of the black ground that the outline closes off, between a tail and the hair for example, do not touch the corner. every region
    of pure black that is large enough to be ground and not a dark line of the drawing is added to it"""
    dark = source.convert("L").point(lambda v: 255 if v < 12 else 0)
    dark = ImageChops.subtract(dark, ground)
    pockets = Image.new("L", source.size, 0)
    for _ in range(200):
        box = dark.getbbox()
        if not box:
            break
        width = box[2] - box[0]
        row = dark.crop(box).tobytes().index(255)
        seed = (box[0] + row % width, box[1] + row // width)
        marked = dark.copy()
        ImageDraw.floodfill(marked, seed, 128)
        region = marked.point(lambda v: 255 if v == 128 else 0)
        dark = ImageChops.subtract(dark, region)
        if region.histogram()[255] >= 30:
            pockets = ImageChops.lighter(pockets, region)
    return pockets


def cut_out(path):
    """the sticker has a white outline on a black ground. the ground is the dark region that touches the corner, and the pixels where the outline
    fades into it get a transparency that follows their brightness, so the edge has no dark halo"""
    source = Image.open(path).convert("RGB")
    flooded = source.copy()
    ImageDraw.floodfill(flooded, (0, 0), MARKER, thresh=40)
    channels = [band.point(lambda v, level=level: 255 if v == level else 0) for band, level in zip(flooded.split(), MARKER)]
    ground = ImageChops.multiply(ImageChops.multiply(channels[0], channels[1]), channels[2])
    ground = ImageChops.lighter(ground, enclosed_ground(source, ground))
    edge = ImageChops.subtract(ground.filter(ImageFilter.MaxFilter(7)), ground)
    fading = source.convert("L").point(lambda v: min(255, int(v * 1.25)))
    alpha = Image.composite(fading, ImageChops.invert(ground), edge)
    color = Image.composite(Image.new("RGB", source.size, (255, 255, 255)), source, edge)
    color.putalpha(alpha)
    box = alpha.getbbox()
    cropped = color.crop(box)
    side = max(cropped.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(cropped, ((side - cropped.width) // 2, (side - cropped.height) // 2))
    return square


def wordmark():
    font = ImageFont.truetype(FONT, 240)
    box = font.getbbox(NAME)
    pad = 24
    mask = Image.new("L", (box[2] - box[0] + 2 * pad, box[3] - box[1] + 2 * pad), 0)
    ImageDraw.Draw(mask).text((pad - box[0], pad - box[1]), NAME, font=font, fill=255)
    image = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    image.putalpha(mask)
    return image


def main():
    hero = cut_out(STICKER)
    for name, side in HERO_SIDES.items():
        hero.resize((side, side), Image.LANCZOS).save(os.path.join(ROOT, "app", "icons", name), optimize=True)
    wordmark().save(os.path.join(ROOT, "app", "icons", "ui", "yz-wordmark.png"), optimize=True)


if __name__ == "__main__":
    main()
