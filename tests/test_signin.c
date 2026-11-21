#include "yozora_signin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *link;
    yozora_signin_status status;
} parse_case;

static const parse_case parse_cases[] = {
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%2Flogin&callback=app%3A%2F%2Fdone&until=https%3A%2F%2Fa.com%2Fok", YOZORA_SIGNIN_OK},
    {"YOZORA-AUTH://start/?url=http%3A%2F%2Fa.com&callback=app%3A&cookie=sid", YOZORA_SIGNIN_OK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A%2F%2Fx%3Fk%3D1&until=https%3A%2F%2Fa.com%2Fok&cookie=sid&state=1&extra=2", YOZORA_SIGNIN_OK},
    {"https://a.com", YOZORA_SIGNIN_NOT_SIGNIN},
    {"yozora://start", YOZORA_SIGNIN_NOT_SIGNIN},
    {"yozora-auth://other?url=https%3A%2F%2Fa.com", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://startx?url=https%3A%2F%2Fa.com", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://start", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://start?", YOZORA_SIGNIN_BAD_URL},
    /* a repeated field is ambiguous */
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&url=https%3A%2F%2Fb.com&callback=app%3A&cookie=s", YOZORA_SIGNIN_MALFORMED},
    /* bad escapes and controls */
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%2&callback=app%3A&cookie=s", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%zz&callback=app%3A&cookie=s", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%0A&callback=app%3A&cookie=s", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%20x&callback=app%3A&cookie=s", YOZORA_SIGNIN_MALFORMED},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=s%00", YOZORA_SIGNIN_MALFORMED},
    /* the start address */
    {"yozora-auth://start?callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    {"yozora-auth://start?url=ftp%3A%2F%2Fa.com&callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    {"yozora-auth://start?url=javascript%3Aalert(1)&callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    {"yozora-auth://start?url=https%3A%2F%2F&callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    {"yozora-auth://start?url=https%3A%2F%2F%2Fpath&callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    {"yozora-auth://start?url=https%3A%2F%2Fuser%3Apw%40a.com&callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%5C%40b.com&callback=app%3A&cookie=s", YOZORA_SIGNIN_BAD_URL},
    /* done before the person did anything */
    {"yozora-auth://start?url=https%3A%2F%2Fa.com%2Fok%2Fx&callback=app%3A&until=https%3A%2F%2Fa.com%2Fok", YOZORA_SIGNIN_BAD_URL},
    /* the callback */
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=nocolon&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=1app%3A&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=ap_p%3A&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=https%3A%2F%2Fevil.com&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=HTTP%3A%2F%2Fevil.com&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=javascript%3Aalert(1)&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=yozora-auth%3A%2F%2Fstart&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=tel%3A123&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A%2F%2Fx%23frag&cookie=s", YOZORA_SIGNIN_BAD_CALLBACK},
    /* a scheme that only starts with a blocked one is a different scheme */
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=https-app%3A%2F%2Fx&cookie=s", YOZORA_SIGNIN_OK},
    /* the end */
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A", YOZORA_SIGNIN_NO_END},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&until=%2Fok", YOZORA_SIGNIN_BAD_END},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=a%3Db", YOZORA_SIGNIN_BAD_END},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=a%3Bb", YOZORA_SIGNIN_BAD_END},
    {"yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=a%2Fb", YOZORA_SIGNIN_BAD_END},
};

static void check_parse(void)
{
    for (size_t i = 0; i < sizeof parse_cases / sizeof parse_cases[0]; ++i) {
        yozora_signin_request request;
        memset(&request, 0xAB, sizeof request);
        yozora_signin_status status = yozora_signin_parse(parse_cases[i].link, &request);
        if (status != parse_cases[i].status) {
            fprintf(stderr, "case %zu: %s gave %d, expected %d\n", i, parse_cases[i].link, status, parse_cases[i].status);
            abort();
        }
        /* a failed parse leaves nothing of what it read */
        if (status != YOZORA_SIGNIN_OK) {
            yozora_signin_request empty;
            memset(&empty, 0, sizeof empty);
            assert(memcmp(&request, &empty, sizeof request) == 0);
        }
    }
    assert(yozora_signin_parse(NULL, NULL) == YOZORA_SIGNIN_MALFORMED);
    yozora_signin_request request;
    assert(yozora_signin_parse(NULL, &request) == YOZORA_SIGNIN_NOT_SIGNIN);
}

static void check_fields(void)
{
    yozora_signin_request request;
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com%2Fl%3Fx%3D1%26y%3D2&callback=app%3A%2F%2Fdone&until=https%3A%2F%2Fa.com%2Fok&cookie=sid&state=a+b%25",
                               &request) == YOZORA_SIGNIN_OK);
    assert(strcmp(request.url, "https://a.com/l?x=1&y=2") == 0);
    assert(strcmp(request.callback, "app://done") == 0);
    assert(strcmp(request.until, "https://a.com/ok") == 0);
    assert(strcmp(request.cookie, "sid") == 0);
    /* a plus is not a space and a fragment ends the query */
    assert(strcmp(request.state, "a+b%") == 0);
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=s#state=x", &request) == YOZORA_SIGNIN_OK);
    assert(request.state[0] == '\0');
    /* empty pairs and a key without a value are skipped */
    assert(yozora_signin_parse("yozora-auth://start?&&url=https%3A%2F%2Fa.com&flag&callback=app%3A&cookie=s&", &request) == YOZORA_SIGNIN_OK);
}

static void check_limits(void)
{
    yozora_signin_request request;
    char *link = malloc(40000);
    assert(link);
    /* the longest url that fits is taken, one byte more is not */
    int n = snprintf(link, 40000, "yozora-auth://start?callback=app%%3A&cookie=s&url=https%%3A%%2F%%2Fa.com%%2F");
    size_t fixed = (size_t)n;
    size_t room = YOZORA_SIGNIN_URL_CAP - 1 - strlen("https://a.com/");
    memset(link + fixed, 'a', room);
    link[fixed + room] = '\0';
    assert(yozora_signin_parse(link, &request) == YOZORA_SIGNIN_OK);
    link[fixed + room] = 'a';
    link[fixed + room + 1] = '\0';
    assert(yozora_signin_parse(link, &request) == YOZORA_SIGNIN_MALFORMED);
    /* the callback and the state have caps of their own */
    n = snprintf(link, 40000, "yozora-auth://start?url=https%%3A%%2F%%2Fa.com&cookie=s&callback=app%%3A");
    memset(link + n, 'a', YOZORA_SIGNIN_CALLBACK_CAP - 1 - 4);
    link[n + YOZORA_SIGNIN_CALLBACK_CAP - 1 - 4] = '\0';
    assert(yozora_signin_parse(link, &request) == YOZORA_SIGNIN_OK);
    link[n + YOZORA_SIGNIN_CALLBACK_CAP - 4] = '\0';
    link[n + YOZORA_SIGNIN_CALLBACK_CAP - 1 - 4] = 'a';
    assert(yozora_signin_parse(link, &request) == YOZORA_SIGNIN_MALFORMED);
    /* a link beyond the whole cap is not read */
    memset(link, 'a', 39999);
    memcpy(link, "yozora-auth://start?state=", 26);
    link[39999] = '\0';
    assert(yozora_signin_parse(link, &request) == YOZORA_SIGNIN_MALFORMED);
    free(link);
}

static void check_finished(void)
{
    yozora_signin_request request;
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&until=https%3A%2F%2Fa.com%2Fok&cookie=sid", &request) == YOZORA_SIGNIN_OK);
    assert(!yozora_signin_finished(&request, "https://a.com/login", ""));
    assert(!yozora_signin_finished(&request, "https://a.com/login", NULL));
    assert(!yozora_signin_finished(&request, NULL, NULL));
    assert(yozora_signin_finished(&request, "https://a.com/ok", ""));
    assert(yozora_signin_finished(&request, "https://a.com/ok?code=1", NULL));
    /* a longer address of another host that merely shares the start is a different address only by its text: the caller gives a full prefix */
    assert(!yozora_signin_finished(&request, "https://a.com/o", ""));
    assert(!yozora_signin_finished(&request, "http://a.com/ok", ""));
    assert(yozora_signin_finished(&request, "https://a.com/login", "sid=1"));
    assert(yozora_signin_finished(&request, "https://a.com/login", "a=b; sid=1; c=d"));
    assert(yozora_signin_finished(&request, "https://a.com/login", "sid=1;c=d"));
    assert(!yozora_signin_finished(&request, "https://a.com/login", "sid="));
    assert(!yozora_signin_finished(&request, "https://a.com/login", "sid"));
    assert(!yozora_signin_finished(&request, "https://a.com/login", "xsid=1; sidx=2"));
    assert(!yozora_signin_finished(&request, "https://a.com/login", "a=sid=1"));
    assert(!yozora_signin_finished(NULL, "https://a.com/ok", NULL));
    /* a request with the cookie only never ends on an address */
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=sid", &request) == YOZORA_SIGNIN_OK);
    assert(!yozora_signin_finished(&request, "https://a.com/ok", ""));
}

static void check_result(void)
{
    yozora_signin_request request;
    char out[YOZORA_SIGNIN_RESULT_CAP];
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A%2F%2Fdone&cookie=sid&state=s%26t", &request) == YOZORA_SIGNIN_OK);
    size_t n = yozora_signin_result(&request, YOZORA_SIGNIN_DONE, "https://a.com/ok?code=1&x=y z", "sid=1; a=b", out, sizeof out);
    assert(n == strlen(out));
    assert(strcmp(out, "app://done?status=done&url=https%3A%2F%2Fa.com%2Fok%3Fcode%3D1%26x%3Dy%20z&cookies=sid%3D1%3B%20a%3Db&state=s%26t") == 0);
    /* a cancelled answer carries no cookies, even when some were passed */
    n = yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, "sid=1", out, sizeof out);
    assert(strcmp(out, "app://done?status=cancelled&state=s%26t") == 0 && n == strlen(out));
    n = yozora_signin_result(&request, YOZORA_SIGNIN_TOO_LARGE, "https://a.com/", "sid=1", out, sizeof out);
    assert(strcmp(out, "app://done?status=too_large&url=https%3A%2F%2Fa.com%2F&state=s%26t") == 0 && n == strlen(out));
    /* the callback with a query takes the answer after it, one that ends with ? or & does not repeat it */
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A%2F%2Fdone%3Fid%3D7&cookie=s", &request) == YOZORA_SIGNIN_OK);
    yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, out, sizeof out);
    assert(strcmp(out, "app://done?id=7&status=cancelled") == 0);
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A%2F%2Fdone%3Fid%3D7%26&cookie=s", &request) == YOZORA_SIGNIN_OK);
    yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, out, sizeof out);
    assert(strcmp(out, "app://done?id=7&status=cancelled") == 0);
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=s", &request) == YOZORA_SIGNIN_OK);
    yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, out, sizeof out);
    assert(strcmp(out, "app:?status=cancelled") == 0);
    /* non-ascii bytes are escaped one by one */
    yozora_signin_result(&request, YOZORA_SIGNIN_DONE, "https://a.com/\xd0\xbf", NULL, out, sizeof out);
    assert(strcmp(out, "app:?status=done&url=https%3A%2F%2Fa.com%2F%D0%BF") == 0);
    /* capacity: the exact length fits, one byte less leaves an empty string and 0 */
    const char *expect = "app:?status=cancelled";
    char small[32];
    n = yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, small, strlen(expect) + 1);
    assert(n == strlen(expect) && strcmp(small, expect) == 0);
    memset(small, 'x', sizeof small);
    n = yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, small, strlen(expect));
    assert(n == 0 && small[0] == '\0');
    assert(yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, small, 0) == 0);
    assert(yozora_signin_result(&request, YOZORA_SIGNIN_CANCELLED, NULL, NULL, NULL, 8) == 0);
    assert(yozora_signin_result(NULL, YOZORA_SIGNIN_CANCELLED, NULL, NULL, small, sizeof small) == 0 && small[0] == '\0');
}

static void check_large_cookies(void)
{
    yozora_signin_request request;
    char out[YOZORA_SIGNIN_RESULT_CAP];
    char *cookies = malloc(20000);
    assert(cookies);
    memset(cookies, 'a', 19999);
    cookies[19999] = '\0';
    assert(yozora_signin_parse("yozora-auth://start?url=https%3A%2F%2Fa.com&callback=app%3A&cookie=s", &request) == YOZORA_SIGNIN_OK);
    /* every byte is a letter, so the encoded cookies are as long as they were and fit */
    assert(yozora_signin_result(&request, YOZORA_SIGNIN_DONE, "https://a.com/", cookies, out, sizeof out) > 19999);
    /* a header of bytes that need an escape triples, and no longer fits the answer */
    memset(cookies, ' ', 19999);
    assert(yozora_signin_result(&request, YOZORA_SIGNIN_DONE, "https://a.com/", cookies, out, sizeof out) == 0);
    assert(out[0] == '\0');
    assert(yozora_signin_result(&request, YOZORA_SIGNIN_TOO_LARGE, "https://a.com/", cookies, out, sizeof out) > 0);
    free(cookies);
}

int main(void)
{
    check_parse();
    check_fields();
    check_limits();
    check_finished();
    check_result();
    check_large_cookies();
    puts("signin: parsing, caps, end conditions, answers and capacity limits passed");
    return 0;
}
