"""writes the launch images of every iPhone and iPad screen from iOS 5 to 16 into app/launch and prints the
UILaunchImages entries for app/Info.plist. without a launch image of its size a screen runs the app letterboxed.
the image is the flat violet of the splash of the app, so the start does not flash white and the system image and the splash of the app are one picture.
needs Pillow (PYTHON with Pillow, for example a venv)"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("make_launch_images: Pillow is required")

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
# the color of the splash of the app, and of the launch storyboard of iOS 9 and later
LAUNCH_COLOR = (88, 30, 118)

# name, width and height in points, scales, first iOS that has the screen, iPad
SCREENS = [
    ("320x480", 320, 480, (2,), "7.0", False),
    ("320x568", 320, 568, (2,), "7.0", False),
    ("375x667", 375, 667, (2,), "8.0", False),
    ("414x736", 414, 736, (3,), "8.0", False),
    ("375x812", 375, 812, (3,), "11.0", False),
    ("414x896", 414, 896, (2, 3), "12.0", False),
    ("390x844", 390, 844, (3,), "14.0", False),
    ("428x926", 428, 926, (3,), "14.0", False),
    ("393x852", 393, 852, (3,), "16.0", False),
    ("430x932", 430, 932, (3,), "16.0", False),
    ("768x1024", 768, 1024, (1, 2), "7.0", True),
    ("834x1112", 834, 1112, (2,), "10.0", True),
    ("1024x1366", 1024, 1366, (2,), "9.0", True),
    ("834x1194", 834, 1194, (2,), "12.0", True),
    ("810x1080", 810, 1080, (2,), "13.0", True),
    ("820x1180", 820, 1180, (2,), "14.0", True),
    ("744x1133", 744, 1133, (2,), "15.0", True),
]

# iOS 5 and 6 look for these names; their images leave out the 20 point status bar
LEGACY = [
    ("Default.png", 320, 480, 1, False),
    ("Default@2x.png", 320, 480, 2, False),
    ("Default-568h@2x.png", 320, 568, 2, False),
    ("Default-Portrait~ipad.png", 768, 1024, 1, True),
    ("Default-Portrait@2x~ipad.png", 768, 1024, 2, True),
    ("Default-Landscape~ipad.png", 1024, 768, 1, True),
    ("Default-Landscape@2x~ipad.png", 1024, 768, 2, True),
]


# a flat ground of the color the app splash starts on (YZSplashController launchColor) with nothing on it: the
# system shows the image until the app is up and the sticker and name wobble in on the same color, so the
# start reads as one splash and not two
def picture(width, height, scale):
    return Image.new("RGB", (width * scale, height * scale), LAUNCH_COLOR)


def main():
    root = os.path.join(ROOT, "app", "launch")
    os.makedirs(root, exist_ok=True)
    entries = []
    for name, width, height, scales, minimum, ipad in SCREENS:
        orientations = (("Portrait", width, height), ("Landscape", height, width)) if ipad else (("Portrait", width, height),)
        for orientation, w, h in orientations:
            base = f"LaunchImage-{orientation}-{w}x{h}"
            for scale in scales:
                suffix = "" if scale == 1 else f"@{scale}x"
                picture(w, h, scale).save(os.path.join(root, f"{base}{suffix}.png"), optimize=True)
            # the size is given the way the portrait screen is, the orientation tells which way it is shown
            entries.append((minimum, base, orientation, width, height))
    for name, width, height, scale, ipad in LEGACY:
        picture(width, height - 20, scale).save(os.path.join(root, name), optimize=True)
    for minimum, base, orientation, width, height in entries:
        print(f"        <dict><key>UILaunchImageMinimumOSVersion</key><string>{minimum}</string><key>UILaunchImageName</key><string>{base}</string>"
              f"<key>UILaunchImageOrientation</key><string>{orientation}</string><key>UILaunchImageSize</key><string>{{{width}, {height}}}</string></dict>")


if __name__ == "__main__":
    sys.exit(main())
