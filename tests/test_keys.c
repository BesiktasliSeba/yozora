#include "yozora_keys.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_keys: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void expect(uint32_t character, int windows_code, int shift, const char *code, const char *identifier)
{
    yozora_key key;
    yozora_key_for_character(character, &key);
    if (key.windows_code != windows_code || key.shift != shift || strcmp(key.code, code) != 0 || strcmp(key.identifier, identifier) != 0) {
        fprintf(stderr, "test_keys: U+%04X gave %d %d '%s' '%s'\n", (unsigned)character, key.windows_code, key.shift, key.code, key.identifier);
        ++failures;
    }
}

int main(void)
{
    expect(' ', 32, 0, "Space", "U+0020");
    expect('a', 65, 0, "KeyA", "U+0041");
    expect('A', 65, 1, "KeyA", "U+0041");
    expect('z', 90, 0, "KeyZ", "U+005A");
    expect('0', 48, 0, "Digit0", "U+0030");
    expect('9', 57, 0, "Digit9", "U+0039");
    expect('!', 49, 1, "Digit1", "U+0021");
    expect(')', 48, 1, "Digit0", "U+0029");
    expect('.', 0xBE, 0, "Period", "U+002E");
    expect('>', 0xBE, 1, "Period", "U+003E");
    expect('?', 0xBF, 1, "Slash", "U+003F");
    expect('\'', 0xDE, 0, "Quote", "U+0027");
    expect('"', 0xDE, 1, "Quote", "U+0022");
    expect('\n', 13, 0, "Enter", "Enter");
    /* a Cyrillic letter has no key of a US layout, the page still gets its identifier */
    expect(0x043F, 0, 0, "", "U+043F");
    /* above the basic plane there is no four digit identifier */
    expect(0x1F600, 0, 0, "", "");
    yozora_key key;
    yozora_key_for_backspace(&key);
    CHECK(key.windows_code == 8 && strcmp(key.code, "Backspace") == 0 && strcmp(key.identifier, "U+0008") == 0);
    /* no code is cut: the longest name fits with its terminator */
    for (uint32_t c = 0; c < 0x300; ++c) {
        yozora_key_for_character(c, &key);
        CHECK(memchr(key.code, 0, sizeof key.code) != NULL);
        CHECK(memchr(key.identifier, 0, sizeof key.identifier) != NULL);
    }
    return failures ? 1 : 0;
}
