/* drives the stream proxy from a test: the commands come on stdin, one per line with tabs between the fields, the answers go to stdout.
   usage: yozora-proxy-cli CA_BUNDLE
   register TAB url TAB cookie TAB referer  ->  ok TAB local address TAB stream   or   error TAB status
   release TAB stream                       ->  ok
   quit                                     ->  stopped (after the proxy stopped) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yozora_stream_proxy.h"

int main(int argc, char **argv) {
    yozora_proxy *proxy = NULL;
    yozora_proxy_config config;
    char line[16384];
    if (argc != 2) {
        fprintf(stderr, "usage: yozora-proxy-cli CA_BUNDLE\n");
        return 2;
    }
    memset(&config, 0, sizeof config);
    config.ca_bundle = argv[1];
    config.user_agent = "yozora-proxy-test/1";
    config.transmux_fmp4 = getenv("YOZORA_PROXY_TRANSMUX") != NULL;
    if (yozora_proxy_start(&config, &proxy) != YOZORA_PROXY_OK) {
        fprintf(stderr, "the proxy did not start\n");
        return 1;
    }
    printf("port\t%d\n", yozora_proxy_port(proxy));
    fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        char *fields[4] = { NULL, NULL, NULL, NULL };
        size_t count = 0, length = strlen(line);
        char *cursor = line, *tab;
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        while (count < 4) {
            fields[count++] = cursor;
            tab = strchr(cursor, '\t');
            if (!tab) break;
            *tab = '\0';
            cursor = tab + 1;
        }
        if (!strcmp(fields[0], "register") && count == 4) {
            char local[8192];
            unsigned stream = 0;
            yozora_proxy_status status = yozora_proxy_register(proxy, fields[1], fields[2], fields[3], local, sizeof local, &stream);
            if (status == YOZORA_PROXY_OK) printf("ok\t%s\t%u\n", local, stream);
            else printf("error\t%d\n", (int)status);
        } else if (!strcmp(fields[0], "release") && count == 2) {
            yozora_proxy_release(proxy, (unsigned)strtoul(fields[1], NULL, 10));
            printf("ok\n");
        } else if (!strcmp(fields[0], "quit")) {
            break;
        } else {
            printf("error\tcommand\n");
        }
        fflush(stdout);
    }
    yozora_proxy_stop(proxy);
    printf("stopped\n");
    fflush(stdout);
    return 0;
}
