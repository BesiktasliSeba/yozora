#include "yozora_debug.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_debug: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void *worker(void *unused)
{
    (void)unused;
    for (int i = 0; i < 10000; ++i)
        yozora_traffic_add(3, 1);
    return NULL;
}

int main(void)
{
    unsigned long long received = 1, sent = 1;
    yozora_traffic_read(&received, &sent);
    CHECK(received == 0 && sent == 0);
    /* four threads add at once, none of the bytes may be lost */
    pthread_t threads[4];
    for (int i = 0; i < 4; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    for (int i = 0; i < 4; ++i)
        pthread_join(threads[i], NULL);
    yozora_traffic_read(&received, &sent);
    CHECK(received == 120000 && sent == 40000);
    yozora_traffic_add(0, 0);
    yozora_traffic_read(&received, NULL);
    CHECK(received == 120000);
    yozora_traffic_read(NULL, NULL);

    char name[32];
    yozora_device_model_name("iPhone4,1", name, sizeof name);
    CHECK(!strcmp(name, "iPhone 4S"));
    yozora_device_model_name("iPhone7,2", name, sizeof name);
    CHECK(!strcmp(name, "iPhone 6"));
    yozora_device_model_name("iPhone99,1", name, sizeof name);
    CHECK(!strcmp(name, "iPhone99,1"));
    yozora_device_model_name("arm64", name, sizeof name);
    CHECK(!strcmp(name, "arm64"));
    yozora_device_model_name(NULL, name, sizeof name);
    CHECK(!name[0]);
    /* a small buffer cuts the text and keeps its end */
    char small[5];
    yozora_device_model_name("iPhone4,1", small, sizeof small);
    CHECK(!strcmp(small, "iPho"));
    yozora_device_model_name("iPhone4,1", small, 0);

    char text[16];
    yozora_format_bytes(0, text, sizeof text);
    CHECK(!strcmp(text, "0 B"));
    yozora_format_bytes(1023, text, sizeof text);
    CHECK(!strcmp(text, "1023 B"));
    yozora_format_bytes(1024, text, sizeof text);
    CHECK(!strcmp(text, "1.0 KB"));
    yozora_format_bytes(1536 * 1024ULL, text, sizeof text);
    CHECK(!strcmp(text, "1.5 MB"));
    yozora_format_bytes(5ULL * 1024 * 1024 * 1024, text, sizeof text);
    CHECK(!strcmp(text, "5.00 GB"));
    yozora_format_bytes(~0ULL, text, sizeof text);
    CHECK(strlen(text) <= 10 && !strcmp(text, "16.00 EB"));
    yozora_format_bytes(1024ULL * 1024 * 1024 * 1024, text, sizeof text);
    CHECK(!strcmp(text, "1.00 TB"));
    yozora_format_bytes(7, NULL, 0);
    return failures ? 1 : 0;
}
