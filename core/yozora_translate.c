#include "yozora_translate.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *code;
    const char *name;
} language;

static const language languages[] = {
    { "af", "Afrikaans" }, { "sq", "Shqip" }, { "am", "አማርኛ" }, { "ar", "العربية" }, { "hy", "Հայերեն" }, { "az", "Azərbaycanca" },
    { "eu", "Euskara" }, { "be", "Беларуская" }, { "bn", "বাংলা" }, { "bs", "Bosanski" }, { "bg", "Български" }, { "ca", "Català" },
    { "zh-CN", "中文 (简体)" }, { "zh-TW", "中文 (繁體)" }, { "hr", "Hrvatski" }, { "cs", "Čeština" }, { "da", "Dansk" },
    { "nl", "Nederlands" }, { "en", "English" }, { "et", "Eesti" }, { "tl", "Filipino" }, { "fi", "Suomi" }, { "fr", "Français" },
    { "gl", "Galego" }, { "ka", "ქართული" }, { "de", "Deutsch" }, { "el", "Ελληνικά" }, { "gu", "ગુજરાતી" }, { "he", "עברית" },
    { "hi", "हिन्दी" }, { "hu", "Magyar" }, { "is", "Íslenska" }, { "id", "Bahasa Indonesia" }, { "ga", "Gaeilge" },
    { "it", "Italiano" }, { "ja", "日本語" }, { "kn", "ಕನ್ನಡ" }, { "kk", "Қазақша" }, { "ko", "한국어" }, { "lv", "Latviešu" },
    { "lt", "Lietuvių" }, { "mk", "Македонски" }, { "ms", "Bahasa Melayu" }, { "ml", "മലയാളം" }, { "mr", "मराठी" },
    { "mn", "Монгол" }, { "ne", "नेपाली" }, { "no", "Norsk" }, { "fa", "فارسی" }, { "pl", "Polski" }, { "pt", "Português" },
    { "pa", "ਪੰਜਾਬੀ" }, { "ro", "Română" }, { "ru", "Русский" }, { "sr", "Српски" }, { "sk", "Slovenčina" }, { "sl", "Slovenščina" },
    { "es", "Español" }, { "sw", "Kiswahili" }, { "sv", "Svenska" }, { "ta", "தமிழ்" }, { "te", "తెలుగు" }, { "th", "ไทย" },
    { "tr", "Türkçe" }, { "uk", "Українська" }, { "ur", "اردو" }, { "uz", "Oʻzbekcha" }, { "vi", "Tiếng Việt" }, { "cy", "Cymraeg" }
};

#define LANGUAGE_COUNT (sizeof languages / sizeof languages[0])

size_t yozora_tr_language_count(void)
{
    return LANGUAGE_COUNT;
}

const char *yozora_tr_language_code(size_t index)
{
    return index < LANGUAGE_COUNT ? languages[index].code : "";
}

const char *yozora_tr_language_name(size_t index)
{
    return index < LANGUAGE_COUNT ? languages[index].name : "";
}

static const char *find_bytes(const char *haystack, size_t length, const char *needle, size_t size)
{
    if (!size || length < size)
        return NULL;
    for (size_t i = 0; i + size <= length; ++i) {
        if (haystack[i] == needle[0] && !memcmp(haystack + i, needle, size))
            return haystack + i;
    }
    return NULL;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

int yozora_tr_language_find(const char *code)
{
    char base[16];
    size_t length = 0;
    if (!code)
        return -1;
    while (code[length] && code[length] != '-' && code[length] != '_' && length < sizeof base - 1) {
        base[length] = lower(code[length]);
        ++length;
    }
    base[length] = '\0';
    if (!length)
        return -1;
    /* the region and the script of a tag decide only for Chinese */
    if (!strcmp(base, "zh")) {
        const char *rest = code + length;
        int traditional = 0;
        if (*rest) {
            char tag[16];
            size_t i = 0;
            for (++rest; rest[i] && i < sizeof tag - 1; ++i)
                tag[i] = lower(rest[i]);
            tag[i] = '\0';
            traditional = !strcmp(tag, "tw") || !strcmp(tag, "hk") || !strcmp(tag, "mo") || !strcmp(tag, "hant");
        }
        for (size_t i = 0; i < LANGUAGE_COUNT; ++i) {
            if (!strcmp(languages[i].code, traditional ? "zh-TW" : "zh-CN"))
                return (int)i;
        }
        return -1;
    }
    /* the old codes of Hebrew, Indonesian and Filipino, and Norwegian Bokmal and Nynorsk */
    if (!strcmp(base, "iw"))
        strcpy(base, "he");
    else if (!strcmp(base, "in"))
        strcpy(base, "id");
    else if (!strcmp(base, "fil"))
        strcpy(base, "tl");
    else if (!strcmp(base, "nb") || !strcmp(base, "nn"))
        strcpy(base, "no");
    for (size_t i = 0; i < LANGUAGE_COUNT; ++i) {
        if (!strcmp(languages[i].code, base))
            return (int)i;
    }
    return -1;
}

/* the next code point of a utf-8 string, or 0 at the end. a byte that is not valid is skipped as one replacement */
static uint32_t next_point(const unsigned char **cursor, const unsigned char *end)
{
    const unsigned char *p = *cursor;
    if (p >= end)
        return 0;
    uint32_t c = *p;
    if (c < 0x80) {
        *cursor = p + 1;
        return c;
    }
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0 || p + extra >= end) {
        *cursor = p + 1;
        return 0xFFFD;
    }
    uint32_t value = c & (0x3Fu >> extra);
    for (int i = 1; i <= extra; ++i) {
        if ((p[i] & 0xC0) != 0x80) {
            *cursor = p + 1;
            return 0xFFFD;
        }
        value = (value << 6) | (p[i] & 0x3F);
    }
    *cursor = p + extra + 1;
    return value;
}

enum {
    SCRIPT_LATIN, SCRIPT_CYRILLIC, SCRIPT_GREEK, SCRIPT_HEBREW, SCRIPT_ARABIC, SCRIPT_DEVANAGARI, SCRIPT_BENGALI, SCRIPT_GUJARATI,
    SCRIPT_GURMUKHI, SCRIPT_TAMIL, SCRIPT_TELUGU, SCRIPT_KANNADA, SCRIPT_MALAYALAM, SCRIPT_THAI, SCRIPT_GEORGIAN, SCRIPT_ARMENIAN,
    SCRIPT_ETHIOPIC, SCRIPT_HANGUL, SCRIPT_KANA, SCRIPT_HAN, SCRIPT_COUNT
};

/* the script of a letter, -1 for what is not a letter of a script this file knows (digits, marks, punctuation, symbols) */
static int script_of(uint32_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x1E00 && c <= 0x1EFF))
        return SCRIPT_LATIN;
    if (c >= 0x400 && c <= 0x52F)
        return SCRIPT_CYRILLIC;
    if ((c >= 0x370 && c <= 0x3FF) || (c >= 0x1F00 && c <= 0x1FFF))
        return SCRIPT_GREEK;
    if (c >= 0x5D0 && c <= 0x5EA)
        return SCRIPT_HEBREW;
    if ((c >= 0x620 && c <= 0x6FF) || (c >= 0x750 && c <= 0x77F))
        return SCRIPT_ARABIC;
    if (c >= 0x904 && c <= 0x939)
        return SCRIPT_DEVANAGARI;
    if (c >= 0x985 && c <= 0x9B9)
        return SCRIPT_BENGALI;
    if (c >= 0xA85 && c <= 0xAB9)
        return SCRIPT_GUJARATI;
    if (c >= 0xA05 && c <= 0xA39)
        return SCRIPT_GURMUKHI;
    if (c >= 0xB85 && c <= 0xBB9)
        return SCRIPT_TAMIL;
    if (c >= 0xC05 && c <= 0xC39)
        return SCRIPT_TELUGU;
    if (c >= 0xC85 && c <= 0xCB9)
        return SCRIPT_KANNADA;
    if (c >= 0xD05 && c <= 0xD3A)
        return SCRIPT_MALAYALAM;
    if (c >= 0xE01 && c <= 0xE30)
        return SCRIPT_THAI;
    if ((c >= 0x10A0 && c <= 0x10FF) || (c >= 0x2D00 && c <= 0x2D2F))
        return SCRIPT_GEORGIAN;
    if (c >= 0x531 && c <= 0x587)
        return SCRIPT_ARMENIAN;
    if (c >= 0x1200 && c <= 0x137F)
        return SCRIPT_ETHIOPIC;
    if ((c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F))
        return SCRIPT_HANGUL;
    if (c >= 0x3041 && c <= 0x30FF)
        return SCRIPT_KANA;
    if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF))
        return SCRIPT_HAN;
    return -1;
}

static const char *const script_language[SCRIPT_COUNT] = {
    NULL, NULL, "el", "he", NULL, "hi", "bn", "gu", "pa", "ta", "te", "kn", "ml", "th", "ka", "hy", "am", "ko", NULL, NULL
};

typedef struct {
    const char *code;
    const char *words;
} stoplist;

/* the commonest short words of the Latin script languages, lowercase, separated by spaces */
static const stoplist stoplists[] = {
    { "en", "the and of to in is that for it with as was on are this be at by from or have not you he she they we his her" },
    { "de", "der die und das ist nicht ein eine zu den mit von sich auf für auch im dem sie ich wir es" },
    { "fr", "le la les des est une un et en du que pas pour dans qui sur avec au ce il elle nous vous" },
    { "es", "el la los las de que y en un una es por con para no se su al lo como más pero" },
    { "it", "il lo la gli le di che è e un una per con non sono del della in da si ma anche" },
    { "pt", "o a os as de que e do da em um uma para com não é se na no por mais os ao" },
    { "nl", "de het een en van ik te dat die in is niet op zijn met voor ook aan er maar" },
    { "sv", "och att det i en som är på för med av inte har den till ett jag vi" },
    { "da", "og at det en den til er som på de med han af for ikke der var" },
    { "no", "og er det en til på som for av med ikke har den å jeg vi" },
    { "fi", "ja on ei että se oli hän mutta kuin tai olla ovat joka kun" },
    { "pl", "i w nie na to się z do że jest jak ale po co czy tak" },
    { "cs", "a je se na to že s v do jako ale by ne pro jsou" },
    { "sk", "a je sa na to že s v do ako ale by nie pre sú" },
    { "tr", "ve bir bu da de için ile çok daha olarak ne gibi var değil" },
    { "ro", "și de la în un o pe cu nu este că se din sunt mai" },
    { "hu", "a az és hogy nem is egy van meg de volt már" },
    { "id", "yang dan di ini itu dengan untuk tidak dari dalam akan pada ke" },
    { "ca", "el la de que i en un a per és amb no les una" },
    { "hr", "i je u se na da za su od s ne ili koji" },
    { "lt", "ir yra į su kad ne iš tai bet ar" },
    { "lv", "un ir ar ka no par uz bet tas nav" },
    { "et", "ja on ei et see oli kui aga ka või" },
    { "vi", "và của là có không được trong cho với những một" },
    { "sw", "na ya wa kwa ni katika la za kuwa" }
};

#define STOPLIST_COUNT (sizeof stoplists / sizeof stoplists[0])

/* a word of a list found at this token. tokens and words are compared byte by byte, ASCII letters folded to lowercase */
static int word_listed(const char *list, const char *token, size_t length)
{
    const char *p = list;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t size = end ? (size_t)(end - p) : strlen(p);
        if (size == length && !memcmp(p, token, length))
            return 1;
        if (!end)
            break;
        p = end + 1;
    }
    return 0;
}

/* the language of Cyrillic text from the letters only some of them use */
static const char *cyrillic_language(const unsigned *letters)
{
    unsigned russian = letters[0x44B - 0x400] + letters[0x44D - 0x400] + letters[0x451 - 0x400];
    unsigned ukrainian = letters[0x457 - 0x400] + letters[0x454 - 0x400] + letters[0x491 - 0x400];
    unsigned belarusian = letters[0x45E - 0x400];
    unsigned serbian = letters[0x452 - 0x400] + letters[0x45B - 0x400] + letters[0x458 - 0x400] + letters[0x459 - 0x400] + letters[0x45A - 0x400] + letters[0x45F - 0x400];
    unsigned macedonian = letters[0x453 - 0x400] + letters[0x45C - 0x400] + letters[0x455 - 0x400];
    unsigned kazakh = letters[0x4D9 - 0x400] + letters[0x493 - 0x400] + letters[0x49B - 0x400] + letters[0x4A3 - 0x400] + letters[0x4B1 - 0x400] + letters[0x4BB - 0x400];
    unsigned mongolian = letters[0x4E9 - 0x400] + letters[0x4AF - 0x400];
    unsigned bulgarian = letters[0x44A - 0x400];
    unsigned best = 0;
    const char *code = "ru";
    struct { unsigned count; const char *code; } found[] = {
        { ukrainian, "uk" }, { belarusian, "be" }, { serbian, "sr" }, { macedonian, "mk" }, { kazakh, "kk" }, { mongolian, "mn" }
    };
    for (size_t i = 0; i < sizeof found / sizeof *found; ++i) {
        if (found[i].count > best) {
            best = found[i].count;
            code = found[i].code;
        }
    }
    if (russian > best)
        return "ru";
    /* a hard sign with none of the letters of Russian is Bulgarian, which spells with it at the end of nearly every other word */
    if (!best && !russian && bulgarian)
        return "bg";
    /* belarusian writes i and Ukrainian writes i too, and only the short u tells them apart */
    return code;
}

int yozora_tr_detect(const char *text, size_t length, const char *declared, char *code, size_t capacity)
{
    if (capacity)
        code[0] = '\0';
    if (!text || !length || !capacity)
        return 0;
    unsigned scripts[SCRIPT_COUNT] = { 0 };
    unsigned cyrillic[0x500 - 0x400] = { 0 };
    unsigned total = 0;
    unsigned vietnamese = 0;
    unsigned traditional = 0;
    const unsigned char *cursor = (const unsigned char *)text, *end = cursor + length;
    /* the first part of a long text tells the language as well as the rest does */
    if (length > 6000)
        end = cursor + 6000;
    while (cursor < end) {
        uint32_t c = next_point(&cursor, end);
        if (!c)
            break;
        int script = script_of(c);
        if (script < 0)
            continue;
        ++scripts[script];
        ++total;
        if (script == SCRIPT_CYRILLIC && c < 0x500)
            ++cyrillic[c - 0x400];
        if (script == SCRIPT_LATIN && ((c >= 0x1EA0 && c <= 0x1EF9) || c == 0x111 || c == 0x1A1 || c == 0x1B0))
            ++vietnamese;
        if (c == 0x5011 || c == 0x9019 || c == 0x8AAA || c == 0x570B || c == 0x5B78 || c == 0x81FA || c == 0x9EBC || c == 0x7232)
            ++traditional;
    }
    if (total < 8)
        return 0;
    int top = 0;
    for (int i = 1; i < SCRIPT_COUNT; ++i) {
        if (scripts[i] > scripts[top])
            top = i;
    }
    /* japanese is written with Han and kana together: the kana are the sign, and few of them are enough */
    if (scripts[SCRIPT_KANA] * 12 >= total || (scripts[SCRIPT_KANA] > 2 && scripts[SCRIPT_HAN] > 0 && top == SCRIPT_HAN)) {
        snprintf(code, capacity, "ja");
        return 95;
    }
    if (top == SCRIPT_HAN) {
        snprintf(code, capacity, traditional ? "zh-TW" : "zh-CN");
        return 90;
    }
    if (top == SCRIPT_ARABIC) {
        unsigned persian = 0, urdu = 0;
        /* letters of Persian and of Urdu that Arabic does not have: pe, che, zhe, gaf; tte, dde, rre, noon ghunna, yeh barree */
        const unsigned char *scan = (const unsigned char *)text;
        const unsigned char *stop = end;
        while (scan < stop) {
            uint32_t c = next_point(&scan, stop);
            if (!c)
                break;
            if (c == 0x67E || c == 0x686 || c == 0x698 || c == 0x6AF)
                ++persian;
            if (c == 0x679 || c == 0x688 || c == 0x691 || c == 0x6BA || c == 0x6D2)
                ++urdu;
        }
        const char *language = urdu > persian && urdu ? "ur" : persian ? "fa" : "ar";
        snprintf(code, capacity, "%s", language);
        return 85;
    }
    if (top == SCRIPT_CYRILLIC) {
        snprintf(code, capacity, "%s", cyrillic_language(cyrillic));
        return 85;
    }
    if (script_language[top]) {
        snprintf(code, capacity, "%s", script_language[top]);
        return 95;
    }
    if (top != SCRIPT_LATIN)
        return 0;
    if (vietnamese >= 3) {
        snprintf(code, capacity, "vi");
        return 90;
    }
    /* latin: the words that each language uses most. the score of a language is the share of the words of the text that are on its list */
    unsigned score[STOPLIST_COUNT] = { 0 };
    unsigned words = 0;
    cursor = (const unsigned char *)text;
    while (cursor < end && words < 800) {
        char token[24];
        size_t size = 0;
        int too_long = 0;
        while (cursor < end) {
            const unsigned char *before = cursor;
            uint32_t c = next_point(&cursor, end);
            if (!c)
                break;
            int letter = script_of(c) == SCRIPT_LATIN;
            if (!letter) {
                if (size)
                    break;
                continue;
            }
            /* the token keeps the bytes of the letter, ASCII folded to lowercase */
            for (const unsigned char *b = before; b < cursor; ++b) {
                if (size + 1 < sizeof token)
                    token[size++] = lower((char)*b);
                else
                    too_long = 1;
            }
        }
        if (!size)
            continue;
        ++words;
        if (too_long)
            continue;
        for (size_t i = 0; i < STOPLIST_COUNT; ++i) {
            if (word_listed(stoplists[i].words, token, size))
                ++score[i];
        }
    }
    if (words < 4)
        return 0;
    size_t best = 0, second = SIZE_MAX;
    for (size_t i = 1; i < STOPLIST_COUNT; ++i) {
        if (score[i] > score[best])
            best = i;
    }
    for (size_t i = 0; i < STOPLIST_COUNT; ++i) {
        if (i != best && (second == SIZE_MAX || score[i] > score[second]))
            second = i;
    }
    if (score[best] < 2)
        return 0;
    size_t chosen = best;
    /* the language the page declares settles a close call between the two best */
    int declared_index = yozora_tr_language_find(declared);
    if (declared_index >= 0) {
        for (size_t i = 0; i < STOPLIST_COUNT; ++i) {
            if (!strcmp(stoplists[i].code, languages[declared_index].code) && score[i] * 10 >= score[best] * 7)
                chosen = i;
        }
    }
    int confidence = (int)(score[chosen] * 100 * 3 / words);
    if (confidence > 100)
        confidence = 100;
    if (second != SIZE_MAX && score[second] * 10 >= score[best] * 9)
        confidence /= 2;
    snprintf(code, capacity, "%s", stoplists[chosen].code);
    return confidence ? confidence : 1;
}

static int is_unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

/* appends the form encoding of a string to out, returns 0 when it does not fit */
static int append_encoded(char *out, size_t capacity, size_t *used, const char *text, size_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (is_unreserved(c)) {
            if (*used + 1 >= capacity)
                return 0;
            out[(*used)++] = (char)c;
        } else {
            if (*used + 3 >= capacity)
                return 0;
            out[(*used)++] = '%';
            out[(*used)++] = hex[c >> 4];
            out[(*used)++] = hex[c & 15];
        }
    }
    return 1;
}

size_t yozora_tr_url(const char *source, const char *target, char *out, size_t capacity)
{
    size_t used = 0;
    static const char head[] = "https://translate.googleapis.com/translate_a/t?client=gtx&format=html&v=1.0&sl=";
    if (!out || !capacity || !source || !target)
        return 0;
    if (sizeof head > capacity)
        return 0;
    memcpy(out, head, sizeof head - 1);
    used = sizeof head - 1;
    if (!append_encoded(out, capacity, &used, source, strlen(source)))
        return 0;
    static const char middle[] = "&tl=";
    if (used + sizeof middle >= capacity)
        return 0;
    memcpy(out + used, middle, sizeof middle - 1);
    used += sizeof middle - 1;
    if (!append_encoded(out, capacity, &used, target, strlen(target)))
        return 0;
    out[used] = '\0';
    return used;
}

/* the html of one item: <a i=N>text</a>, with & < and > of the text written as entities */
static int append_item(char *out, size_t capacity, size_t *used, size_t index, const char *text)
{
    char head[24];
    int size = snprintf(head, sizeof head, "<a i=%zu>", index);
    if (size < 0 || !append_encoded(out, capacity, used, head, (size_t)size))
        return 0;
    for (const char *p = text; *p;) {
        const char *entity = NULL;
        size_t run = 0;
        if (*p == '&')
            entity = "&amp;";
        else if (*p == '<')
            entity = "&lt;";
        else if (*p == '>')
            entity = "&gt;";
        if (entity) {
            if (!append_encoded(out, capacity, used, entity, strlen(entity)))
                return 0;
            ++p;
            continue;
        }
        while (p[run] && p[run] != '&' && p[run] != '<' && p[run] != '>')
            ++run;
        if (!append_encoded(out, capacity, used, p, run))
            return 0;
        p += run;
    }
    return append_encoded(out, capacity, used, "</a>", 4);
}

size_t yozora_tr_body(const char *const *texts, size_t first, size_t count, char *out, size_t capacity)
{
    size_t used = 0;
    if (!out || capacity < 3 || !texts)
        return 0;
    out[used++] = 'q';
    out[used++] = '=';
    for (size_t i = 0; i < count; ++i) {
        if (!texts[first + i] || !append_item(out, capacity, &used, i, texts[first + i]))
            return 0;
    }
    out[used] = '\0';
    return used;
}

size_t yozora_tr_batch(const size_t *lengths, size_t total, size_t start, size_t max_bytes, size_t max_items)
{
    size_t count = 0, bytes = 0;
    if (start >= total || !max_items)
        return 0;
    while (start + count < total && count < max_items) {
        /* the form encoding takes up to three bytes for each, and the wrapping of an item is a few more */
        size_t cost = lengths[start + count] * 3 + 24;
        if (count && bytes + cost > max_bytes)
            break;
        bytes += cost;
        ++count;
    }
    return count;
}

/* decodes the json string after its opening quote at *cursor into out, advancing past the closing quote.
   returns the length, -1 when the output is full, -2 when the string is cut or has a bad escape */
static long json_string(const char **cursor, const char *end, char *out, size_t capacity)
{
    const char *p = *cursor;
    size_t used = 0;
    while (p < end && *p != '"') {
        unsigned char c = (unsigned char)*p;
        uint32_t point = c;
        if (c == '\\') {
            if (++p >= end)
                return -2;
            switch (*p) {
            case 'n': point = '\n'; break;
            case 't': point = '\t'; break;
            case 'r': point = '\r'; break;
            case 'b': point = '\b'; break;
            case 'f': point = '\f'; break;
            case 'u': {
                if (p + 4 >= end)
                    return -2;
                point = 0;
                for (int i = 1; i <= 4; ++i) {
                    char h = p[i];
                    uint32_t digit = h >= '0' && h <= '9' ? (uint32_t)(h - '0') : h >= 'a' && h <= 'f' ? (uint32_t)(h - 'a' + 10) : h >= 'A' && h <= 'F' ? (uint32_t)(h - 'A' + 10) : 16;
                    if (digit == 16)
                        return -2;
                    point = point * 16 + digit;
                }
                p += 4;
                if (point >= 0xD800 && point <= 0xDBFF && p + 6 < end && p[1] == '\\' && p[2] == 'u') {
                    uint32_t low = 0;
                    int valid = 1;
                    for (int i = 3; i <= 6; ++i) {
                        char h = p[i];
                        uint32_t digit = h >= '0' && h <= '9' ? (uint32_t)(h - '0') : h >= 'a' && h <= 'f' ? (uint32_t)(h - 'a' + 10) : h >= 'A' && h <= 'F' ? (uint32_t)(h - 'A' + 10) : 16;
                        if (digit == 16)
                            valid = 0;
                        low = low * 16 + digit;
                    }
                    if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                        point = 0x10000 + ((point - 0xD800) << 10) + (low - 0xDC00);
                        p += 6;
                    }
                }
                break;
            }
            default:
                point = (unsigned char)*p;
                break;
            }
            ++p;
            /* encode the code point */
            if (point < 0x80) {
                if (used + 1 >= capacity)
                    return -1;
                out[used++] = (char)point;
            } else if (point < 0x800) {
                if (used + 2 >= capacity)
                    return -1;
                out[used++] = (char)(0xC0 | (point >> 6));
                out[used++] = (char)(0x80 | (point & 0x3F));
            } else if (point < 0x10000) {
                if (used + 3 >= capacity)
                    return -1;
                out[used++] = (char)(0xE0 | (point >> 12));
                out[used++] = (char)(0x80 | ((point >> 6) & 0x3F));
                out[used++] = (char)(0x80 | (point & 0x3F));
            } else {
                if (used + 4 >= capacity)
                    return -1;
                out[used++] = (char)(0xF0 | (point >> 18));
                out[used++] = (char)(0x80 | ((point >> 12) & 0x3F));
                out[used++] = (char)(0x80 | ((point >> 6) & 0x3F));
                out[used++] = (char)(0x80 | (point & 0x3F));
            }
            continue;
        }
        if (used + 1 >= capacity)
            return -1;
        out[used++] = (char)c;
        ++p;
    }
    if (p >= end)
        return -2;
    *cursor = p + 1;
    out[used] = '\0';
    return (long)used;
}

/* replaces the entities of html in place and removes any tag, returns the new length */
static size_t unescape_html(char *text, size_t length)
{
    size_t read = 0, write = 0;
    while (read < length) {
        char c = text[read];
        if (c == '<') {
            while (read < length && text[read] != '>')
                ++read;
            if (read < length)
                ++read;
            continue;
        }
        if (c != '&') {
            text[write++] = c;
            ++read;
            continue;
        }
        size_t end = read + 1;
        while (end < length && end - read < 12 && text[end] != ';' && text[end] != '&' && text[end] != '<')
            ++end;
        if (end >= length || text[end] != ';') {
            text[write++] = c;
            ++read;
            continue;
        }
        const char *name = text + read + 1;
        size_t size = end - read - 1;
        uint32_t point = 0;
        if (size == 3 && !memcmp(name, "amp", 3))
            point = '&';
        else if (size == 2 && !memcmp(name, "lt", 2))
            point = '<';
        else if (size == 2 && !memcmp(name, "gt", 2))
            point = '>';
        else if (size == 4 && !memcmp(name, "quot", 4))
            point = '"';
        else if (size == 4 && !memcmp(name, "apos", 4))
            point = '\'';
        else if (size == 4 && !memcmp(name, "nbsp", 4))
            point = 0xA0;
        else if (size >= 2 && name[0] == '#') {
            int hex = name[1] == 'x' || name[1] == 'X';
            size_t i = hex ? 2 : 1;
            int valid = i < size;
            for (; i < size && valid; ++i) {
                char h = name[i];
                uint32_t digit = h >= '0' && h <= '9' ? (uint32_t)(h - '0') : hex && h >= 'a' && h <= 'f' ? (uint32_t)(h - 'a' + 10) : hex && h >= 'A' && h <= 'F' ? (uint32_t)(h - 'A' + 10) : 99;
                if (digit >= (hex ? 16u : 10u))
                    valid = 0;
                else
                    point = point * (hex ? 16 : 10) + digit;
            }
            if (!valid || point > 0x10FFFF)
                point = 0;
        }
        if (!point) {
            text[write++] = c;
            ++read;
            continue;
        }
        /* a code point is never longer as utf-8 than the entity that wrote it (the shortest entity is four bytes) */
        if (point < 0x80)
            text[write++] = (char)point;
        else if (point < 0x800) {
            text[write++] = (char)(0xC0 | (point >> 6));
            text[write++] = (char)(0x80 | (point & 0x3F));
        } else if (point < 0x10000) {
            text[write++] = (char)(0xE0 | (point >> 12));
            text[write++] = (char)(0x80 | ((point >> 6) & 0x3F));
            text[write++] = (char)(0x80 | (point & 0x3F));
        } else {
            text[write++] = (char)(0xF0 | (point >> 18));
            text[write++] = (char)(0x80 | ((point >> 12) & 0x3F));
            text[write++] = (char)(0x80 | ((point >> 6) & 0x3F));
            text[write++] = (char)(0x80 | (point & 0x3F));
        }
        read = end + 1;
    }
    text[write] = '\0';
    return write;
}

yozora_tr_status yozora_tr_parse(const char *answer, size_t length, size_t count, char *arena, size_t arena_capacity,
                                 const char **items, char *detected, size_t detected_capacity)
{
    for (size_t i = 0; i < count; ++i)
        items[i] = NULL;
    if (detected_capacity)
        detected[0] = '\0';
    if (!answer || !arena || arena_capacity < 2)
        return YOZORA_TR_MALFORMED;
    const char *p = answer, *end = answer + length;
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '\xEF' || *p == '\xBB' || *p == '\xBF'))
        ++p;
    if (p >= end || *p != '[') {
        if (length && (find_bytes(answer, length, "Sorry", 5) || find_bytes(answer, length, "unusual traffic", 15)))
            return YOZORA_TR_BLOCKED;
        return YOZORA_TR_MALFORMED;
    }
    /* the answer is [["html","language"]], strings at the second level, the first of each pair is html and
       goes one after the other into the arena */
    size_t html = 0;
    int depth = 0, index = 0, first_pair = 1;
    for (; p < end; ++p) {
        char c = *p;
        if (c == '[') {
            ++depth;
            if (depth == 2)
                index = 0;
        } else if (c == ']') {
            if (depth == 2)
                first_pair = 0;
            if (--depth <= 0)
                break;
        } else if (c == ',' && depth == 2) {
            ++index;
        } else if (c == '"' && depth == 2) {
            const char *cursor = p + 1;
            if (index == 0) {
                long size = json_string(&cursor, end, arena + html, arena_capacity - html);
                if (size < 0)
                    return size == -1 ? YOZORA_TR_TOO_LONG : YOZORA_TR_MALFORMED;
                html += (size_t)size;
            } else if (index == 1 && first_pair && detected_capacity) {
                if (json_string(&cursor, end, detected, detected_capacity) < 0)
                    detected[0] = '\0';
            } else {
                char skip[64];
                if (json_string(&cursor, end, skip, sizeof skip) == -2)
                    return YOZORA_TR_MALFORMED;
            }
            p = cursor - 1;
        } else if (c == '"') {
            /* a string at another depth: skip it whole */
            const char *cursor = p + 1;
            while (cursor < end && *cursor != '"')
                cursor += *cursor == '\\' ? 2 : 1;
            p = cursor;
        }
    }
    if (depth > 0 || !html)
        return YOZORA_TR_MALFORMED;
    /* the items of the batch are cut out of the html in place: what is written is always behind what is read */
    size_t written = 0, found = 0;
    char *scan = arena;
    char *stop = arena + html;
    while (scan < stop) {
        char *open = (char *)find_bytes(scan, (size_t)(stop - scan), "<a i=", 5);
        if (!open)
            break;
        char *number = open + 5;
        size_t index_value = 0;
        int digits = 0;
        /* the service may put a space or a quote before the number */
        while (number < stop && (*number == ' ' || *number == '"' || *number == '\''))
            ++number;
        while (number < stop && *number >= '0' && *number <= '9' && digits < 9) {
            index_value = index_value * 10 + (size_t)(*number - '0');
            ++number;
            ++digits;
        }
        while (number < stop && *number != '>')
            ++number;
        if (number >= stop || !digits) {
            scan = open + 5;
            continue;
        }
        char *content = number + 1;
        char *close = (char *)find_bytes(content, (size_t)(stop - content), "</a>", 4);
        size_t size = close ? (size_t)(close - content) : (size_t)(stop - content);
        scan = close ? close + 4 : stop;
        if (index_value >= count || items[index_value])
            continue;
        memmove(arena + written, content, size);
        arena[written + size] = '\0';
        size = unescape_html(arena + written, size);
        items[index_value] = arena + written;
        written += size + 1;
        ++found;
    }
    if (!found)
        return YOZORA_TR_MALFORMED;
    return found == count ? YOZORA_TR_OK : YOZORA_TR_PARTIAL;
}
