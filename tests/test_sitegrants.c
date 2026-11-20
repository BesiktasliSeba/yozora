#define _POSIX_C_SOURCE 200809L
#include "yozora_sitegrants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_sitegrants: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void test_table(void)
{
    yozora_sitegrants *grants = yozora_sitegrants_create("test 1", 3);
    CHECK(grants != NULL);
    CHECK(yozora_sitegrants_get(grants, "https://a.example") == YOZORA_GRANT_NONE);
    CHECK(yozora_sitegrants_set(grants, "https://a.example", YOZORA_GRANT_ALLOW) == 0);
    CHECK(yozora_sitegrants_set(grants, "https://b.example", YOZORA_GRANT_DENY) == 0);
    CHECK(yozora_sitegrants_get(grants, "https://a.example") == YOZORA_GRANT_ALLOW);
    CHECK(yozora_sitegrants_get(grants, "https://b.example") == YOZORA_GRANT_DENY);
    CHECK(yozora_sitegrants_get(grants, "https://c.example") == YOZORA_GRANT_NONE);
    /* a decision that changes moves the site to the newest place */
    CHECK(yozora_sitegrants_set(grants, "https://a.example", YOZORA_GRANT_DENY) == 0);
    CHECK(yozora_sitegrants_count(grants) == 2);
    CHECK(!strcmp(yozora_sitegrants_key_at(grants, 0), "https://b.example"));
    CHECK(!strcmp(yozora_sitegrants_key_at(grants, 1), "https://a.example"));
    CHECK(yozora_sitegrants_grant_at(grants, 1) == YOZORA_GRANT_DENY);
    /* past the limit the oldest decision goes */
    CHECK(yozora_sitegrants_set(grants, "https://c.example", YOZORA_GRANT_ALLOW) == 0);
    CHECK(yozora_sitegrants_set(grants, "https://d.example", YOZORA_GRANT_ALLOW) == 0);
    CHECK(yozora_sitegrants_count(grants) == 3);
    CHECK(yozora_sitegrants_get(grants, "https://b.example") == YOZORA_GRANT_NONE);
    CHECK(yozora_sitegrants_get(grants, "https://d.example") == YOZORA_GRANT_ALLOW);
    CHECK(yozora_sitegrants_remove(grants, "https://d.example") == 1);
    CHECK(yozora_sitegrants_remove(grants, "https://d.example") == 0);
    CHECK(yozora_sitegrants_key_at(grants, 9) == NULL);
    CHECK(yozora_sitegrants_grant_at(grants, 9) == YOZORA_GRANT_NONE);
    yozora_sitegrants_clear(grants);
    CHECK(yozora_sitegrants_count(grants) == 0);
    yozora_sitegrants_destroy(grants);
}

static void test_rejections(void)
{
    char long_key[YOZORA_GRANT_KEY_MAX + 2];
    yozora_sitegrants *grants = yozora_sitegrants_create("test 1", 8);
    CHECK(yozora_sitegrants_set(grants, "", YOZORA_GRANT_ALLOW) == -1);
    CHECK(yozora_sitegrants_set(grants, NULL, YOZORA_GRANT_ALLOW) == -1);
    CHECK(yozora_sitegrants_set(grants, "a\tb", YOZORA_GRANT_ALLOW) == -1);
    CHECK(yozora_sitegrants_set(grants, "a\nb", YOZORA_GRANT_ALLOW) == -1);
    CHECK(yozora_sitegrants_set(grants, "https://a.example", YOZORA_GRANT_NONE) == -1);
    memset(long_key, 'a', sizeof long_key - 1);
    long_key[sizeof long_key - 1] = '\0';
    CHECK(yozora_sitegrants_set(grants, long_key, YOZORA_GRANT_ALLOW) == -1);
    long_key[YOZORA_GRANT_KEY_MAX] = '\0';
    CHECK(yozora_sitegrants_set(grants, long_key, YOZORA_GRANT_ALLOW) == 0);
    CHECK(yozora_sitegrants_create("", 4) == NULL);
    CHECK(yozora_sitegrants_create("x", 0) == NULL);
    CHECK(yozora_sitegrants_count(NULL) == 0);
    yozora_sitegrants_destroy(grants);
}

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (!file)
        return;
    fputs(text, file);
    fclose(file);
}

static void test_file(void)
{
    char path[] = "/tmp/yozora-sitegrants-XXXXXX";
    int descriptor = mkstemp(path);
    yozora_sitegrants *grants = yozora_sitegrants_create("yozora-test 1", 8);
    yozora_sitegrants *again = yozora_sitegrants_create("yozora-test 1", 8);
    yozora_sitegrants *other = yozora_sitegrants_create("yozora-test 2", 8);
    char missing[sizeof path + 8];
    CHECK(descriptor >= 0);
    close(descriptor);
    yozora_sitegrants_set(grants, "https://a.example", YOZORA_GRANT_ALLOW);
    yozora_sitegrants_set(grants, "https://b.example:8080", YOZORA_GRANT_DENY);
    CHECK(yozora_sitegrants_save(grants, path) == 0);
    CHECK(yozora_sitegrants_load(again, path) == 0);
    CHECK(yozora_sitegrants_count(again) == 2);
    CHECK(yozora_sitegrants_get(again, "https://a.example") == YOZORA_GRANT_ALLOW);
    CHECK(yozora_sitegrants_get(again, "https://b.example:8080") == YOZORA_GRANT_DENY);
    /* another header is another format: the table stays empty */
    yozora_sitegrants_set(other, "https://stale.example", YOZORA_GRANT_ALLOW);
    CHECK(yozora_sitegrants_load(other, path) == 0);
    CHECK(yozora_sitegrants_count(other) == 0);
    /* a missing file is an empty table, not a failure */
    snprintf(missing, sizeof missing, "%s.none", path);
    yozora_sitegrants_set(again, "https://stale.example", YOZORA_GRANT_ALLOW);
    CHECK(yozora_sitegrants_load(again, missing) == 0);
    CHECK(yozora_sitegrants_count(again) == 0);
    /* lines without a tab, with an unknown word, an empty key and a last line without a break */
    write_file(path, "yozora-test 1\nno tab here\n\tgranted\nhttps://c.example\tmaybe\nhttps://d.example\tgranted\nhttps://e.example\tdenied");
    CHECK(yozora_sitegrants_load(again, path) == 0);
    CHECK(yozora_sitegrants_count(again) == 2);
    CHECK(yozora_sitegrants_get(again, "https://d.example") == YOZORA_GRANT_ALLOW);
    CHECK(yozora_sitegrants_get(again, "https://e.example") == YOZORA_GRANT_DENY);
    /* the file of the notifications before this module has the same shape */
    write_file(path, "yozora-test 1\n");
    CHECK(yozora_sitegrants_load(again, path) == 0);
    CHECK(yozora_sitegrants_count(again) == 0);
    /* a line longer than a key can be is skipped whole, the next one still reads */
    {
        FILE *file = fopen(path, "w");
        CHECK(file != NULL);
        if (file) {
            fputs("yozora-test 1\n", file);
            for (int i = 0; i < 2000; ++i)
                fputc('x', file);
            fputs("\tgranted\nhttps://f.example\tgranted\n", file);
            fclose(file);
        }
    }
    CHECK(yozora_sitegrants_load(again, path) == 0);
    CHECK(yozora_sitegrants_count(again) == 1);
    CHECK(yozora_sitegrants_get(again, "https://f.example") == YOZORA_GRANT_ALLOW);
    /* a directory that does not exist fails the save and leaves nothing behind */
    CHECK(yozora_sitegrants_save(grants, "/nonexistent-yozora-dir/grants.txt") == -1);
    remove(path);
    yozora_sitegrants_destroy(grants);
    yozora_sitegrants_destroy(again);
    yozora_sitegrants_destroy(other);
}

int main(void)
{
    test_table();
    test_rejections();
    test_file();
    if (failures) {
        fprintf(stderr, "test_sitegrants: %d failures\n", failures);
        return 1;
    }
    puts("test_sitegrants: passed");
    return 0;
}
