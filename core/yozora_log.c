#define _POSIX_C_SOURCE 200809L

#include "yozora_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static volatile int minimum_level = YOZORA_LOG_INFO;

void yozora_log_set_minimum(int level)
{
    if (level < YOZORA_LOG_DEBUG)
        level = YOZORA_LOG_DEBUG;
    if (level > YOZORA_LOG_ERROR)
        level = YOZORA_LOG_ERROR;
    minimum_level = level;
}

yozora_log_level yozora_log_minimum(void)
{
    return (yozora_log_level)minimum_level;
}

static char level_letter(yozora_log_level level)
{
    switch (level) {
    case YOZORA_LOG_DEBUG:
        return 'D';
    case YOZORA_LOG_INFO:
        return 'I';
    case YOZORA_LOG_WARN:
        return 'W';
    case YOZORA_LOG_ERROR:
        return 'E';
    }
    return 'I';
}

/* a category is lower case letters, digits and . _ -, anything else becomes a question mark, an empty or too
   long name is cut or "log" */
static size_t write_category(char *out, const char *category)
{
    size_t n = 0;
    if (category) {
        for (; category[n] && n < YOZORA_LOG_CATEGORY_MAX; ++n) {
            char c = category[n];
            int allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            out[n] = allowed ? c : '?';
        }
    }
    if (!n) {
        memcpy(out, "log", 3);
        n = 3;
    }
    return n;
}

size_t yozora_log_format(char *out, size_t capacity, time_t seconds, int milliseconds, yozora_log_level level, const char *category, const char *format,
                         va_list arguments)
{
    char message[YOZORA_LOG_LINE_MAX];
    char name[YOZORA_LOG_CATEGORY_MAX + 1];
    struct tm parts;
    size_t used, name_length, length;
    int written;
    if (!out || !capacity)
        return 0;
    memset(&parts, 0, sizeof parts);
    if (!localtime_r(&seconds, &parts))
        memset(&parts, 0, sizeof parts);
    if (milliseconds < 0 || milliseconds > 999)
        milliseconds = 0;
    name_length = write_category(name, category);
    name[name_length] = '\0';
    written = format ? vsnprintf(message, sizeof message, format, arguments) : 0;
    if (written < 0)
        written = 0;
    length = (size_t)written;
    if (length >= sizeof message) {
        /* cut at the end of a whole character: a byte that continues one (10xxxxxx) is not the start of the cut */
        size_t keep = sizeof message - 4;
        while (keep && ((unsigned char)message[keep] & 0xC0) == 0x80)
            --keep;
        memcpy(message + keep, "...", 4);
        length = keep + 3;
    }
    /* one event, one line: a break or another control character is a space */
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)message[i] < 0x20 || (unsigned char)message[i] == 0x7f)
            message[i] = ' ';
    while (length && message[length - 1] == ' ')
        --length;
    used = (size_t)snprintf(out, capacity, "%02d:%02d:%02d.%03d %c %s: ", parts.tm_hour, parts.tm_min, parts.tm_sec, milliseconds, level_letter(level), name);
    if (used >= capacity)
        used = capacity - 1;
    if (used + length + 1 >= capacity) {
        /* the line does not fit: what fits is kept, and it still ends with a line break, so it is one line of its own */
        if (capacity < used + 2) {
            if (capacity < 2) {
                out[0] = '\0';
                return 0;
            }
            out[capacity - 2] = '\n';
            out[capacity - 1] = '\0';
            return capacity - 1;
        }
        length = capacity - used - 2;
    }
    memcpy(out + used, message, length);
    out[used + length] = '\n';
    out[used + length + 1] = '\0';
    return used + length + 1;
}

void yozora_vlog(yozora_log_level level, const char *category, const char *format, va_list arguments)
{
    char line[YOZORA_LOG_LINE_MAX + YOZORA_LOG_PREFIX + YOZORA_LOG_CATEGORY_MAX + 8];
    struct timeval now;
    size_t length, sent = 0;
    if ((int)level < minimum_level)
        return;
    if (gettimeofday(&now, NULL) != 0) {
        now.tv_sec = 0;
        now.tv_usec = 0;
    }
    length = yozora_log_format(line, sizeof line, now.tv_sec, (int)(now.tv_usec / 1000), level, category, format, arguments);
    /* one write per line, a pipe takes it whole under PIPE_BUF so lines of two threads do not mix. a log that
       cannot be written has nowhere to say so and never fails its caller */
    while (sent < length) {
        ssize_t n = write(STDERR_FILENO, line + sent, length - sent);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return;
        sent += (size_t)n;
    }
}

void yozora_log(yozora_log_level level, const char *category, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    yozora_vlog(level, category, format, arguments);
    va_end(arguments);
}

static int digit_at(const char *line, size_t i)
{
    return line[i] >= '0' && line[i] <= '9';
}

int yozora_log_parse(const char *line, size_t length, yozora_log_level *level, char *category, size_t category_capacity, size_t *message_offset)
{
    static const size_t digits[] = { 0, 1, 3, 4, 6, 7, 9, 10, 11 };
    size_t n = 0;
    if (!line || length < YOZORA_LOG_PREFIX + 2)
        return 0;
    for (size_t i = 0; i < sizeof digits / sizeof digits[0]; ++i)
        if (!digit_at(line, digits[i]))
            return 0;
    if (line[2] != ':' || line[5] != ':' || line[8] != '.' || line[12] != ' ' || line[14] != ' ')
        return 0;
    {
        const char *letters = "DIWE";
        const char *found = line[13] ? strchr(letters, line[13]) : NULL;
        if (!found)
            return 0;
        if (level)
            *level = (yozora_log_level)(found - letters);
    }
    for (size_t i = YOZORA_LOG_PREFIX; i < length && n <= YOZORA_LOG_CATEGORY_MAX; ++i, ++n) {
        if (line[i] == ':') {
            if (!n || i + 1 >= length || line[i + 1] != ' ' || n > YOZORA_LOG_CATEGORY_MAX)
                return 0;
            if (category && category_capacity) {
                size_t copy = n < category_capacity - 1 ? n : category_capacity - 1;
                memcpy(category, line + YOZORA_LOG_PREFIX, copy);
                category[copy] = '\0';
            }
            if (message_offset)
                *message_offset = i + 2;
            return 1;
        }
        {
            char c = line[i];
            int allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '?';
            if (!allowed)
                return 0;
        }
    }
    return 0;
}
