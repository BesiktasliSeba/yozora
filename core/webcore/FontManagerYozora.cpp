#include "config.h"
#include "FontManagerYozora.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <unicode/uchar.h>
#include <unicode/ustring.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkData.h>
#include <skia/core/SkFontScanner.h>
#include <skia/core/SkStream.h>
#include <skia/core/SkString.h>
#include <skia/ports/SkFontMgr_data.h>
#include <skia/ports/SkFontScanner_FreeType.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

namespace WebCore {

static constexpr size_t maximum_files = 64;
static constexpr size_t maximum_file_bytes = 32 * 1024 * 1024;
static constexpr size_t maximum_catalog_bytes = 64 * 1024 * 1024;
static constexpr size_t maximum_faces = 256;
static constexpr size_t maximum_cluster = 256;
static constexpr const char* emoji_family = "Apple Color Emoji";

static sk_sp<SkFontMgr> font_failure(char* error, size_t capacity, const char* operation)
{
    if (error && capacity)
        snprintf(error, capacity, "%s", operation);
    return nullptr;
}

static bool font_filename(const char* name)
{
    auto* suffix = strrchr(name, '.');
    return name[0] != '.' && suffix && (!strcasecmp(suffix, ".ttf") || !strcasecmp(suffix, ".ttc") || !strcasecmp(suffix, ".otf"));
}

sk_sp<SkFontMgr> createYozoraFontManager(const char* directory, char* error, size_t capacity)
{
    if (error && capacity)
        error[0] = 0;
    if (!directory || !*directory || strnlen(directory, PATH_MAX) == PATH_MAX)
        return font_failure(error, capacity, "invalid font directory");
    DIR* stream = opendir(directory);
    if (!stream)
        return font_failure(error, capacity, "cannot open font directory");
    std::vector<std::string> names;
    bool too_many = false;
    size_t entries = 0;
    errno = 0;
    while (auto* entry = readdir(stream)) {
        if (++entries > 4096) {
            too_many = true;
            break;
        }
        if (!font_filename(entry->d_name))
            continue;
        if (names.size() == maximum_files) {
            too_many = true;
            break;
        }
        names.emplace_back(entry->d_name);
    }
    int read_error = errno;
    int close_error = closedir(stream);
    if (too_many || read_error || close_error)
        return font_failure(error, capacity, too_many ? "font file limit exceeded" : "cannot read font directory");
    if (names.empty())
        return font_failure(error, capacity, "font directory is empty");
    std::sort(names.begin(), names.end());
    std::vector<sk_sp<SkData>> data;
    auto scanner = SkFontScanner_Make_FreeType();
    size_t total_bytes = 0;
    size_t total_faces = 0;
    for (const auto& name : names) {
        std::string path = std::string(directory) + "/" + name;
        if (path.size() >= PATH_MAX)
            return font_failure(error, capacity, "font path limit exceeded");
        // nonblocking open prevents a font-shaped FIFO from blocking startup
        int descriptor;
        do {
            descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0)
            return font_failure(error, capacity, "cannot open font file");
        struct stat status;
        if (fstat(descriptor, &status) || !S_ISREG(status.st_mode) || status.st_size <= 0
            || static_cast<uint64_t>(status.st_size) > maximum_file_bytes
            || static_cast<uint64_t>(status.st_size) > maximum_catalog_bytes - total_bytes) {
            int closed = close(descriptor);
            return font_failure(error, capacity, closed ? "cannot close font file" : "font file type or size is invalid");
        }
        auto bytes = SkData::MakeUninitialized(static_cast<size_t>(status.st_size));
        size_t offset = 0;
        while (bytes && offset < bytes->size()) {
            auto count = read(descriptor, static_cast<uint8_t*>(bytes->writable_data()) + offset, bytes->size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                break;
            offset += static_cast<size_t>(count);
        }
        int closed = close(descriptor);
        if (closed || !bytes || offset != bytes->size())
            return font_failure(error, capacity, closed ? "cannot close font file" : "cannot read complete font file");
        // owned bytes cannot fault if an installed font is replaced or truncated
        SkMemoryStream font_stream(bytes);
        int faces = 0;
        if (!scanner->scanFile(&font_stream, &faces) || faces <= 0 || static_cast<size_t>(faces) > maximum_faces - total_faces)
            return font_failure(error, capacity, "font face count or format is invalid");
        for (int face = 0; face < faces; ++face) {
            int instances = 0;
            if (!scanner->scanFace(&font_stream, face, &instances) || instances < 0
                || static_cast<size_t>(instances) + 1 > maximum_faces - total_faces)
                return font_failure(error, capacity, "font instance limit exceeded");
            total_faces += static_cast<size_t>(instances) + 1;
            for (int instance = 0; instance <= instances; ++instance) {
                SkString family;
                SkFontStyle style;
                bool fixed_pitch = false;
                if (!scanner->scanInstance(&font_stream, face, instance, &family, &style, &fixed_pitch, nullptr, nullptr)
                    || family.isEmpty() || family.size() > 1024)
                    return font_failure(error, capacity, "font instance metadata is invalid");
            }
        }
        total_bytes += bytes->size();
        data.push_back(std::move(bytes));
    }
    auto manager = SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(data.data(), data.size()));
    if (!manager || !manager->countFamilies())
        return font_failure(error, capacity, "font catalog has no families");
    // skia's empty-family sentinel must not turn invalid fonts into success
    for (int i = 0; i < manager->countFamilies(); ++i) {
        auto styles = manager->createStyleSet(i);
        for (int j = 0; j < styles->count(); ++j) {
            auto face = styles->createTypeface(j);
            if (!face || !face->countGlyphs())
                return font_failure(error, capacity, "font catalog has an empty face");
        }
    }
    return manager;
}

static bool equal_family(const char* a, const char* b)
{
    if (strnlen(a, 1025) > 1024 || strnlen(b, 1025) > 1024)
        return false;
    std::array<UChar, 1024> left;
    std::array<UChar, 1024> right;
    int32_t left_length = 0;
    int32_t right_length = 0;
    UErrorCode status = U_ZERO_ERROR;
    u_strFromUTF8(left.data(), left.size(), &left_length, a, -1, &status);
    u_strFromUTF8(right.data(), right.size(), &right_length, b, -1, &status);
    if (U_FAILURE(status))
        return false;
    return !u_strCaseCompare(left.data(), left_length, right.data(), right_length, U_FOLD_CASE_DEFAULT, &status) && U_SUCCESS(status);
}

static sk_sp<SkTypeface> exact_family(SkFontMgr& manager, const char* family, const SkFontStyle& style)
{
    for (int i = 0; i < manager.countFamilies(); ++i) {
        SkString name;
        manager.getFamilyName(i, &name);
        if (equal_family(name.c_str(), family))
            return manager.createStyleSet(i)->matchStyle(style);
    }
    return nullptr;
}

sk_sp<SkTypeface> matchYozoraFont(SkFontMgr& manager, const char* family, const SkFontStyle& style)
{
    if (!family)
        family = "sans-serif";
    if (auto face = exact_family(manager, family, style))
        return face;
    const char* const sans[] = { "Helvetica", "Arial", "Roboto", "Noto Sans", "DejaVu Sans" };
    const char* const serif[] = { "Times New Roman", "Times", "Noto Serif", "DejaVu Serif", "Amiri" };
    const char* const mono[] = { "Courier New", "Courier", "Menlo", "Roboto Mono", "Noto Sans Mono", "DejaVu Sans Mono" };
    std::span<const char* const> candidates;
    if (!strcasecmp(family, "sans-serif") || !strcasecmp(family, "system-ui") || !strcasecmp(family, "ui-sans-serif")
        || !strcasecmp(family, "-apple-system") || !strcasecmp(family, "-apple-system-font") || !strcasecmp(family, "-webkit-system-font")
        || !strcasecmp(family, "-webkit-system-ui") || !strcasecmp(family, "system-font"))
        candidates = sans;
    else if (!strcasecmp(family, "serif") || !strcasecmp(family, "ui-serif") || !strcasecmp(family, "cursive") || !strcasecmp(family, "fantasy") || !strcasecmp(family, "math"))
        candidates = serif;
    else if (!strcasecmp(family, "monospace") || !strcasecmp(family, "ui-monospace"))
        candidates = mono;
    else
        return nullptr;
    for (auto* name : candidates) {
        if (auto face = exact_family(manager, name, style))
            return face;
    }
    if (candidates.data() == mono) {
        for (int i = 0; i < manager.countFamilies(); ++i) {
            auto face = manager.createStyleSet(i)->matchStyle(style);
            if (face && face->isFixedPitch())
                return face;
        }
        return nullptr;
    }
    return manager.legacyMakeTypeface(nullptr, style);
}

sk_sp<SkTypeface> matchYozoraFontCluster(SkFontMgr& manager, const SkFontStyle& style, const char* locale, std::span<const char32_t> cluster)
{
    if (cluster.empty() || cluster.size() > maximum_cluster)
        return nullptr;
    std::array<SkUnichar, maximum_cluster> characters;
    size_t count = 0;
    for (auto character : cluster) {
        if (character > 0x10ffff || (character >= 0xd800 && character <= 0xdfff))
            return nullptr;
        if (!u_hasBinaryProperty(static_cast<UChar32>(character), UCHAR_DEFAULT_IGNORABLE_CODE_POINT))
            characters[count++] = static_cast<SkUnichar>(character);
    }
    if (!count)
        return nullptr;
    auto covers_cluster = [&](const sk_sp<SkTypeface>& face) {
        if (!face)
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (!face->unicharToGlyph(characters[i]))
                return false;
        }
        return true;
    };
    // language-specific CJK families avoid choosing a regional Han shape by file order
    const char* preferred = nullptr;
    if (locale) {
        if (!strncasecmp(locale, "ja", 2))
            preferred = "Noto Sans CJK JP";
        else if (!strncasecmp(locale, "ko", 2))
            preferred = "Noto Sans CJK KR";
        else if (!strncasecmp(locale, "zh-TW", 5) || !strncasecmp(locale, "zh-HK", 5) || !strncasecmp(locale, "zh-Hant", 7))
            preferred = "Noto Sans CJK TC";
        else if (!strncasecmp(locale, "zh", 2))
            preferred = "Noto Sans CJK SC";
        else if (!strncasecmp(locale, "und-Zsye", 8))
            preferred = emoji_family;
    }
    if (preferred) {
        auto face = exact_family(manager, preferred, style);
        if (covers_cluster(face))
            return face;
    }
    for (int i = 0; i < manager.countFamilies(); ++i) {
        // the color font sorts first and covers text symbols such as the check mark, which would turn into pictures
        SkString name;
        manager.getFamilyName(i, &name);
        if (equal_family(name.c_str(), emoji_family))
            continue;
        auto face = manager.createStyleSet(i)->matchStyle(style);
        if (covers_cluster(face))
            return face;
    }
    return nullptr;
}

}
