// a test helper for a jailbroken ios 5 or 6 phone: screenshot, launch by bundle id and synthesized touches.
// the browser cannot be driven over ssh otherwise, a process started from a shell has no window and uiopen
// does not start it. usage: yzdev shot FILE | launch BUNDLE_ID | tap X Y | swipe X1 Y1 X2 Y2 MS, coordinates
// in points. build with clang -target armv7-apple-ios5.0 against the ios 5 sdk, -fno-objc-arc, frameworks
// UIKit Foundation CoreGraphics ImageIO, then sign with ldid and tools/yzdev.entitlements
#import <UIKit/UIKit.h>
#import <dlfcn.h>
#import <mach/mach_time.h>

extern CGImageRef UIGetScreenImage(void);

static int shot(const char *path)
{
    CGImageRef image = UIGetScreenImage();
    if (!image) { fprintf(stderr, "UIGetScreenImage returned nothing\n"); return 1; }
    UIImage *picture = [UIImage imageWithCGImage:image];
    NSData *png = UIImagePNGRepresentation(picture);
    if (![png writeToFile:[NSString stringWithUTF8String:path] atomically:NO]) { fprintf(stderr, "write failed\n"); return 1; }
    printf("%zu x %zu\n", CGImageGetWidth(image), CGImageGetHeight(image));
    return 0;
}

static int launch(const char *identifier)
{
    void *handle = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_NOW);
    int (*open_app)(CFStringRef, Boolean) = handle ? dlsym(handle, "SBSLaunchApplicationWithIdentifier") : NULL;
    if (!open_app) { fprintf(stderr, "no SBSLaunchApplicationWithIdentifier\n"); return 1; }
    CFStringRef name = CFStringCreateWithCString(NULL, identifier, kCFStringEncodingUTF8);
    int result = open_app(name, false);
    printf("launch result %d\n", result);
    return result;
}

typedef void *HIDEventRef;
typedef void *HIDClientRef;

// synthesized touches go to backboardd like the real digitizer: one finger, down, a few moves, up
static int touch(double x1, double y1, double x2, double y2, int ms)
{
    void *kit = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW);
    if (!kit) { fprintf(stderr, "no IOKit\n"); return 1; }
    HIDClientRef (*create_client)(CFAllocatorRef) = dlsym(kit, "IOHIDEventSystemClientCreate");
    HIDEventRef (*create_hand)(CFAllocatorRef, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, float, float, float, float, Boolean, Boolean, uint32_t) = dlsym(kit, "IOHIDEventCreateDigitizerEvent");
    HIDEventRef (*create_finger)(CFAllocatorRef, uint64_t, uint32_t, uint32_t, uint32_t, float, float, float, float, float, Boolean, Boolean, uint32_t) = dlsym(kit, "IOHIDEventCreateDigitizerFingerEvent");
    void (*append)(HIDEventRef, HIDEventRef) = dlsym(kit, "IOHIDEventAppendEvent");
    void (*set_integer)(HIDEventRef, uint32_t, int) = dlsym(kit, "IOHIDEventSetIntegerValue");
    void (*set_sender)(HIDEventRef, uint64_t) = dlsym(kit, "IOHIDEventSetSenderID");
    void (*dispatch)(HIDClientRef, HIDEventRef) = dlsym(kit, "IOHIDEventSystemClientDispatchEvent");
    if (!create_client || !create_hand || !create_finger || !append || !set_integer || !set_sender || !dispatch) {
        fprintf(stderr, "missing IOHID symbols %p %p %p %p %p %p %p\n", create_client, create_hand, create_finger, append, set_integer, set_sender, dispatch);
        return 1;
    }
    HIDClientRef client = create_client(kCFAllocatorDefault);
    if (!client) { fprintf(stderr, "no hid client\n"); return 1; }
    CGSize screen = [UIScreen mainScreen].bounds.size;
    int steps = ms > 0 ? ms / 16 : 0;
    // kIOHIDDigitizerEventRange 1, Touch 2, Position 4
    for (int step = -1; step <= steps + 1; ++step) {
        double t = steps ? (step < 0 ? 0 : (step > steps ? 1 : (double)step / steps)) : 0;
        double px = (x1 + (x2 - x1) * t) / screen.width, py = (y1 + (y2 - y1) * t) / screen.height;
        BOOL down = step >= 0 && step <= steps;
        uint32_t mask = step < 0 ? 3 : (step > steps ? 3 : 4);
        if (step == 0) mask = 3;
        uint64_t stamp = mach_absolute_time();
        HIDEventRef hand = create_hand(kCFAllocatorDefault, stamp, 3, 0, 0, mask, 0, px, py, 0, 0, 0, down, down, 0);
        HIDEventRef finger = create_finger(kCFAllocatorDefault, stamp, 1, 2, mask, px, py, 0, down ? 1.0f : 0.0f, 0, down, down, 0);
        set_integer(hand, 0xb0019, 1);
        set_integer(finger, 0xb0019, 1);
        append(hand, finger);
        set_sender(hand, 0x8000000817319372ULL);
        dispatch(client, hand);
        usleep(16000);
    }
    return 0;
}

int main(int argc, char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int status = 2;
    if (argc == 3 && !strcmp(argv[1], "shot")) status = shot(argv[2]);
    else if (argc == 3 && !strcmp(argv[1], "launch")) status = launch(argv[2]);
    else if (argc == 4 && !strcmp(argv[1], "tap")) status = touch(atof(argv[2]), atof(argv[3]), atof(argv[2]), atof(argv[3]), 0);
    else if (argc == 7 && !strcmp(argv[1], "swipe")) status = touch(atof(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5]), atoi(argv[6]));
    else fprintf(stderr, "usage: yzdev shot FILE | launch BUNDLE_ID | tap X Y | swipe X1 Y1 X2 Y2 MS\n");
    [pool drain];
    return status;
}
