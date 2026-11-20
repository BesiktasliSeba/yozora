#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "yozora_log.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_log: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static size_t format(char *out, size_t capacity, yozora_log_level level, const char *category, const char *text, ...) __attribute__((format(printf, 5, 6)));

static size_t format(char *out, size_t capacity, yozora_log_level level, const char *category, const char *text, ...)
{
    va_list arguments;
    va_start(arguments, text);
    /* a fixed moment, 1 January 2001 UTC plus a time of day, shown in local time, so the test reads the
       fields back instead of comparing a clock */
    size_t n = yozora_log_format(out, capacity, 1000000000, 42, level, category, text, arguments);
    va_end(arguments);
    return n;
}

static void formatting(void)
{
    char line[YOZORA_LOG_LINE_MAX + 64];
    yozora_log_level level;
    char category[32];
    size_t offset;
    size_t n = format(line, sizeof line, YOZORA_LOG_WARN, "mse", "epoch %u of %s", 3u, "video");
    CHECK(n == strlen(line) && line[n - 1] == '\n');
    CHECK(line[2] == ':' && line[5] == ':' && line[8] == '.' && !memcmp(line + 8, ".042 W mse: ", 12));
    CHECK(!strcmp(line + n - 22, "epoch 3 of video\n") || strstr(line, "mse: epoch 3 of video\n"));
    CHECK(yozora_log_parse(line, n - 1, &level, category, sizeof category, &offset) == 1);
    CHECK(level == YOZORA_LOG_WARN && !strcmp(category, "mse") && !strncmp(line + offset, "epoch 3 of video", 16));

    /* every level, and its letter */
    const char letters[] = "DIWE";
    for (int l = YOZORA_LOG_DEBUG; l <= YOZORA_LOG_ERROR; ++l) {
        n = format(line, sizeof line, (yozora_log_level)l, "caps", "x");
        CHECK(line[13] == letters[l]);
        CHECK(yozora_log_parse(line, n - 1, &level, NULL, 0, &offset) == 1 && (int)level == l);
    }

    /* a category that is not a name: other characters are question marks, an empty or missing one is "log", a long one is cut */
    format(line, sizeof line, YOZORA_LOG_INFO, "Bad Name:x", "m");
    CHECK(!strncmp(line + 15, "?ad?Name?x: m", 13) || !strncmp(line + 15, "?ad?name?x: m", 13) || strstr(line, "?ad?"));
    format(line, sizeof line, YOZORA_LOG_INFO, NULL, "m");
    CHECK(!strncmp(line + 15, "log: m\n", 7));
    format(line, sizeof line, YOZORA_LOG_INFO, "", "m");
    CHECK(!strncmp(line + 15, "log: m\n", 7));
    format(line, sizeof line, YOZORA_LOG_INFO, "abcdefghijklmnopqrstuvwxyz", "m");
    CHECK(!strncmp(line + 15, "abcdefghijklmnop: m\n", 20));

    /* one event is one line: breaks and control characters become spaces, trailing spaces go */
    n = format(line, sizeof line, YOZORA_LOG_INFO, "x", "a\nb\r\nc\td\x01 e  \n");
    CHECK(strchr(line, '\n') == line + n - 1);
    CHECK(strstr(line, "x: a b  c d  e\n") != NULL);

    /* an empty message, and a null format */
    n = format(line, sizeof line, YOZORA_LOG_INFO, "x", "%s", "");
    CHECK(line[n - 1] == '\n' && !strcmp(line + 15, "x: \n"));
}

static void cutting(void)
{
    char line[YOZORA_LOG_LINE_MAX + 64];
    char text[3000];
    memset(text, 'a', sizeof text - 1);
    text[sizeof text - 1] = '\0';
    size_t n = format(line, sizeof line, YOZORA_LOG_INFO, "x", "%s", text);
    CHECK(n <= YOZORA_LOG_LINE_MAX + YOZORA_LOG_PREFIX + 8 && line[n - 1] == '\n' && !memcmp(line + n - 4, "...\n", 4));
    /* the cut never stands inside a character of two bytes: the text is all "é" (c3 a9) */
    char accented[3000];
    for (size_t i = 0; i + 1 < sizeof accented; i += 2) {
        accented[i] = (char)0xc3;
        accented[i + 1] = (char)0xa9;
    }
    accented[sizeof accented - 1] = '\0';
    n = format(line, sizeof line, YOZORA_LOG_INFO, "x", "%s", accented);
    size_t body = n - 1 - 3;
    int valid = 1;
    for (size_t i = 18; i < body; ) {
        if ((unsigned char)line[i] == 0xc3 && i + 1 < body && (unsigned char)line[i + 1] == 0xa9)
            i += 2;
        else {
            valid = 0;
            break;
        }
    }
    CHECK(valid && !memcmp(line + n - 4, "...\n", 4));
    /* a buffer too small for the line: what fits, the line break and the end of the string, never a write past it */
    char small[20];
    memset(small, 'Z', sizeof small);
    n = format(small, sizeof small, YOZORA_LOG_INFO, "category", "a long message that does not fit");
    CHECK(n < sizeof small && small[n] == '\0' && small[n - 1] == '\n');
    char tiny[4];
    n = format(tiny, sizeof tiny, YOZORA_LOG_INFO, "x", "m");
    CHECK(n < sizeof tiny && tiny[n] == '\0');
    CHECK(format(NULL, 10, YOZORA_LOG_INFO, "x", "m") == 0);
    char none[1] = { 'Q' };
    CHECK(format(none, 0, YOZORA_LOG_INFO, "x", "m") == 0 && none[0] == 'Q');
}

static void parsing(void)
{
    yozora_log_level level;
    char category[YOZORA_LOG_CATEGORY_MAX + 1];
    size_t offset = 0;
    const char *good = "01:02:03.004 E media: it broke";
    CHECK(yozora_log_parse(good, strlen(good), &level, category, sizeof category, &offset) == 1);
    CHECK(level == YOZORA_LOG_ERROR && !strcmp(category, "media") && offset == 22 && !strcmp(good + offset, "it broke"));
    /* lines of other kinds: NSLog, a console line of a page, an unfinished line, garbage */
    const char *others[] = {
        "2026-11-20 02:33:01.123 Yozora[213:1234] yozora: media 4 plays",
        "console: log: BLOCKED: interaction.dragdrop",
        "01:02:03.004 E media",
        "01:02:03.004 X media: m",
        "01:02:03.004 E Media: m",
        "01:02:03.004 E : m",
        "01:02:03.004 E media:m",
        "1:02:03.004 E media: m",
        "01-02-03.004 E media: m",
        "01:02:03.004 E abcdefghijklmnopqrstuvwxyz: m",
        "",
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; ++i)
        CHECK(yozora_log_parse(others[i], strlen(others[i]), &level, category, sizeof category, &offset) == 0);
    CHECK(yozora_log_parse(NULL, 5, &level, category, sizeof category, &offset) == 0);
    /* the category goes into a small buffer cut to fit */
    char small[4];
    CHECK(yozora_log_parse(good, strlen(good), &level, small, sizeof small, &offset) == 1 && !strcmp(small, "med"));
}

/* the real thing: lines go to stderr, which here is a pipe, and the minimum level decides which of them arrive */
static void writing(void)
{
    int fds[2];
    char buffer[2048];
    CHECK(pipe(fds) == 0);
    int saved = dup(STDERR_FILENO);
    CHECK(saved >= 0 && dup2(fds[1], STDERR_FILENO) >= 0);
    close(fds[1]);
    CHECK(yozora_log_minimum() == YOZORA_LOG_INFO);
    yozora_log(YOZORA_LOG_DEBUG, "t", "hidden %d", 1);
    yozora_log(YOZORA_LOG_INFO, "t", "shown %d", 2);
    yozora_log_set_minimum(YOZORA_LOG_DEBUG);
    yozora_log(YOZORA_LOG_DEBUG, "t", "now shown %d", 3);
    yozora_log_set_minimum(YOZORA_LOG_ERROR);
    yozora_log(YOZORA_LOG_WARN, "t", "hidden %d", 4);
    yozora_log(YOZORA_LOG_ERROR, "t", "error %d", 5);
    yozora_log_set_minimum(99);
    CHECK(yozora_log_minimum() == YOZORA_LOG_ERROR);
    yozora_log_set_minimum(-5);
    CHECK(yozora_log_minimum() == YOZORA_LOG_DEBUG);
    yozora_log_set_minimum(YOZORA_LOG_INFO);
    dup2(saved, STDERR_FILENO);
    close(saved);
    ssize_t n = read(fds[0], buffer, sizeof buffer - 1);
    close(fds[0]);
    CHECK(n > 0);
    buffer[n > 0 ? n : 0] = '\0';
    CHECK(!strstr(buffer, "hidden"));
    CHECK(strstr(buffer, " I t: shown 2\n") && strstr(buffer, " D t: now shown 3\n") && strstr(buffer, " E t: error 5\n"));
    int lines = 0;
    for (char *p = buffer; *p; ++p)
        if (*p == '\n')
            ++lines;
    if (lines != 3)
        fprintf(stderr, "test_log: the pipe holds:\n%s", buffer);
    CHECK(lines == 3);
}

int main(void)
{
    formatting();
    cutting();
    parsing();
    writing();
    if (failures) {
        fprintf(stderr, "test_log: %d failures\n", failures);
        return 1;
    }
    puts("test_log: ok");
    return 0;
}
