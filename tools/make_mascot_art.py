#!/usr/bin/env python3
"""writes the art of the fox mascot from the pictures in tools/art: the start page (fox-hero), the other screens and the
about screen (fox-sticker) and the error screens (fox-error), each without the black ground the pictures come on.
the ground is the dark region that touches the border, plus enclosed pockets of pure black. the pixels at the edge
fade into it, so their transparency follows their brightness against the nearest pixel inside, and their color is that
inner pixel, which leaves no dark halo when the picture is scaled.
needs Pillow, numpy and scipy (PYTHON with them, for example a venv)"""
import os
import sys

try:
    import numpy
    from PIL import Image
    from scipy import ndimage
except ImportError:
    sys.exit("make_mascot_art: Pillow, numpy and scipy are required")

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ART = os.path.join(ROOT, "tools", "art")
ICONS = os.path.join(ROOT, "app", "icons")
# source, written name and the sides of the square pictures, in pixels, by scale
SETS = (
    ("fox-hero-source.png", "fox-hero", (("", 240), ("@2x", 480))),
    ("fox-sticker-source.png", "fox-sticker", (("", 128), ("@2x", 256), ("@3x", 384))),
    ("fox-error-source.png", "fox-error", (("", 160), ("@2x", 320), ("@3x", 480))),
)
# the darkest pixel of the drawing is the violet of the nose, well above this
GROUND_LEVEL = 40
POCKET_LEVEL = 12
POCKET_AREA = 30
EDGE_WIDTH = 3


def cut_out(path):
    rgb = numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.float32)
    value = rgb.max(axis=2)
    labels, count = ndimage.label(value < GROUND_LEVEL)
    border = numpy.unique(numpy.concatenate((labels[0], labels[-1], labels[:, 0], labels[:, -1])))
    ground = numpy.isin(labels, border[border > 0])
    pockets, pocket_count = ndimage.label(value < POCKET_LEVEL)
    sizes = ndimage.sum(numpy.ones_like(value), pockets, range(1, pocket_count + 1))
    for index, size in enumerate(sizes, start=1):
        if size >= POCKET_AREA:
            ground |= pockets == index
    foreground = ~ground
    core = ndimage.binary_erosion(foreground, iterations=EDGE_WIDTH)
    # the nearest pixel inside gives the color of the edge and of the ground, and the brightness the edge is measured against
    _, nearest = ndimage.distance_transform_edt(~core, return_indices=True)
    inner = rgb[nearest[0], nearest[1]]
    inner_value = inner.max(axis=2)
    edge = foreground & ~core
    alpha = numpy.where(foreground, 1.0, 0.0)
    alpha[edge] = numpy.clip(value[edge] / numpy.maximum(inner_value[edge], 1.0), 0.0, 1.0)
    color = numpy.where(core[..., None], rgb, inner)
    picture = numpy.dstack((color, alpha * 255.0)).round().clip(0, 255).astype(numpy.uint8)
    image = Image.fromarray(picture, "RGBA")
    box = image.getchannel("A").getbbox()
    cropped = image.crop(box)
    side = max(cropped.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(cropped, ((side - cropped.width) // 2, (side - cropped.height) // 2))
    return square


def main():
    for source, name, sizes in SETS:
        picture = cut_out(os.path.join(ART, source))
        for suffix, side in sizes:
            picture.resize((side, side), Image.LANCZOS).save(os.path.join(ICONS, name + suffix + ".png"), optimize=True)


if __name__ == "__main__":
    main()
