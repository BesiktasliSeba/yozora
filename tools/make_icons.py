#!/usr/bin/env python3
import os
import subprocess
import sys

# the suffix of each file name after the name of the icon, and its side in pixels
SIZES = {
    ".png": 57, "@2x.png": 114,
    "-60.png": 60, "-60@2x.png": 120, "-60@3x.png": 180,
    "-72.png": 72, "-72@2x.png": 144,
    "-76.png": 76, "-76@2x.png": 152,
}

# ios 6 springboard draws a prerendered icon as is, so the corners are cut here. the radius stays under the
# ios 7 mask, which keeps no opaque pixel outside it
CORNER_RADIUS = 0.175


def run(command):
    subprocess.run(command, stdout=subprocess.PIPE, check=True)


def rounded_alpha(size):
    radius = size * CORNER_RADIUS
    half = size / 2
    reach = half - radius
    return ("255*clip({r}+0.5-hypot(max(abs(X+0.5-{h})-{k},0),max(abs(Y+0.5-{h})-{k},0)),0,1)"
            .format(r=radius, h=half, k=reach))


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: make_icons.py SOURCE.png OUTPUT_DIRECTORY [NAME]")
    source, output = sys.argv[1:3]
    # the app icon is Icon, an alternate icon the person can choose is named by its own word (Fox-60@2x.png)
    prefix = sys.argv[3] if len(sys.argv) == 4 else "Icon"
    probe = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=width,height", "-of", "csv=p=0", source],
                           stdout=subprocess.PIPE, check=True).stdout.decode().strip()
    width, height = (int(value) for value in probe.split(","))
    if width != height or not 512 <= width <= 4096:
        raise SystemExit("make_icons: the source must be a square between 512 and 4096 pixels")
    os.makedirs(output, exist_ok=True)
    for suffix, size in SIZES.items():
        name = prefix + suffix
        # the mask is built at the final size so the edge stays anti-aliased
        graph = "scale={0}:{0}:flags=lanczos,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='{1}'".format(size, rounded_alpha(size))
        run(["ffmpeg", "-v", "error", "-y", "-i", source, "-vf", graph, "-frames:v", "1", os.path.join(output, name)])


if __name__ == "__main__":
    main()
