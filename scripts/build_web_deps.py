import json
import os
import re
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from fetch_core import ROOT
from fetch_web_deps import fetch_all


def run(args, cwd, env):
    print("yozora: " + shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def mend_ios32_armcap(directory, generated):
    """OpenSSL 3.5 declares OPENSSL_armcap_P with .extern, and the ios32 flavour turns that into .private_extern alone. the files reach the word as a symbol
    minus a label (.word OPENSSL_armcap_P-Lname) and read it through two loads on Apple, which needs a non lazy pointer named OPENSSL_armcap_P in the same file:
    the .comm of the flavour used to define one, and no Mach-O assembler takes an external symbol in a difference. the pointer is added at the end of each
    file that needs it, once"""
    block = ("\n#if __ARM_MAX_ARCH__>=7\n.non_lazy_symbol_pointer\nOPENSSL_armcap_P:\n.indirect_symbol\t_OPENSSL_armcap_P\n.long\t0\n#endif\n")
    mended = 0
    for name in generated:
        path = directory / name
        text = path.read_text()
        if not re.search(r"^\.word\s+OPENSSL_armcap_P-", text, re.M) or re.search(r"^OPENSSL_armcap_P:", text, re.M):
            continue
        path.write_text(text + block)
        mended += 1
    print(f"yozora: gave {mended} generated assembly files their OPENSSL_armcap_P pointer", flush=True)


def mend_ios_thread_local(path):
    """libjxl keeps a little-cms context per thread in a function static thread_local, and clang refuses thread_local for ios before 8 (armv7 builds for 5.0
    and arm64 for 7.0). the context goes under a pthread key instead, and is deleted when its thread ends"""
    text = path.read_text()
    old = """cmsContext GetContext() {
  static thread_local void* context_;
  if (context_ == nullptr) {
    context_ = cmsCreateContext(nullptr, nullptr);
    JXL_DASSERT(context_ != nullptr);

    cmsSetLogErrorHandlerTHR(static_cast<cmsContext>(context_), &ErrorHandler);
  }
  return static_cast<cmsContext>(context_);
}
"""
    new = """void DeleteContext(void* context) { cmsDeleteContext(static_cast<cmsContext>(context)); }

cmsContext GetContext() {
  static pthread_key_t key;
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  pthread_once(&once, [] { pthread_key_create(&key, &DeleteContext); });
  void* context = pthread_getspecific(key);
  if (context == nullptr) {
    context = cmsCreateContext(nullptr, nullptr);
    JXL_DASSERT(context != nullptr);

    cmsSetLogErrorHandlerTHR(static_cast<cmsContext>(context), &ErrorHandler);
    pthread_setspecific(key, context);
  }
  return static_cast<cmsContext>(context);
}
"""
    if text.count(old) != 1 or text.count("\n#include ") < 1:
        raise RuntimeError("libjxl jxl_cms.cc is not the version the thread_local mend expects")
    text = text.replace(old, new).replace("\n#include ", "\n#include <pthread.h>\n#include ", 1)
    path.write_text(text)


def build(mode):
    if mode not in ("host", "ios"):
        raise RuntimeError("usage: build_web_deps.py host|ios")
    jobs = int(os.environ.get("YOZORA_CORE_JOBS", "2"))
    if not 1 <= jobs <= 8:
        raise RuntimeError("YOZORA_CORE_JOBS must be between 1 and 8")
    sources = fetch_all()
    pins = json.loads((ROOT / "core/web-deps.json").read_text())
    deps = Path(os.environ.get("YOZORA_CORE_DEPS", ROOT / "build/core/deps")).resolve()
    output = Path(os.environ.get("YOZORA_WEB_DEPS", deps)).resolve()
    builds = Path(os.environ.get("YOZORA_WEB_DEPS_BUILD", ROOT / "build/core/web-deps")).resolve()
    tools = {}
    for key, default in (("CC", "clang"), ("CXX", "clang++"), ("AR", "llvm-ar"), ("RANLIB", "llvm-ranlib")):
        value = shutil.which(os.environ.get("YOZORA_CORE_" + key, default))
        if not value:
            raise RuntimeError(f"missing {default}, set YOZORA_CORE_{key}")
        tools[key] = value
    tc = os.environ.get("YOZORA_TC")
    if mode == "ios" and not tc:
        raise RuntimeError("set YOZORA_TC for the iOS cctools directory, meow sets it for its steps")
    # YOZORA_WEB_DEPS_ARCHES (armv7, arm64) and YOZORA_WEB_DEPS_ONLY (project names such as
    # zlibng,png,openssl,curl) rebuild part of the set in place. a partial build leaves the record of the
    # whole set's inputs alone and skips the universal archives
    arches = [name for name in os.environ.get("YOZORA_WEB_DEPS_ARCHES", "").split(",") if name]
    only = {name for name in os.environ.get("YOZORA_WEB_DEPS_ONLY", "").split(",") if name}
    if arches and (mode == "host" or not set(arches) <= {"armv7", "arm64"}):
        raise RuntimeError("YOZORA_WEB_DEPS_ARCHES takes armv7 and arm64 in the ios mode")
    partial = bool(arches or only)

    def wanted(name):
        return not only or name in only

    for arch in (("host",) if mode == "host" else tuple(arches or ("armv7", "arm64"))):
        env = os.environ.copy()
        prefix = output / arch / "web"
        icu = deps / arch / "icu"
        runtime = deps / arch / "runtime"
        sdk = deps / "sdk" / f"iPhoneOS-{arch}.sdk"
        build_root = builds / arch
        build_root.mkdir(parents=True, exist_ok=True)
        prefix.mkdir(parents=True, exist_ok=True)
        if not (icu / "lib/libicuuc.a").is_file():
            raise RuntimeError(f"missing {icu}, build core-deps first")
        env.update({"YOZORA_CORE_" + key: value for key, value in tools.items()})
        env.update(YOZORA_SLICE=arch, YOZORA_SLICE_MIN="5.0" if arch == "armv7" else "7.0",
                   YOZORA_SLICE_SDK=str(sdk), YOZORA_SLICE_DEPS=str(deps / arch))
        env["PKG_CONFIG_PATH"] = ""
        env["PKG_CONFIG_LIBDIR"] = os.pathsep.join(str(p / "lib/pkgconfig") for p in (prefix, icu))
        env.update(CC=tools["CC"], CXX=tools["CXX"], AR=tools["AR"], RANLIB=tools["RANLIB"])
        cmake = ["-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_INSTALL_LIBDIR=lib",
                 f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DCMAKE_PREFIX_PATH={prefix};{icu}",
                 f"-DCMAKE_C_COMPILER={tools['CC']}", f"-DCMAKE_CXX_COMPILER={tools['CXX']}",
                 f"-DICU_INCLUDE_DIR={icu}/include",
                 *[f"-DICU_{component.upper()}_LIBRARY_RELEASE={icu}/lib/libicu{component}.a" for component in ("uc", "data", "i18n")],
                 f"-DCMAKE_AR={tools['AR']}", f"-DCMAKE_RANLIB={tools['RANLIB']}",
                 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
        runtime_libs = f'"{runtime}/lib/libc++.a" "{runtime}/lib/libc++abi.a"'
        if arch != "host":
            env["YOZORA_TC"] = tc
            for key in ("CC", "CXX"):
                wrapper = deps / arch / "compiler" / ("cc" if key == "CC" else "cxx")
                if not wrapper.is_file():
                    raise RuntimeError(f"missing compiler wrapper: {wrapper}")
                env[key] = str(wrapper)
            env["CFLAGS"] = "-O2"
            env["CXXFLAGS"] = "-O2"
            cmake += [f"-DCMAKE_TOOLCHAIN_FILE={ROOT}/core/IOSToolchain.cmake",
                      f'-DCMAKE_C_FLAGS=-B "{tc}" -mlinker-version=253',
                      f'-DCMAKE_CXX_FLAGS=-B "{tc}" -mlinker-version=253 -nostdinc++ -isystem "{runtime}/include/c++/v1" -nostdlib++ -faligned-allocation',
                      f"-DCMAKE_CXX_STANDARD_LIBRARIES={runtime_libs}",
                      "-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_CXX_STANDARD_LIBRARIES"]
        else:
            runtime_libs = "-lstdc++ -ldl -pthread"
        identity = dict(arch=arch, sdk=str(sdk.resolve()) if arch != "host" else None,
                        tools=tools, tc=tc, prefix=str(prefix), pins=pins)
        marker = build_root / "inputs.json"
        if not partial:
            if marker.exists() and json.loads(marker.read_text()) != identity:
                raise RuntimeError(f"build inputs changed: choose a new YOZORA_WEB_DEPS_BUILD and YOZORA_WEB_DEPS for {arch}")
            marker.write_text(json.dumps(identity, indent=2) + "\n")

        def project(name, options, source=None, cxx_define=None, targets=()):
            if not wanted(name):
                return
            directory = build_root / name
            if source is None:
                source = sources[name] if name != "sqlite" else ROOT / "core/web-deps"
            flags = [f"{option} {cxx_define}" if cxx_define and option.startswith("-DCMAKE_CXX_FLAGS=") else option for option in cmake]
            run(["cmake", "-S", source, "-B", directory, "-G", "Ninja", *flags, *options], ROOT, env)
            run(["cmake", "--build", directory, "--parallel", jobs, *(["--target", *targets] if targets else [])], ROOT, env)
            run(["cmake", "--install", directory], ROOT, env)

        # zlib-ng in its zlib compatible form with NEON code for inflate, adler32, the window and string
        # compare, picked at run time with sysctl hw.optional.neon, every ios phone with an armv7 slice has
        # NEON. YOZORA_ZLIB=plain keeps zlib 1.3.2 on armv7
        zlib_ng = arch == "armv7" and os.environ.get("YOZORA_ZLIB", "ng") != "plain"
        if zlib_ng:
            project("zlibng", ["-DZLIB_COMPAT=ON", "-DBUILD_TESTING=OFF", "-DWITH_GTEST=OFF", "-DWITH_ARMV8=OFF", "-DINSTALL_UTILS=OFF"])
        else:
            project("zlib", ["-DZLIB_BUILD_SHARED=OFF", "-DZLIB_BUILD_TESTING=OFF"])
        # libpng reads and writes rows through NEON filters and expands palettes with NEON, on armv7 ios it
        # cannot ask the system so it is told to use it
        project("png", ["-DPNG_SHARED=OFF", "-DPNG_FRAMEWORK=OFF", "-DPNG_TESTS=OFF", "-DPNG_TOOLS=OFF", f"-DZLIB_ROOT={prefix}",
                        *(["-DPNG_ARM_NEON=on"] if arch == "armv7" else [])])
        project("jpeg", ["-DENABLE_SHARED=OFF", "-DWITH_TURBOJPEG=OFF", "-DWITH_TOOLS=OFF", "-DWITH_TESTS=OFF"])
        project("webp", [f"-DWEBP_BUILD_{name}=OFF" for name in
                          ("ANIM_UTILS", "CWEBP", "DWEBP", "GIF2WEBP", "IMG2WEBP", "VWEBP", "WEBPINFO", "WEBPMUX", "EXTRAS")])
        project("xml", ["-DLIBXML2_WITH_PROGRAMS=OFF", "-DLIBXML2_WITH_TESTS=OFF", "-DLIBXML2_WITH_ICONV=OFF",
                        "-DLIBXML2_WITH_ICU=ON", "-DLIBXML2_WITH_MODULES=OFF", f"-DICU_ROOT={icu}"])
        project("sqlite", [f"-DYOZORA_SQLITE_SRC={sources['sqlite']}"])
        project("brotli", ["-DBROTLI_BUILD_TOOLS=OFF", "-DBROTLI_DISABLE_TESTS=ON"])
        # libjxl reads highway and little-cms from its third_party, which a source archive leaves empty, so a
        # copy of the source gets the pinned ones. little-cms is built as a separate library the install does
        # not carry, so it is copied next to the others. armv7 runs highway's portable code: the cpu has NEON
        # without VFPv4 and highway picks its ARMv7 target at compile time, which would emit fused
        # multiply-add the phone cannot run
        if wanted("jxl"):
            jxl_source = build_root / "jxl-source"
            shutil.rmtree(jxl_source, ignore_errors=True)
            shutil.copytree(sources["jxl"], jxl_source, symlinks=True)
            for name in ("highway", "lcms"):
                shutil.rmtree(jxl_source / "third_party" / name, ignore_errors=True)
                shutil.copytree(sources[name], jxl_source / "third_party" / name, symlinks=True)
            if arch != "host":
                mend_ios_thread_local(jxl_source / "lib/jxl/cms/jxl_cms.cc")
            # third_party/CMakeLists.txt takes the bundled little-cms only when lcms/.git exists, and looks for a system one otherwise
            (jxl_source / "third_party/lcms/.git").write_text("")
            project("jxl", ["-DBUILD_TESTING=OFF", "-DJPEGXL_STATIC=ON", "-DJPEGXL_ENABLE_TOOLS=OFF", "-DJPEGXL_ENABLE_DOXYGEN=OFF",
                            "-DJPEGXL_ENABLE_MANPAGES=OFF", "-DJPEGXL_ENABLE_BENCHMARK=OFF", "-DJPEGXL_ENABLE_EXAMPLES=OFF", "-DJPEGXL_ENABLE_JNI=OFF",
                            "-DJPEGXL_ENABLE_SJPEG=OFF", "-DJPEGXL_ENABLE_OPENEXR=OFF", "-DJPEGXL_ENABLE_PLUGINS=OFF", "-DJPEGXL_ENABLE_JPEGLI=OFF",
                            "-DJPEGXL_ENABLE_VIEWERS=OFF", "-DJPEGXL_ENABLE_FUZZERS=OFF", "-DJPEGXL_ENABLE_SKCMS=OFF", "-DJPEGXL_BUNDLE_LIBPNG=OFF",
                            "-DJPEGXL_FORCE_SYSTEM_BROTLI=ON",
                            "-DHWY_ENABLE_EXAMPLES=OFF", "-DHWY_ENABLE_CONTRIB=OFF", "-DHWY_ENABLE_TESTS=OFF",
                            # pthreads live in libSystem on ios and FindThreads cannot link a test for them
                            # while cross compiling, and its feature checks (atomics, linker flags) cannot
                            # link a test program without crt0, so compiling them is enough
                            *(["-DCMAKE_HAVE_LIBC_PTHREAD=1", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"] if arch != "host" else [])],
                    source=jxl_source, cxx_define="-DHWY_COMPILE_ONLY_EMU128=1" if arch == "armv7" else None,
                    # highway has an executable target of its own that the ios link settings of these builds cannot make
                    targets=("jxl", "jxl_cms", "jxl_threads"))
            shutil.copyfile(build_root / "jxl/third_party/liblcms2.a", prefix / "lib/liblcms2.a")
        # the 16.5 sdk declares clock_gettime, ios 7 to 9 do not export it and dyld would refuse the app, so nghttp2 keeps gettimeofday
        nghttp2_time = [] if arch == "host" else ["-DHAVE_CLOCK_GETTIME=0"]
        project("nghttp2", ["-DENABLE_LIB_ONLY=ON", "-DBUILD_STATIC_LIBS=ON", "-DBUILD_TESTING=OFF", "-DENABLE_DOC=OFF", *nghttp2_time])
        directory = build_root / "openssl"
        if wanted("openssl"):
            directory.mkdir(exist_ok=True)
        openssl_env = env.copy()
        target = "linux-aarch64" if os.uname().machine == "aarch64" else "linux-x86_64"
        if arch != "host":
            target = "ios-cross" if arch == "armv7" else "ios64-cross"
            sdk_root = directory / "SDKs"
            sdk_root.mkdir(exist_ok=True)
            link = sdk_root / "iPhoneOS.sdk"
            if not link.is_symlink():
                link.symlink_to(sdk)
            openssl_env.update(CROSS_TOP=str(directory), CROSS_SDK="iPhoneOS.sdk", CROSS_COMPILE="")
        # armv7 runs OpenSSL's armv4 and NEON code for AES, ChaCha20, Poly1305, GHASH, SHA and the big number
        # multiply, picked at run time (armcap.c probes the cpu with SIGILL where the system gives no feature
        # word). YOZORA_OPENSSL_ASM=0 keeps armv7 on the C code
        asm = arch == "armv7" and os.environ.get("YOZORA_OPENSSL_ASM", "1") != "0"
        options = [target, "no-shared", "no-dso", *([] if asm else ["no-asm"]), "no-tests", "no-engine", "no-module",
                   f"--prefix={prefix}", "--libdir=lib", "--openssldir=/etc/ssl"]
        if arch == "armv7":
            # iOS 5 has no libatomic entry points for OpenSSL's generic 64-bit atomics
            options += ["-DBROKEN_CLANG_ATOMICS"]
        if wanted("openssl"):
            run(["perl", sources["openssl"] / "Configure", *options], directory, openssl_env)
            if asm:
                # the files of the ios32 flavour are made first and mended before they are assembled
                generated = re.findall(r"^(\S+\.S):", (directory / "Makefile").read_text(), re.M)
                run(["make", f"-j{jobs}", *generated], directory, openssl_env)
                mend_ios32_armcap(directory, generated)
            run(["make", f"-j{jobs}", "build_libs"], directory, openssl_env)
            run(["make", "install_dev"], directory, openssl_env)
        directory = build_root / "psl"
        if wanted("psl"):
            directory.mkdir(exist_ok=True)
        psl_env = env.copy()
        psl_env["LIBS"] = f"-L{runtime}/lib -lc++ -lc++abi" if arch != "host" else runtime_libs
        options = [f"--prefix={prefix}", "--disable-shared", "--enable-static", "--enable-runtime=libicu",
                   "--disable-nls", "--disable-gtk-doc", "--disable-gtk-doc-html", "--disable-man"]
        if arch != "host":
            options += [f"--host={'arm' if arch == 'armv7' else 'aarch64'}-apple-darwin"]
        if wanted("psl"):
            run([sources["psl"] / "configure", *options], directory, psl_env)
            for subdir in ("src", "include"):
                run(["make", "-C", subdir, f"-j{jobs}"], directory, psl_env)
                run(["make", "-C", subdir, "install"], directory, psl_env)
            (prefix / "lib/pkgconfig").mkdir(exist_ok=True)
            shutil.copyfile(directory / "libpsl.pc", prefix / "lib/pkgconfig/libpsl.pc")
        project("curl", ["-DBUILD_CURL_EXE=OFF", "-DBUILD_STATIC_LIBS=ON", "-DBUILD_TESTING=OFF",
                         "-DBUILD_LIBCURL_DOCS=OFF", "-DBUILD_MISC_DOCS=OFF", "-DENABLE_CURL_MANUAL=OFF",
                         "-DCURL_USE_OPENSSL=ON", f"-DOPENSSL_ROOT_DIR={prefix}", "-DOPENSSL_USE_STATIC_LIBS=ON",
                         "-DCURL_USE_LIBPSL=ON", "-DCURL_USE_LIBSSH2=OFF", "-DCURL_USE_GSSAPI=OFF", "-DUSE_LIBIDN2=OFF",
                         "-DUSE_NGHTTP2=ON", "-DCURL_ZLIB=ON", "-DCURL_BROTLI=ON", "-DCURL_ZSTD=OFF",
                         "-DCURL_CA_BUNDLE=none", "-DCURL_CA_PATH=none", "-DCURL_CA_FALLBACK=OFF",
                         *(["-DHAVE_BUILTIN_AVAILABLE=0"] if arch != "host" else []),
                         "-DCURL_USE_PKGCONFIG=ON", "-DCURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON",
                         *[f"-DCURL_DISABLE_{p}=ON" for p in ("DICT", "FTP", "GOPHER", "IMAP", "LDAP", "MQTT", "POP3", "RTSP", "SMTP", "TELNET", "TFTP")]])
        if not partial:
            (prefix / "yozora-inputs.json").write_text(json.dumps(identity, indent=2) + "\n")
        print(f"yozora: installed {arch} web dependencies in {prefix}", flush=True)
    if partial:
        return
    if mode == "ios":
        universal = builds / "universal"
        universal.mkdir(exist_ok=True)
        for path in sorted((output / "armv7/web/lib").glob("*.a")):
            result = universal / path.name
            run([Path(tc) / "lipo", "-create", path, output / "arm64/web/lib" / path.name, "-output", result], ROOT, os.environ.copy())
            run([Path(tc) / "lipo", "-info", result], ROOT, os.environ.copy())
            run([Path(tc) / "lipo", result, "-verify_arch", "armv7", "arm64"], ROOT, os.environ.copy())

    from test_web_deps import test
    test(mode)


if __name__ == "__main__":
    try:
        build(sys.argv[1] if len(sys.argv) == 2 else "")
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"yozora: {error}") from error
