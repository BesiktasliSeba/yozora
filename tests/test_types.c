#include "yozora_types.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *valid[] = {"", "plain", "Акари", "明かり", "\xf0\x9f\x8c\x99", "\xed\x9f\xbf", "\xf4\x8f\xbf\xbf"};
    const char *invalid[] = {"\x80", "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80",
                             "\xf5\x80\x80\x80", "\xe2\x82", "\xc2x", "\xf0\x80\x80\xaf"};
    char boundary[YOZORA_URL_CAP + 1];
    size_t i;
    for (i = 0; i < sizeof valid / sizeof valid[0]; ++i)
        assert(yozora_text_check(valid[i], YOZORA_TITLE_CAP, 1) == YOZORA_OK);
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(yozora_text_check(invalid[i], YOZORA_TITLE_CAP, 1) == YOZORA_INVALID);
    assert(yozora_text_check(NULL, 10, 1) == YOZORA_INVALID);
    assert(yozora_text_check("", 10, 0) == YOZORA_INVALID);
    assert(yozora_text_check("abc", 0, 1) == YOZORA_INVALID);
    assert(yozora_text_check("abc", 3, 1) == YOZORA_LIMIT);
    assert(yozora_text_check("abc", 4, 1) == YOZORA_OK);
    memset(boundary, 'a', sizeof boundary);
    boundary[YOZORA_URL_CAP - 1] = '\0';
    assert(yozora_url_text_check(boundary) == YOZORA_OK);
    boundary[YOZORA_URL_CAP - 1] = 'a';
    boundary[YOZORA_URL_CAP] = '\0';
    assert(yozora_url_text_check(boundary) == YOZORA_LIMIT);
    assert(yozora_url_text_check("https://example.test/a%20b") == YOZORA_OK);
    assert(yozora_url_text_check("https://example.test/a b") == YOZORA_INVALID);
    assert(yozora_url_text_check("https://example.test/\r\nHeader:value") == YOZORA_INVALID);
    assert(yozora_url_text_check("\x7f") == YOZORA_INVALID);
    puts("types: UTF-8, empty input, control bytes and length boundaries passed");
    return 0;
}
