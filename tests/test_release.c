#include <stdio.h>
#include <string.h>

#include "yozora_release.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_release: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static int compare(const char *a, const char *b)
{
    int result = 99;
    return yozora_version_compare(a, b, &result) ? result : 42;
}

static const char *const release_json =
    "{\"url\":\"https://api.github.com/x\",\"author\":{\"login\":\"sqmrak\",\"id\":1,\"site_admin\":false},"
    "\"tag_name\":\"v1.5.0-stable\",\"draft\":false,\"prerelease\":false,"
    "\"assets\":[{\"name\":\"Yozora-v1.5.0-stable.ipa.sha256\",\"size\":90,\"uploader\":{\"id\":2},"
    "\"browser_download_url\":\"https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-v1.5.0-stable.ipa.sha256\"},"
    "{\"name\":\"Yozora-v1.5.0-stable.ipa\",\"size\":104857600,"
    "\"browser_download_url\":\"https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-v1.5.0-stable.ipa\"},"
    "{\"name\":\"Yozora-armv7.ipa\",\"size\":5,\"browser_download_url\":\"https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-armv7.ipa\"}],"
    "\"body\":\"## What's new\\r\\n\\r\\n- **faster** pages\\r\\n- [notes](https://x.y/z) and `code`\\r\\n* \\u0443\\u043b\\u0443\\u0447\\u0448\\u0435\\u043d\\u0438\\u044f \\ud83e\\udd8a\\r\\n\\r\\n\\r\\n\\r\\nend\"}";

static const char *const atom_feed =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><feed xmlns=\"http://www.w3.org/2005/Atom\"><id>tag:github.com,2008:Repository/1/v1</id>"
    "<entry><id>tag:github.com,2008:Repository/1/v1.5.0-stable</id>"
    "<link rel=\"alternate\" type=\"text/html\" href=\"https://github.com/sqmrak/yozora/releases/tag/v1.5.0-stable\"/>"
    "<title>Yozora 1.5.0</title><content type=\"html\">&lt;h2&gt;What&amp;#39;s new&lt;/h2&gt;\n&lt;ul&gt;\n&lt;li&gt;faster &amp;amp; smaller&lt;/li&gt;\n"
    "&lt;li&gt;second&lt;/li&gt;\n&lt;/ul&gt;&lt;p&gt;bye&lt;/p&gt;</content></entry>"
    "<entry><link rel=\"alternate\" href=\"https://github.com/sqmrak/yozora/releases/tag/v1.4.0-stable\"/><content type=\"html\">old</content></entry></feed>";

int main(void)
{
    CHECK(compare("1.4.0-stable", "v1.5.0-stable") == -1);
    CHECK(compare("v1.5.0", "1.4.0-stable") == 1);
    CHECK(compare("1.4.0-stable", "v1.4.0-developer") == 0);
    CHECK(compare("1.10", "1.9") == 1);
    CHECK(compare("1.4", "1.4.0.0") == 0);
    CHECK(compare("2", "1.99.99") == 1);
    CHECK(compare("Yozora 1.4.1", "1.4.0") == 1);
    CHECK(compare("1.4.0.1", "1.4.0.2") == -1);
    CHECK(compare("stable", "1.0") == 42);
    CHECK(compare("", "1.0") == 42);
    CHECK(compare("1.0.0.0.9", "1.0.0.0.1") == 0);
    CHECK(!yozora_version_compare(NULL, "1", &(int){ 0 }));

    yozora_release release;
    CHECK(yozora_release_parse_json(release_json, strlen(release_json), &release) == YOZORA_RELEASE_OK);
    CHECK(!strcmp(release.tag, "v1.5.0-stable"));
    CHECK(!strcmp(release.ipa_url, "https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-v1.5.0-stable.ipa"));
    CHECK(!strcmp(release.sha_url, "https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-v1.5.0-stable.ipa.sha256"));
    CHECK(release.ipa_size == 104857600u);
    CHECK(!strcmp(release.notes, "What's new\n\n\xe2\x80\xa2 faster pages\n\xe2\x80\xa2 notes and code\n\xe2\x80\xa2 \xd1\x83\xd0\xbb\xd1\x83\xd1\x87\xd1\x88\xd0\xb5\xd0\xbd\xd0\xb8\xd1\x8f \xf0\x9f\xa6\x8a\n\nend"));

    char json[2048];
    snprintf(json, sizeof json, "{\"tag_name\":\"v2\",\"assets\":[{\"name\":\"Yozora-v2.ipa\",\"size\":9,\"browser_download_url\":\"https://evil.example/Yozora-v2.ipa\"}]}");
    CHECK(yozora_release_parse_json(json, strlen(json), &release) == YOZORA_RELEASE_OK);
    CHECK(release.ipa_url[0] == '\0' && release.sha_url[0] == '\0' && release.ipa_size == 0 && release.notes[0] == '\0');
    CHECK(yozora_release_parse_json("{\"tag_name\":\"v2\",\"prerelease\":true}", 36, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("{\"tag_name\":\"v2\",\"draft\":true}", 31, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("{\"tag_name\":\"v2 ../x\"}", 22, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("{\"tag_name\":\"\"}", 15, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("{\"body\":\"no tag\"}", 17, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("{}", 2, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("[]", 2, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("", 0, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json("<html>rate limit</html>", 23, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_json(NULL, 0, &release) == YOZORA_RELEASE_BAD_ARGUMENT);
    for (size_t cut = 0; cut < strlen(release_json); cut += 7)
        CHECK(yozora_release_parse_json(release_json, cut, &release) == YOZORA_RELEASE_MALFORMED);

    {
        char deep[512];
        size_t n = 0;
        n += (size_t)snprintf(deep + n, sizeof deep - n, "{\"tag_name\":\"v3\",\"x\":");
        for (int i = 0; i < 60; ++i)
            deep[n++] = '[';
        for (int i = 0; i < 60; ++i)
            deep[n++] = ']';
        n += (size_t)snprintf(deep + n, sizeof deep - n, "}");
        CHECK(yozora_release_parse_json(deep, n, &release) == YOZORA_RELEASE_MALFORMED);
    }

    {
        static char big[40000];
        size_t n = (size_t)snprintf(big, sizeof big, "{\"tag_name\":\"v4\",\"body\":\"");
        for (int i = 0; i < 30000; ++i)
            big[n++] = (char)('a' + i % 26);
        n += (size_t)snprintf(big + n, sizeof big - n, "\"}");
        CHECK(yozora_release_parse_json(big, n, &release) == YOZORA_RELEASE_OK);
        CHECK(strlen(release.notes) < sizeof release.notes);
    }

    CHECK(yozora_release_parse_atom(atom_feed, strlen(atom_feed), &release) == YOZORA_RELEASE_OK);
    CHECK(!strcmp(release.tag, "v1.5.0-stable"));
    CHECK(!strcmp(release.notes, "What's new\n\n\xe2\x80\xa2 faster & smaller\n\xe2\x80\xa2 second\n\nbye"));
    CHECK(!strcmp(release.ipa_url, "https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-v1.5.0-stable.ipa"));
    CHECK(!strcmp(release.sha_url, "https://github.com/sqmrak/yozora/releases/download/v1.5.0-stable/Yozora-v1.5.0-stable.ipa.sha256"));
    CHECK(release.ipa_size == 0);
    CHECK(yozora_release_parse_atom("<feed></feed>", 13, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_atom("<feed><entry><link href=\"x\"/></entry></feed>", 44, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_atom("<entry><link href=\"/releases/tag/a b\"/></entry>", 46, &release) == YOZORA_RELEASE_MALFORMED);
    CHECK(yozora_release_parse_atom(NULL, 0, &release) == YOZORA_RELEASE_BAD_ARGUMENT);
    for (size_t cut = 0; cut < strlen(atom_feed); cut += 11)
        yozora_release_parse_atom(atom_feed, cut, &release);

    char plain[64];
    yozora_release_notes_plain("one\n\n\n\ntwo\n", 11, 0, plain, sizeof plain);
    CHECK(!strcmp(plain, "one\n\ntwo"));
    yozora_release_notes_plain("alpha\nbeta\ngamma\ndelta", 22, 0, plain, 14);
    CHECK(!strcmp(plain, "alpha\nbeta"));
    yozora_release_notes_plain("x", 1, 0, plain, 1);
    CHECK(plain[0] == '\0');
    yozora_release_notes_plain("&lt;b&gt;x&lt;/b&gt; &amp;#x41;&amp;#9999999999;", 46, 1, plain, sizeof plain);
    CHECK(plain[0] != '\0');

    uint8_t digest[32];
    const char *line = "00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF  Yozora-v1.5.0-stable.ipa\n";
    CHECK(yozora_release_parse_sha256(line, strlen(line), digest) && digest[0] == 0x00 && digest[15] == 0xff && digest[31] == 0xff);
    CHECK(yozora_release_parse_sha256(line, 64, digest));
    CHECK(!yozora_release_parse_sha256(line, 63, digest));
    CHECK(!yozora_release_parse_sha256("zz112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF", 64, digest));
    CHECK(!yozora_release_parse_sha256("00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF0", 65, digest));

    if (failures) {
        fprintf(stderr, "test_release: %d failures\n", failures);
        return 1;
    }
    printf("test_release: ok\n");
    return 0;
}
