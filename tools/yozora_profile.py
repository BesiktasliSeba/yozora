#!/usr/bin/env python3
"""reads a profile written by yozora-render (YOZORA_RENDER_PROFILE) and prints where the time went.

usage: yozora_profile.py PROFILE UNSTRIPPED_BINARY [--top N] [--thread NAME] [--callers FUNCTION] [--within FUNCTION] [--depth N]
"""
import argparse
import bisect
import collections
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("profile")
parser.add_argument("binary")
parser.add_argument("--top", type=int, default=40)
parser.add_argument("--callers", help="substring of a function: print what calls it and what it spends time in")
parser.add_argument("--within", help="substring of a function: only samples that have it on the stack, to see what one phase spends")
parser.add_argument("--depth", type=int, default=6)
parser.add_argument("--core", action="store_true", help="only samples of the core thread")
parser.add_argument("--nm", default="llvm-nm")
parser.add_argument("--demangler", default="llvm-cxxfilt")
args = parser.parse_args()

slide = 0
interval_us = 1000
samples = []
for line in open(args.profile):
    fields = line.split()
    if fields[0] == "I":
        slide = int(fields[1], 16)
        interval_us = int(fields[2])
    elif fields[0] == "S":
        samples.append((fields[1], [int(value, 16) for value in fields[2:]]))

# the leaf is the program counter, every other frame is a return address that points after the call
def lookup_address(index, address):
    address &= ~1
    return address if index == 0 else address - 2

# llvm-symbolizer finds no thumb functions in these binaries, the symbol table is read directly
nm = subprocess.run([args.nm, "-n", "--defined-only", args.binary], capture_output=True, text=True, check=True)
table_addresses = []
table_names = []
for line in nm.stdout.splitlines():
    fields = line.split(None, 2)
    if len(fields) == 3 and fields[1] in "tTsS":
        table_addresses.append(int(fields[0], 16))
        table_names.append(fields[2][1:] if fields[2].startswith("_") else fields[2])
cache = {}

def demangled(mangled):
    if mangled not in cache:
        result = subprocess.run([args.demangler], input=mangled + "\n", capture_output=True, text=True)
        cache[mangled] = result.stdout.strip() or mangled
    return cache[mangled]

names = {}
for address in {lookup_address(i, a) - slide for _, frames in samples for i, a in enumerate(frames)}:
    position = bisect.bisect_right(table_addresses, address) - 1
    inside = 0 <= position and address - table_addresses[position] < 0x40000 and address < table_addresses[-1]
    names[address] = demangled(table_names[position]) if inside else "[outside]"

def symbol(index, address):
    return names[lookup_address(index, address) - slide]

def shorten(name):
    return name if len(name) < 110 else name[:107] + "..."

threads = collections.Counter(thread for thread, _ in samples)
if args.core:
    samples = [(t, f) for t, f in samples if any("thread_main" in symbol(i, a) for i, a in enumerate(f))]
if args.within:
    samples = [(t, f) for t, f in samples if any(args.within in symbol(i, a) for i, a in enumerate(f))]
total = len(samples)
print("%d samples, %.0f ms of running threads, interval %d us" % (total, total * interval_us / 1000.0, interval_us))
for thread, count in threads.most_common(8):
    print("  thread %s: %d samples" % (thread, count))

def table(title, counter):
    print("\n" + title)
    for name, count in counter.most_common(args.top):
        print("%6d %5.1f%%  %s" % (count, 100.0 * count / total, shorten(name)))

# code the binary does not hold (system libraries, jit code) is named by the nearest frame that is in it
def leaf(frames):
    first = symbol(0, frames[0])
    if first != "[outside]":
        return first
    for index in range(1, len(frames)):
        name = symbol(index, frames[index])
        if name != "[outside]":
            return "[outside, called from] " + name
    return first

own = collections.Counter(leaf(frames) for _, frames in samples if frames)
table("self time", own)
inclusive = collections.Counter()
for _, frames in samples:
    seen = set()
    for index, address in enumerate(frames):
        name = symbol(index, address)
        if name not in seen:
            seen.add(name)
            inclusive[name] += 1
table("inclusive", inclusive)

# each sample goes to the category of its frame nearest the leaf that names one, so a collection in the middle
# of a script counts as collection
CATEGORIES = [
    ("gc", ["JSC::Heap::", "JSC::SlotVisitor", "JSC::MarkedBlock", "JSC::IncrementalSweeper", "JSC::WeakBlock", "JSC::BlockDirectory", "JSC::LocalAllocator"]),
    ("js compile", ["JSC::Parser<", "JSC::Lexer<", "JSC::BytecodeGenerator", "JSC::JIT::", "JSC::JITCompilation", "JSC::LinkBuffer", "JSC::UnlinkedCodeBlock", "JSC::CodeCache", "JSC::generateUnlinked", "JSC::ProgramExecutable", "JSC::ModuleProgramExecutable", "JSC::FunctionExecutable", "JSC::EvalExecutable", "JSC::ScriptExecutable"]),
    ("js run", ["vmEntryToJavaScript", "llint_entry", "operation", "llint_slow_path", "JSC::JSObject::", "JSC::JSFunction", "JSC::Structure"]),
    ("style", ["Style::", "SelectorChecker", "CSSParser", "CSSSelector", "StyleScope", "RuleSet", "StyleSheetContents", "CSSStyleSheet"]),
    ("layout", ["RenderBlock", "RenderFlexibleBox", "RenderGrid", "RenderTable", "LayoutIfNeeded", "FrameView::layout", "LocalFrameView::layout", "LayoutState", "Layout::", "InlineContent", "RenderText", "LineLayout", "TextUtil", "ComplexTextController", "FontCascade"]),
    ("paint", ["paintContents", "RenderLayer::paint", "GraphicsContextSkia", "Skia", "SkCanvas", "SkRasterPipeline", "SkBitmapDevice", "SkScan", "SkBlitter", "neon::", "RenderBox::paint", "RenderObject::paint", "InlineContentPainter", "BoxPainter", "TextPainter"]),
    ("html parse", ["HTMLDocumentParser", "HTMLTokenizer", "HTMLTreeBuilder", "HTMLConstructionSite", "HTMLPreloadScanner", "TextCodec", "SegmentedString", "DocumentWriter"]),
    ("image", ["ImageDecoder", "PNGImageDecoder", "JPEGImageDecoder", "png_", "jpeg_", "inflate", "BitmapImage", "ImageFrame", "ImageBackingStore", "WebP", "GIFImage"]),
    ("network", ["Curl", "curl", "SSL_", "ssl3", "tls", "AES", "gcm_", "bn_", "nghttp2", "Curl_", "OPENSSL", "ossl_", "EVP_", "ec_", "BIO_", "YozoraNetworkJob", "YozoraLoader", "cf_socket"]),
    ("dom", ["WebCore::Node::", "WebCore::Element::", "ContainerNode", "WebCore::Document::", "MutationObserver", "WebCore::Event", "WebCore::JS"]),
]

def category(frames):
    for index, address in enumerate(frames):
        name = symbol(index, address)
        for label, patterns in CATEGORIES:
            if any(pattern in name for pattern in patterns):
                return label
    return "other"

print("\ncategories (all threads)")
for label, count in collections.Counter(category(frames) for _, frames in samples).most_common():
    print("%6d %5.1f%%  %s" % (count, 100.0 * count / total, label))
core_samples = [frames for _, frames in samples if any("thread_main" in symbol(i, a) for i, a in enumerate(frames))]
if core_samples:
    print("\ncategories (core thread, %d samples)" % len(core_samples))
    for label, count in collections.Counter(category(frames) for frames in core_samples).most_common():
        print("%6d %5.1f%%  %s" % (count, 100.0 * count / len(core_samples), label))

if args.callers:
    below = collections.Counter()
    above = collections.Counter()
    for _, frames in samples:
        symbols = [symbol(i, a) for i, a in enumerate(frames)]
        for index, name in enumerate(symbols):
            if args.callers in name:
                above[" < ".join(shorten(s)[:60] for s in symbols[index + 1:index + 1 + args.depth])] += 1
                below[" > ".join(shorten(s)[:60] for s in reversed(symbols[max(0, index - args.depth + 1):index]))] += 1
                break
    print("\ncallers of %s" % args.callers)
    for name, count in above.most_common(args.top):
        print("%6d  %s" % (count, name))
    print("\nwhere %s spends its time" % args.callers)
    for name, count in below.most_common(args.top):
        print("%6d  %s" % (count, name))
