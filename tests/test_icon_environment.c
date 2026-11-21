#include "yozora_icon_environment.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_icon_environment: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(void)
{
    char *source[] = {"HOME=/sandbox", "PATH=/bin", "CFFIXED_USER_HOME=/sandbox", "LANG=en", "HOMEPAGE=x", NULL};
    char *out[8];
    /* the two variables go and two come, so five entries need seven places with the end mark */
    CHECK(!yozora_icon_cache_environment(source, out, 5));
    CHECK(yozora_icon_cache_environment(source, out, 8));
    CHECK(!strcmp(out[0], "PATH=/bin"));
    CHECK(!strcmp(out[1], "LANG=en"));
    /* a name that only starts like HOME is another variable */
    CHECK(!strcmp(out[2], "HOMEPAGE=x"));
    CHECK(!strcmp(out[3], "HOME=/var/mobile"));
    CHECK(!strcmp(out[4], "CFFIXED_USER_HOME=/var/mobile"));
    CHECK(out[5] == NULL);
    /* the smallest list that fits */
    char *only[] = {NULL};
    CHECK(yozora_icon_cache_environment(only, out, 3));
    CHECK(!strcmp(out[0], "HOME=/var/mobile") && !strcmp(out[1], "CFFIXED_USER_HOME=/var/mobile") && out[2] == NULL);
    CHECK(!yozora_icon_cache_environment(only, out, 2));
    CHECK(!yozora_icon_cache_environment(NULL, out, 8));
    CHECK(!yozora_icon_cache_environment(source, NULL, 8));
    if (failures) {
        fprintf(stderr, "test_icon_environment: %d failed\n", failures);
        return 1;
    }
    puts("icon environment: the home of the app is replaced by the one of mobile, a list that does not fit is refused");
    return 0;
}
