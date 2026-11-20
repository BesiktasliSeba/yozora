#ifndef YOZORA_KEYS_H
#define YOZORA_KEYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* what a key event gives a page besides the text: keyCode, the physical key name and the legacy identifier.
   the physical key is that of a US layout as in a desktop browser, whatever script the character is from */
typedef struct {
    int windows_code;
    /* 1 when the character needs shift on that layout */
    int shift;
    char code[16];
    char identifier[16];
} yozora_key;

/* a printable character or '\n' for the return key. a character without a US key gets keyCode 0 and no physical key name */
void yozora_key_for_character(uint32_t character, yozora_key *out);
void yozora_key_for_backspace(yozora_key *out);

#ifdef __cplusplus
}
#endif
#endif
