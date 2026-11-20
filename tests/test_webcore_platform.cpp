#include "config.h"
#include "PlatformLocale.h"
#include <pal/crypto/CryptoDigest.h>
#include <array>
#include <cstdio>
#include <wtf/MainThread.h>
#include <wtf/text/AtomString.h>

// release WTF omits debug assertion helpers, enable C assertions after its headers
#undef NDEBUG
#include <cassert>

using namespace WebCore;
using namespace WTF::StringLiterals;

static void digests()
{
    using Algorithm = PAL::Crypto::CryptoDigest::Algorithm;
    struct Fixture {
        Algorithm algorithm;
        ASCIILiteral expected;
    } fixtures[] = {
        { Algorithm::SHA_1, "A9993E364706816ABA3E25717850C26C9CD0D89D"_s },
        { Algorithm::SHA_256, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"_s },
        { Algorithm::SHA_384, "CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED8086072BA1E7CC2358BAECA134C825A7"_s },
        { Algorithm::SHA_512, "DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F"_s },
    };
    const uint8_t bytes[] = { 'a', 'b', 'c' };
    for (const auto& fixture : fixtures) {
        auto digest = PAL::Crypto::CryptoDigest::create(fixture.algorithm);
        assert(digest);
        digest->addBytes({ bytes, 1 });
        digest->addBytes({ bytes + 1, 2 });
        assert(digest->toHexString() == fixture.expected);
    }
    auto empty = PAL::Crypto::CryptoDigest::create(Algorithm::SHA_256);
    assert(empty && empty->toHexString() == "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"_s);
    std::array<uint8_t, 1000> chunk;
    chunk.fill('a');
    auto streamed = PAL::Crypto::CryptoDigest::create(Algorithm::SHA_256);
    assert(streamed);
    for (unsigned i = 0; i < 1000; ++i)
        streamed->addBytes(chunk);
    assert(streamed->toHexString() == "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0"_s);
}

static void locales()
{
    auto english = Locale::create("en-US"_s);
    auto russian = Locale::create("ru-RU"_s);
    auto arabic = Locale::create("ar-EG"_s);
    assert(english && russian && arabic);
    assert(english->monthLabels().size() == 12 && english->monthLabels()[0] == "January"_s);
    assert(russian->monthLabels().size() == 12 && russian->monthLabels()[0] == String::fromUTF8("января"));
    assert(english->localizedDecimalSeparator() == "."_s);
    assert(russian->localizedDecimalSeparator() == ","_s);
    assert(arabic->localizeNumberCharacters("123.5"_s) == String::fromUTF8("١٢٣٫٥"));
    assert(!english->dateFormat().isEmpty() && !russian->dateFormat().isEmpty());
    assert(!arabic->timeFormat().isEmpty() && !arabic->shortTimeFormat().isEmpty());
    assert(english->timeAMPMLabels().size() == 2);
    assert(english->defaultWritingDirection() == Locale::WritingDirection::LeftToRight);
    assert(arabic->defaultWritingDirection() == Locale::WritingDirection::RightToLeft);
}

int main()
{
    WTF::initializeMainThread();
    digests();
    locales();
    assert(std::puts("platform: SHA vectors, streaming input and ICU locale data passed") >= 0);
    return 0;
}
