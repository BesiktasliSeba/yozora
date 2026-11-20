#include "yozora_keys.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char plain;
    char shifted;
    int windows_code;
    const char *code;
} punctuation_key;

/* the keys of a US layout that carry punctuation, with the character each gives without and with shift */
static const punctuation_key punctuation[] = {
    { ';', ':', 0xBA, "Semicolon" },
    { '=', '+', 0xBB, "Equal" },
    { ',', '<', 0xBC, "Comma" },
    { '-', '_', 0xBD, "Minus" },
    { '.', '>', 0xBE, "Period" },
    { '/', '?', 0xBF, "Slash" },
    { '`', '~', 0xC0, "Backquote" },
    { '[', '{', 0xDB, "BracketLeft" },
    { '\\', '|', 0xDC, "Backslash" },
    { ']', '}', 0xDD, "BracketRight" },
    { '\'', '"', 0xDE, "Quote" },
};

static const char shifted_digits[] = ")!@#$%^&*(";

static void set_identifier(yozora_key *out, uint32_t character)
{
    if (character <= 0xFFFF)
        snprintf(out->identifier, sizeof out->identifier, "U+%04X", (unsigned)character);
}

void yozora_key_for_character(uint32_t character, yozora_key *out)
{
    memset(out, 0, sizeof *out);
    if (character == '\n') {
        out->windows_code = 13;
        snprintf(out->code, sizeof out->code, "Enter");
        snprintf(out->identifier, sizeof out->identifier, "Enter");
        return;
    }
    if (character == ' ') {
        out->windows_code = 32;
        snprintf(out->code, sizeof out->code, "Space");
        set_identifier(out, character);
        return;
    }
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
        uint32_t upper = character >= 'a' ? character - 'a' + 'A' : character;
        out->windows_code = (int)upper;
        out->shift = character == upper;
        snprintf(out->code, sizeof out->code, "Key%c", (char)upper);
        set_identifier(out, upper);
        return;
    }
    if (character >= '0' && character <= '9') {
        out->windows_code = (int)character;
        snprintf(out->code, sizeof out->code, "Digit%c", (char)character);
        set_identifier(out, character);
        return;
    }
    if (character < 0x80) {
        for (size_t i = 0; i < sizeof shifted_digits - 1; ++i) {
            if ((char)character == shifted_digits[i]) {
                out->windows_code = (int)('0' + i);
                out->shift = 1;
                snprintf(out->code, sizeof out->code, "Digit%c", (char)('0' + i));
                set_identifier(out, character);
                return;
            }
        }
        for (size_t i = 0; i < sizeof punctuation / sizeof punctuation[0]; ++i) {
            if ((char)character == punctuation[i].plain || (char)character == punctuation[i].shifted) {
                out->windows_code = punctuation[i].windows_code;
                out->shift = (char)character == punctuation[i].shifted;
                snprintf(out->code, sizeof out->code, "%s", punctuation[i].code);
                set_identifier(out, character);
                return;
            }
        }
    }
    set_identifier(out, character);
}

void yozora_key_for_backspace(yozora_key *out)
{
    memset(out, 0, sizeof *out);
    out->windows_code = 8;
    snprintf(out->code, sizeof out->code, "Backspace");
    snprintf(out->identifier, sizeof out->identifier, "U+0008");
}
