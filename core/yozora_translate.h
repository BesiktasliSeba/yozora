#ifndef YOZORA_TRANSLATE_H
#define YOZORA_TRANSLATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* page translation without a network or a page: languages, the language of a text, the service request and
   its answer. the service takes texts as html, each wrapped as <a i=N>text</a>, and answers in the same
   wrapping, so the order of answers never depends on the service */

typedef enum {
    YOZORA_TR_OK = 0,
    /* the text is not a service answer: an html page of a block, a captive portal, a cut reply */
    YOZORA_TR_MALFORMED,
    /* the answer is the page of the service that refuses an address that asks too often */
    YOZORA_TR_BLOCKED,
    /* some items of the batch are missing from the answer, the rest are filled */
    YOZORA_TR_PARTIAL,
    /* the output does not fit its capacity */
    YOZORA_TR_TOO_LONG
} yozora_tr_status;

/* the languages the interface offers, codes as the service writes them ("zh-CN" and "zh-TW" for Chinese),
   names in the language itself */
size_t yozora_tr_language_count(void);
const char *yozora_tr_language_code(size_t index);
const char *yozora_tr_language_name(size_t index);
/* the index of a code ("fr", "FR-ca", "pt_BR" and "zh-Hans" are held to the code of the table), or -1 */
int yozora_tr_language_find(const char *code);

/* the language a text is written in: the script of its letters first, then for shared scripts (Cyrillic,
   Latin, Arabic) the letters and common words. declared is the page lang attribute, it settles a close call
   only (pages lie). writes the table code and a confidence 0 to 100, 0 with an empty code when there are too
   few letters */
int yozora_tr_detect(const char *text, size_t length, const char *declared, char *code, size_t capacity);

/* the address of a request: "...translate_a/t?client=gtx&sl=SOURCE&tl=TARGET&format=html". source may be "auto" */
size_t yozora_tr_url(const char *source, const char *target, char *out, size_t capacity);

/* the body of a request for texts[first .. first + count): "q=" and the form encoded html of the texts, each
   as <a i=N> with N counting from 0 in the batch. returns the body length, 0 when it does not fit */
size_t yozora_tr_body(const char *const *texts, size_t first, size_t count, char *out, size_t capacity);

/* how many items from start fit one request: at most max_items and as many as keep the encoded body under
   max_bytes (an item always goes alone at least). lengths are the byte lengths of the texts */
size_t yozora_tr_batch(const size_t *lengths, size_t total, size_t start, size_t max_bytes, size_t max_items);

/* the answer for count items: each is written to arena with a terminator and items[i] points at it, NULL when
   the answer lacks it. detected gets the language the service found (empty when none). returns OK when every
   item is there */
yozora_tr_status yozora_tr_parse(const char *answer, size_t length, size_t count, char *arena, size_t arena_capacity,
                                 const char **items, char *detected, size_t detected_capacity);

#ifdef __cplusplus
}
#endif
#endif
