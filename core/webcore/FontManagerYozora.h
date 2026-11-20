#pragma once

#include <cstddef>
#include <span>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkFontMgr.h>
#include <skia/core/SkFontStyle.h>
#include <skia/core/SkTypeface.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

namespace WebCore {

sk_sp<SkFontMgr> createYozoraFontManager(const char* directory, char* error, size_t capacity);
bool initializeYozoraFonts(const char* directory, char* error, size_t capacity);
sk_sp<SkFontMgr> yozoraFontManager();
sk_sp<SkTypeface> matchYozoraFont(SkFontMgr&, const char* family, const SkFontStyle&);
sk_sp<SkTypeface> matchYozoraFontCluster(SkFontMgr&, const SkFontStyle&, const char* locale, std::span<const char32_t>);

}
