#ifndef YOZORA_LOG_H
#define YOZORA_LOG_H

#include <stdarg.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the core log: a line goes to stderr, which the app captures for the log screen and keeps in a file. one
   event is one line "HH:MM:SS.mmm L category: message", L is D, I, W or E, the category names a part of the
   browser (mse, media, caps, notify), no line break so a line filters alone. a message past
   YOZORA_LOG_LINE_MAX is cut with "..." */

typedef enum {
    YOZORA_LOG_DEBUG = 0,
    YOZORA_LOG_INFO,
    YOZORA_LOG_WARN,
    YOZORA_LOG_ERROR
} yozora_log_level;

#define YOZORA_LOG_LINE_MAX 1024
#define YOZORA_LOG_CATEGORY_MAX 16
/* the length of the part of a line before the category: the time, the level and the spaces */
#define YOZORA_LOG_PREFIX 15

/* lines under the minimum are not written. INFO until the app asks for more */
void yozora_log_set_minimum(int level);
yozora_log_level yozora_log_minimum(void);

void yozora_log(yozora_log_level level, const char *category, const char *format, ...) __attribute__((format(printf, 3, 4)));
void yozora_vlog(yozora_log_level level, const char *category, const char *format, va_list arguments) __attribute__((format(printf, 3, 0)));

/* the line for a moment and a message, with its new line, into out of capacity bytes (YOZORA_LOG_LINE_MAX + 1
   are enough). returns the length, 0 when capacity is 0 */
size_t yozora_log_format(char *out, size_t capacity, time_t seconds, int milliseconds, yozora_log_level level, const char *category, const char *format,
                         va_list arguments) __attribute__((format(printf, 7, 0)));

/* reads the head of a line of this format: 1 with level, category (at most YOZORA_LOG_CATEGORY_MAX
   characters) and message offset, 0 for another kind of line (NSLog, a page console) */
int yozora_log_parse(const char *line, size_t length, yozora_log_level *level, char *category, size_t category_capacity, size_t *message_offset);

#ifdef __cplusplus
}
#endif
#endif
