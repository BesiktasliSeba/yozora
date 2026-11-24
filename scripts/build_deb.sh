#!/usr/bin/env bash
# one deb for ios 5 and later, made from the ipa that meow built and checked: the bundle sits in /var/jb, which
# rootless jailbreaks register directly and rootful ones receive in /Applications from postinst
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IPA="${YOZORA_DEB_IPA:-${ROOT}/build/ipa/Yozora.ipa}"
OUT_DIR="${YOZORA_DEB_OUT:-${ROOT}/build/ipa}"
STAGE="${YOZORA_DEB_STAGE:-${ROOT}/build/deb-stage}"
CONTROL="${ROOT}/packaging/DEBIAN/control"

[[ -f "${IPA}" ]] || { echo "yozora: ${IPA} is missing, run: meow build ipa" >&2; exit 1; }
PKG_VERSION="$(awk -F': ' '$1 == "Version" { print $2; exit }' "${CONTROL}")"
PKG_ARCH="$(awk -F': ' '$1 == "Architecture" { print $2; exit }' "${CONTROL}")"
if [[ ! "${PKG_VERSION}" =~ ^[0-9][0-9A-Za-z.+:~-]*$ ]]; then
    echo "yozora: invalid package version: ${PKG_VERSION:-<empty>}" >&2
    exit 1
fi
if [[ "${PKG_ARCH}" != "all" ]]; then
    echo "yozora: the universal package must use Architecture: all" >&2
    exit 1
fi

# the package, the plist of the ipa and the name must agree
TMP="$(mktemp -d "${ROOT}/build/deb-check.XXXXXX")"
trap 'rm -rf "${TMP}"' EXIT
unzip -q -o "${IPA}" "Payload/Yozora.app/Info.plist" -d "${TMP}"
APP_VERSION="$(python3 -I -c 'import plistlib,sys; print(plistlib.load(open(sys.argv[1],"rb"))["CFBundleShortVersionString"])' "${TMP}/Payload/Yozora.app/Info.plist")"
if [[ "${APP_VERSION}" != "${PKG_VERSION}" ]]; then
    echo "yozora: the package says ${PKG_VERSION}, the app in the ipa says ${APP_VERSION}" >&2
    exit 1
fi

# a clean stage: a file left by an earlier build would ship in this package
rm -rf "${STAGE}"
mkdir -p "${STAGE}/var/jb/Applications" "${STAGE}/DEBIAN"
unzip -q "${IPA}" -d "${STAGE}/unpacked"
mv "${STAGE}/unpacked/Payload/Yozora.app" "${STAGE}/var/jb/Applications/Yozora.app"
rm -rf "${STAGE}/unpacked"
cp "${CONTROL}" "${ROOT}/packaging/DEBIAN/postinst" "${ROOT}/packaging/DEBIAN/postrm" "${STAGE}/DEBIAN/"

APP="${STAGE}/var/jb/Applications/Yozora.app"
# github refuses a file over 100 MiB and the repository serves this package from there: the pictures are recoded smaller, every pixel stays
if command -v oxipng >/dev/null 2>&1; then
    find "${APP}" -name '*.png' -exec oxipng -q -o6 --zopfli --strip safe {} +
else
    echo "yozora: oxipng is missing, the pictures keep their size and the package can pass the 100 MiB of github" >&2
fi
# the symbol table (some 24 thousand names nothing on the phone reads: a crash report is read with the map kept under the uuid of the binary)
# is dropped too, 667 KB of package. strip takes the signature off, ldid puts it back with the entitlements of the app
TC="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
"${TC}/strip" "${APP}/Yozora" 2> >(grep -v "invalidate the code signature" >&2)
"${TC}/ldid" -S"${ROOT}/tools/yozora-jit.entitlements" "${APP}/Yozora"
find "${APP}" -type d -exec chmod 755 {} +
find "${APP}" -type f -exec chmod 644 {} +
chmod 755 "${APP}/Yozora" "${STAGE}/DEBIAN/postinst" "${STAGE}/DEBIAN/postrm"

mkdir -p "${OUT_DIR}"
OUT="${OUT_DIR}/Yozora-v${PKG_VERSION}.deb"
rm -f "${OUT}"
# gzip members: the dpkg of ios 5 does not read xz or zstd
if ! dpkg-deb -Zgzip --build --root-owner-group "${STAGE}" "${OUT}" >/dev/null 2>&1; then
    dpkg-deb -Zgzip --build "${STAGE}" "${OUT}"
fi
rm -rf "${STAGE}"
echo "built ${OUT}"
