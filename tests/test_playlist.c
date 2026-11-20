#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yozora_playlist.h"

typedef struct {
    int calls;
    char last[YOZORA_URL_MAX];
    int fail_at;
} mapping;

/* the proxy address of an address is its own text between brackets, so a test sees what was resolved */
static int map_bracket(void *context, const char *absolute, char *out, size_t capacity) {
    mapping *m = context;
    ++m->calls;
    snprintf(m->last, sizeof m->last, "%s", absolute);
    if (m->fail_at && m->calls == m->fail_at) return 1;
    return snprintf(out, capacity, "<%s>", absolute) < (int)capacity ? 0 : 1;
}

static void check_resolve(const char *base, const char *ref, const char *expected) {
    char out[YOZORA_URL_MAX];
    yozora_playlist_status status = yozora_url_resolve(base, ref, out, sizeof out);
    if (status != YOZORA_PLAYLIST_OK || strcmp(out, expected)) {
        fprintf(stderr, "resolve(%s, %s) = %d %s, expected %s\n", base, ref, status, out, expected);
        assert(0);
    }
}

static void resolving(void) {
    char out[YOZORA_URL_MAX], small[10];
    const char *base = "http://a/b/c/d;p?q";
    /* the normal examples of RFC 3986 section 5.4.1 */
    check_resolve(base, "g", "http://a/b/c/g");
    check_resolve(base, "./g", "http://a/b/c/g");
    check_resolve(base, "g/", "http://a/b/c/g/");
    check_resolve(base, "/g", "http://a/g");
    check_resolve(base, "//g", "http://g");
    check_resolve(base, "?y", "http://a/b/c/d;p?y");
    check_resolve(base, "g?y", "http://a/b/c/g?y");
    check_resolve(base, "g#s", "http://a/b/c/g");
    check_resolve(base, "#s", "http://a/b/c/d;p?q");
    check_resolve(base, ".", "http://a/b/c/");
    check_resolve(base, "./", "http://a/b/c/");
    check_resolve(base, "..", "http://a/b/");
    check_resolve(base, "../", "http://a/b/");
    check_resolve(base, "../g", "http://a/b/g");
    check_resolve(base, "../..", "http://a/");
    check_resolve(base, "../../g", "http://a/g");
    /* and the abnormal ones: nothing climbs above the root */
    check_resolve(base, "../../../g", "http://a/g");
    check_resolve(base, "/./g", "http://a/g");
    check_resolve(base, "g/./h", "http://a/b/c/g/h");
    check_resolve(base, "g/../h", "http://a/b/c/h");
    check_resolve(base, "http://x/y/../z", "http://x/z");
    check_resolve("https://h/live/index.m3u8?token=1", "seg1.ts", "https://h/live/seg1.ts");
    check_resolve("https://h/live/index.m3u8", "//cdn.example/s/1.ts", "https://cdn.example/s/1.ts");
    check_resolve("http://h", "seg.ts", "http://h/seg.ts");
    check_resolve("http://h:8080/a/b.m3u8", "c.ts?x=1&y=2", "http://h:8080/a/c.ts?x=1&y=2");
    check_resolve("HTTP://H/a/b", "c", "HTTP://H/a/c");
    /* refusals and errors leave out empty and say which kind they are */
    assert(yozora_url_resolve(base, "data:text/plain,x", out, sizeof out) == YOZORA_PLAYLIST_REFUSED && !out[0]);
    assert(yozora_url_resolve(base, "skd://key", out, sizeof out) == YOZORA_PLAYLIST_REFUSED);
    assert(yozora_url_resolve(base, "file:///etc/passwd", out, sizeof out) == YOZORA_PLAYLIST_REFUSED);
    assert(yozora_url_resolve(base, "", out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    assert(yozora_url_resolve(base, "  ", out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    assert(yozora_url_resolve("ftp://h/a", "b", out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    assert(yozora_url_resolve("h/a", "b", out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    assert(yozora_url_resolve("http://", "b", out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    assert(yozora_url_resolve(NULL, "b", out, sizeof out) == YOZORA_PLAYLIST_BAD_ARGUMENT);
    assert(yozora_url_resolve(base, NULL, out, sizeof out) == YOZORA_PLAYLIST_BAD_ARGUMENT);
    assert(yozora_url_resolve(base, "b", NULL, 10) == YOZORA_PLAYLIST_BAD_ARGUMENT);
    assert(yozora_url_resolve(base, "b", out, 0) == YOZORA_PLAYLIST_BAD_ARGUMENT);
    /* a result that does not fit is an error, never a cut address */
    assert(yozora_url_resolve(base, "g", small, sizeof small) == YOZORA_PLAYLIST_TOO_LARGE && !small[0]);
    assert(yozora_url_resolve(base, "g", out, strlen("http://a/b/c/g")) == YOZORA_PLAYLIST_TOO_LARGE);
    assert(yozora_url_resolve(base, "g", out, strlen("http://a/b/c/g") + 1) == YOZORA_PLAYLIST_OK);
    {
        static char huge[YOZORA_URL_MAX + 10];
        memset(huge, 'a', sizeof huge - 1);
        huge[sizeof huge - 1] = '\0';
        assert(yozora_url_resolve(base, huge, out, sizeof out) == YOZORA_PLAYLIST_TOO_LARGE);
        {
            static char long_base[YOZORA_URL_MAX + 20];
            snprintf(long_base, sizeof long_base, "http://h/%s", huge);
            assert(yozora_url_resolve(long_base, "g", out, sizeof out) == YOZORA_PLAYLIST_TOO_LARGE);
        }
        /* a base that is no address is a different error from one that is too long */
        assert(yozora_url_resolve(huge, "g", out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    }
}

static void detecting(void) {
    assert(yozora_playlist_is((const unsigned char *)"#EXTM3U\n", 8));
    assert(yozora_playlist_is((const unsigned char *)"\xEF\xBB\xBF#EXTM3U\r\n", 12));
    assert(yozora_playlist_is((const unsigned char *)"  \n#EXTM3U", 10));
    assert(!yozora_playlist_is((const unsigned char *)"#EXTM3", 6));
    assert(!yozora_playlist_is((const unsigned char *)"<html>", 6));
    assert(!yozora_playlist_is((const unsigned char *)"", 0));
    assert(!yozora_playlist_is(NULL, 5));
}

static yozora_playlist_status rewrite(const char *text, const char *base, mapping *m, char *out, size_t capacity) {
    size_t written = 0;
    yozora_playlist_status status = yozora_playlist_rewrite(text, strlen(text), base, map_bracket, m, out, capacity, &written);
    if (status == YOZORA_PLAYLIST_OK) assert(written == strlen(out));
    else assert(written == 0 && !out[0]);
    return status;
}

static void rewriting(void) {
    mapping m;
    char out[4096];
    const char *media =
        "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"keys/k1.bin\",IV=0x01\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:6.0,\nseg1.ts\n#EXTINF:6.0,\n../other/seg2.ts?x=1\n#EXTINF:6.0,\nhttps://cdn.example/seg3.ts\n#EXT-X-ENDLIST\n";
    const char *expected =
        "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"<https://h/live/keys/k1.bin>\",IV=0x01\n"
        "#EXT-X-MAP:URI=\"<https://h/live/init.mp4>\"\n"
        "#EXTINF:6.0,\n<https://h/live/seg1.ts>\n#EXTINF:6.0,\n<https://h/other/seg2.ts?x=1>\n#EXTINF:6.0,\n<https://cdn.example/seg3.ts>\n#EXT-X-ENDLIST\n";
    memset(&m, 0, sizeof m);
    assert(rewrite(media, "https://h/live/index.m3u8?t=1", &m, out, sizeof out) == YOZORA_PLAYLIST_OK);
    if (strcmp(out, expected)) fprintf(stderr, "got:\n%s\nexpected:\n%s\n", out, expected);
    assert(!strcmp(out, expected));
    assert(m.calls == 5);

    /* a master playlist: the renditions and the variants */
    memset(&m, 0, sizeof m);
    assert(rewrite("#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"en\",URI=\"audio/en.m3u8\"\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=800000,CODECS=\"avc1.4d401f,mp4a.40.2\"\nlow/index.m3u8\n"
                   "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=100,URI=\"low/iframe.m3u8\"\n",
                   "http://h/master.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_OK);
    assert(strstr(out, "URI=\"<http://h/audio/en.m3u8>\""));
    assert(strstr(out, "CODECS=\"avc1.4d401f,mp4a.40.2\"\n<http://h/low/index.m3u8>\n"));
    assert(strstr(out, "URI=\"<http://h/low/iframe.m3u8>\""));
    assert(m.calls == 3);

    /* the line endings of the source stay, a missing last newline too, blank lines and comments pass */
    memset(&m, 0, sizeof m);
    assert(rewrite("#EXTM3U\r\n# a comment\r\n\r\na.ts\r\nb.ts", "http://h/x/y.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_OK);
    assert(!strcmp(out, "#EXTM3U\r\n# a comment\r\n\r\n<http://h/x/a.ts>\r\n<http://h/x/b.ts>"));

    /* other schemes are copied, an attribute whose name only ends in URI is not an address, an open quote is left alone */
    memset(&m, 0, sizeof m);
    assert(rewrite("#EXTM3U\n#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES,URI=\"skd://key1\"\n#EXT-X-DATERANGE:ID=\"x\",X-ASSET-URI=\"a.mp4\"\n"
                   "#EXT-X-KEY:METHOD=AES-128,URI=\"data:text/plain;base64,AAAA\"\n#EXT-X-MAP:URI=\"broken\n",
                   "http://h/p.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_OK);
    assert(strstr(out, "URI=\"skd://key1\"") && strstr(out, "X-ASSET-URI=\"a.mp4\"") && strstr(out, "URI=\"data:text/plain;base64,AAAA\"") && strstr(out, "URI=\"broken\n"));
    assert(m.calls == 0);

    /* a leading and trailing blank around an address goes with it */
    memset(&m, 0, sizeof m);
    assert(rewrite("#EXTM3U\n  a.ts  \n", "http://h/p.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_OK);
    assert(!strcmp(out, "#EXTM3U\n  <http://h/a.ts>  \n"));

    /* failures: a map that refuses, a base that is no address, an output that is too small, a line that is too long, bad arguments */
    memset(&m, 0, sizeof m);
    m.fail_at = 2;
    assert(rewrite("#EXTM3U\na.ts\nb.ts\nc.ts\n", "http://h/p.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_MAP_FAILED);
    memset(&m, 0, sizeof m);
    assert(rewrite("#EXTM3U\na.ts\n", "file:///p.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_BAD_URL);
    assert(rewrite("#EXTM3U\na.ts\n", "http://h/p.m3u8", &m, out, 20) == YOZORA_PLAYLIST_TOO_LARGE);
    {
        static char line[YOZORA_PLAYLIST_LINE_MAX + 100];
        static char big[sizeof line + 20];
        memset(line, 'a', sizeof line - 1);
        line[sizeof line - 1] = '\0';
        snprintf(big, sizeof big, "#EXTM3U\n%s\n", line);
        assert(rewrite(big, "http://h/p.m3u8", &m, out, sizeof out) == YOZORA_PLAYLIST_TOO_LARGE);
    }
    {
        size_t written = 7;
        assert(yozora_playlist_rewrite(NULL, 0, "http://h/", map_bracket, &m, out, sizeof out, &written) == YOZORA_PLAYLIST_BAD_ARGUMENT && written == 0);
        assert(yozora_playlist_rewrite("a", 1, "http://h/", NULL, &m, out, sizeof out, NULL) == YOZORA_PLAYLIST_BAD_ARGUMENT);
        assert(yozora_playlist_rewrite("a", 1, "http://h/", map_bracket, &m, out, 0, NULL) == YOZORA_PLAYLIST_BAD_ARGUMENT);
        assert(yozora_playlist_rewrite("", 0, "http://h/", map_bracket, &m, out, sizeof out, &written) == YOZORA_PLAYLIST_OK && written == 0 && !out[0]);
    }
}

int main(void) {
    resolving();
    detecting();
    rewriting();
    puts("playlist: reference resolution, detection, master and media rewriting, line endings, other schemes and bounds passed");
    return 0;
}
