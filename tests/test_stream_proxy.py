#!/usr/bin/env python3
"""the stream proxy against real servers on the loopback address: http and https origins, ranges, cookies and referers, redirects, HLS playlists, failures
and a client that goes away. usage: test_stream_proxy.py PATH_TO_yozora-proxy-cli

the https part needs the openssl command to make a root and a server certificate, and says so when it is skipped"""
import http.client
import http.server
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

CLI = os.path.abspath(sys.argv[1])
VIDEO = bytes((i * 7 + 3) & 255 for i in range(200000))
SEGMENT = bytes((i * 11 + 5) & 255 for i in range(30000))
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
# made by tests/test_fmp4_ts.c: avc1 and mp4a tracks, three video samples and two audio frames
FMP4_INIT = open(os.path.join(DATA, "fmp4-init.mp4"), "rb").read()
FMP4_SEGMENT = open(os.path.join(DATA, "fmp4-segment.m4s"), "rb").read()
FMP4_PLAYLIST = (b"#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:6\n#EXT-X-MAP:URI=\"init.mp4\"\n"
                 b"#EXTINF:6.0,\nseg0.m4s\n#EXTINF:6.0,\nseg1.m4s\n#EXT-X-ENDLIST\n")
KEY = bytes(range(16))
COOKIE = "sid=abc"
REFERER = "http://page.example/"
seen = {"echo": [], "cookie": []}


class Origin(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def allowed(self):
        return COOKIE in (self.headers.get("Cookie") or "") and self.headers.get("Referer") == REFERER

    def reply(self, status, body=b"", ctype="text/plain", extra=(), length=True):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        if length:
            self.send_header("Content-Length", str(len(body)))
        for name, value in extra:
            self.send_header(name, value)
        if not length:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def ranged(self, data, ctype):
        header = self.headers.get("Range")
        if not header:
            return self.reply(200, data, ctype, [("Accept-Ranges", "bytes")])
        m = re.match(r"bytes=(\d*)-(\d*)$", header)
        size = len(data)
        if m.group(1):
            first = int(m.group(1))
            last = int(m.group(2)) if m.group(2) else size - 1
        else:
            first = max(0, size - int(m.group(2)))
            last = size - 1
        if first >= size:
            return self.reply(416, b"", ctype, [("Content-Range", "bytes */%d" % size)])
        last = min(last, size - 1)
        self.reply(206, data[first:last + 1], ctype, [("Content-Range", "bytes %d-%d/%d" % (first, last, size)), ("Accept-Ranges", "bytes")])

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path = self.path
        if path == "/video.mp4" or path.startswith("/video.mp4?"):
            if not self.allowed():
                return self.reply(403, b"no cookie")
            return self.ranged(VIDEO, "video/mp4")
        if path == "/open.mp4":
            return self.ranged(VIDEO, "video/mp4")
        if path == "/redir":
            return self.reply(302, b"", extra=[("Location", "/video.mp4")])
        if path == "/redir-away":
            return self.reply(302, b"", extra=[("Location", "http://localhost:%d/echo" % B_PORT)])
        if path == "/redir-long":
            return self.reply(302, b"", extra=[("Location", "/video.mp4?x=" + "a" * 3000)])
        if path == "/loop":
            return self.reply(302, b"", extra=[("Location", "/loop")])
        if path == "/to-http":
            return self.reply(302, b"", extra=[("Location", "http://127.0.0.1:%d/echo-cookie" % A_PORT)])
        if path == "/echo-cookie":
            seen["cookie"].append(self.headers.get("Cookie"))
            return self.reply(200, b"ok")
        if path == "/master.m3u8":
            body = (b"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=800000\nlow/index.m3u8\n"
                    b"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"x\",URI=\"low/audio.m3u8\"\n")
            return self.reply(200, body, "application/vnd.apple.mpegurl")
        if path in ("/low/index.m3u8", "/low/audio.m3u8"):
            if not self.allowed():
                return self.reply(403)
            body = (b"#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-KEY:METHOD=AES-128,URI=\"../k.key\"\n"
                    b"#EXTINF:6.0,\nseg0.ts\n#EXTINF:6.0,\nseg1.ts?x=1\n#EXT-X-ENDLIST\n")
            # served as plain text: only the first bytes say it is a playlist
            return self.reply(200, body, "text/plain")
        if path in ("/low/seg0.ts", "/low/seg1.ts?x=1"):
            if not self.allowed():
                return self.reply(403)
            return self.ranged(SEGMENT, "video/mp2t")
        if path == "/fm/index.m3u8":
            return self.reply(200, FMP4_PLAYLIST, "application/vnd.apple.mpegurl")
        if path == "/fm/keyed.m3u8":
            return self.reply(200, FMP4_PLAYLIST.replace(b"#EXT-X-MAP", b"#EXT-X-KEY:METHOD=AES-128,URI=\"k.key\"\n#EXT-X-MAP"), "application/vnd.apple.mpegurl")
        if path == "/fm/init.mp4":
            return self.reply(200, FMP4_INIT, "video/mp4")
        if path in ("/fm/seg0.m4s", "/fm/seg1.m4s"):
            return self.reply(200, FMP4_SEGMENT, "video/iso.segment")
        if path == "/fm/bad.m3u8":
            return self.reply(200, FMP4_PLAYLIST.replace(b"init.mp4", b"bad.mp4"), "application/vnd.apple.mpegurl")
        if path == "/fm/bad.mp4":
            return self.reply(200, b"not an mp4 at all", "video/mp4")
        if path == "/k.key":
            if not self.allowed():
                return self.reply(403)
            return self.reply(200, KEY, "application/octet-stream")
        if path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            for _ in range(5):
                self.wfile.write(b"s" * 10000)
                self.wfile.flush()
                time.sleep(0.02)
            return
        if path == "/big":
            self.send_response(200)
            self.send_header("Content-Type", "video/mp4")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            try:
                for _ in range(4000):
                    self.wfile.write(b"b" * 16384)
                    self.wfile.flush()
                    time.sleep(0.002)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if path == "/empty":
            return self.reply(200, b"")
        if path.startswith("/status/"):
            code = int(path.split("/")[2])
            return self.reply(code, b"nope")
        self.reply(404, b"unknown")


class Echo(Origin):
    def do_GET(self):
        if self.path == "/echo":
            seen["echo"].append((self.headers.get("Cookie"), self.headers.get("Referer")))
            return self.reply(200, b"ok")
        self.reply(404, b"unknown")


class QuietServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    # a client that refuses the certificate leaves a broken handshake, which is what the test asks for
    def handle_error(self, request, client_address):
        pass


def serve(handler, tls=None):
    server = QuietServer(("127.0.0.1", 0), handler)
    if tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(*tls)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


class Proxy:
    def __init__(self, ca, transmux=False):
        environment = dict(os.environ)
        environment.pop("YOZORA_PROXY_TRANSMUX", None)
        if transmux:
            environment["YOZORA_PROXY_TRANSMUX"] = "1"
        self.process = subprocess.Popen([CLI, ca], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=environment)
        kind, port = self.process.stdout.readline().rstrip("\n").split("\t")
        assert kind == "port", kind
        self.port = int(port)

    def command(self, *fields):
        self.process.stdin.write("\t".join(fields) + "\n")
        self.process.stdin.flush()
        return self.process.stdout.readline().rstrip("\n").split("\t")

    def register(self, url, cookie=COOKIE, referer=REFERER):
        reply = self.command("register", url, cookie, referer)
        assert reply[0] == "ok", reply
        return reply[1], int(reply[2])

    def get(self, url, method="GET", headers=None, read=True):
        parts = http.client.urlsplit(url) if hasattr(http.client, "urlsplit") else None
        host_port = url.split("/")[2]
        path = url[url.index("/", 8):]
        connection = http.client.HTTPConnection(host_port, timeout=20)
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        body = response.read() if read else b""
        result = (response.status, dict((k.lower(), v) for k, v in response.getheaders()), body)
        connection.close()
        return result

    def raw(self, data, wait=True):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=20)
        s.sendall(data)
        chunks = []
        while wait:
            chunk = s.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
        s.close()
        return b"".join(chunks)

    def stop(self):
        reply = self.command("quit")
        assert reply == ["stopped"], reply
        assert self.process.wait(timeout=20) == 0


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def status_of(raw):
    return int(raw.split(b" ", 2)[1])


def make_certificates(directory):
    openssl = shutil.which("openssl")
    if not openssl:
        return None

    def run(*args):
        subprocess.run([openssl, *args], cwd=directory, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    for name in ("ca", "other"):
        run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", name + ".key", "-out", name + ".pem", "-days", "2", "-subj", "/CN=test-" + name)
    run("req", "-newkey", "rsa:2048", "-nodes", "-keyout", "server.key", "-out", "server.csr", "-subj", "/CN=127.0.0.1")
    with open(os.path.join(directory, "san.cnf"), "w") as f:
        f.write("subjectAltName=IP:127.0.0.1\n")
    run("x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial", "-out", "server.pem", "-days", "2", "-extfile", "san.cnf")
    return {k: os.path.join(directory, k + ".pem") for k in ("ca", "other", "server")} | {"key": os.path.join(directory, "server.key")}


def main():
    global A_PORT, B_PORT
    work = tempfile.mkdtemp(prefix="yozora-proxy-test-")
    certs = make_certificates(work)
    a = serve(Origin)
    b = serve(Echo)
    A_PORT, B_PORT = a.server_address[1], b.server_address[1]
    ca = certs["ca"] if certs else os.path.join(work, "empty.pem")
    if not certs:
        open(ca, "w").write("")
    proxy = Proxy(ca)
    base = "http://127.0.0.1:%d" % A_PORT
    passed = []

    # a full file with the cookie and the referer of the page
    url, stream = proxy.register(base + "/video.mp4")
    check(re.fullmatch(r"http://127\.0\.0\.1:%d/s/[0-9a-f]{32}/0\.mp4" % proxy.port, url), url)
    status, headers, body = proxy.get(url)
    check(status == 200 and body == VIDEO, "full file %d %d" % (status, len(body)))
    check(headers["content-type"] == "video/mp4" and headers["content-length"] == str(len(VIDEO)) and headers["accept-ranges"] == "bytes", headers)
    passed.append("full file with cookie and referer")

    # ranges pass through: a window, the tail, a suffix, and past the end
    for spec, first, last in (("bytes=100-199", 100, 199), ("bytes=199990-", 199990, 199999), ("bytes=-100", 199900, 199999), ("bytes=0-0", 0, 0)):
        status, headers, body = proxy.get(url, headers={"Range": spec})
        check(status == 206 and body == VIDEO[first:last + 1], "%s -> %d %d" % (spec, status, len(body)))
        check(headers["content-range"] == "bytes %d-%d/%d" % (first, last, len(VIDEO)), headers)
    status, headers, body = proxy.get(url, headers={"Range": "bytes=999999-"})
    check(status == 416, "range past the end %d" % status)
    passed.append("ranges")

    # the server asked for the cookie, a stream registered without one is refused by the server and the
    # refusal reaches the player as it is
    bare, _ = proxy.register(base + "/video.mp4", cookie="", referer="")
    check(proxy.get(bare)[0] == 403, "no cookie")
    wrong, _ = proxy.register(base + "/video.mp4", cookie=COOKIE, referer="http://elsewhere/")
    check(proxy.get(wrong)[0] == 403, "wrong referer")
    passed.append("cookie and referer are sent, refusals pass through")

    status, headers, body = proxy.get(url, method="HEAD")
    check(status == 200 and body == b"" and headers["content-length"] == str(len(VIDEO)), "head %d %s" % (status, headers))
    passed.append("head")

    # redirects: followed, the cookie stays with its host, a long signed location works, a loop ends
    redirect, _ = proxy.register(base + "/redir")
    check(proxy.get(redirect)[2] == VIDEO, "redirect")
    long_redirect, _ = proxy.register(base + "/redir-long")
    check(proxy.get(long_redirect)[2] == VIDEO, "long location")
    away, _ = proxy.register(base + "/redir-away")
    check(proxy.get(away)[0] == 200, "redirect to another host")
    check(seen["echo"] and seen["echo"][-1][0] is None, "the cookie went to another host: %r" % (seen["echo"],))
    check(proxy.get(proxy.register(base + "/loop")[0])[0] == 502, "redirect loop")
    passed.append("redirects, the cookie stays with its host")

    # HLS: the master and the media playlist come back with proxy addresses, the segments and the key are fetched through them
    master, _ = proxy.register(base + "/master.m3u8")
    check(master.endswith("/0.m3u8"), master)
    status, headers, body = proxy.get(master)
    text = body.decode()
    check(status == 200 and headers["content-type"].startswith("application/vnd.apple.mpegurl"), "master %d %s" % (status, headers))
    check("127.0.0.1:%d/low" % A_PORT not in text and text.count("http://127.0.0.1:%d/s/" % proxy.port) == 2, text)
    check(headers["content-length"] == str(len(body)), "the length of the rewritten playlist")
    variants = re.findall(r"http://127\.0\.0\.1:%d/s/[0-9a-f]{32}/\d+\.m3u8" % proxy.port, text)
    check(len(variants) == 2, text)
    check(proxy.get(master)[2] == body, "the same playlist gets the same addresses")
    status, headers, media = proxy.get(variants[0])
    media_text = media.decode()
    check(status == 200 and media_text.startswith("#EXTM3U") and headers["content-type"].startswith("application/vnd.apple.mpegurl"), "media %d %s" % (status, headers))
    addresses = re.findall(r"http://127\.0\.0\.1:%d/s/[0-9a-f]{32}/\d+(?:\.\w+)?" % proxy.port, media_text)
    check(len(addresses) == 3 and "k.key" not in media_text and "seg0.ts" not in media_text, media_text)
    key = [x for x in addresses if x.endswith(".key")][0]
    check(proxy.get(key)[2] == KEY, "key")
    for segment in [x for x in addresses if x.endswith(".ts")]:
        status, headers, body = proxy.get(segment)
        check(status == 200 and body == SEGMENT and headers["content-type"] == "video/mp2t", "segment %d" % status)
    passed.append("hls master, media, key and segments through the proxy")

    # a stream without a length ends when the server closes it, server errors are the proxy's failure except
    # the ones that mean something
    status, headers, body = proxy.get(proxy.register(base + "/stream")[0])
    check(status == 200 and len(body) == 50000 and "content-length" not in headers, "close delimited %d %d" % (status, len(body)))
    check(proxy.get(proxy.register(base + "/status/404")[0])[0] == 404, "404")
    check(proxy.get(proxy.register(base + "/status/500")[0])[0] == 502, "500")
    check(proxy.get(proxy.register(base + "/status/401")[0])[0] == 502, "401")
    status, headers, body = proxy.get(proxy.register(base + "/empty")[0])
    check(status == 200 and body == b"" and headers["content-length"] == "0", "empty")
    passed.append("unsized streams, statuses, empty bodies")

    # fmp4 hls for a player without it: segments become transport streams, what cannot convert is left or refused
    plain = Proxy(ca)
    playlist, _ = plain.register(base + "/fm/index.m3u8")
    status, headers, body = plain.get(playlist)
    check(status == 200 and b"#EXT-X-MAP" in body and b"#EXT-X-VERSION:7" in body and b".ts" not in body, "map kept without the switch: %r" % body)
    plain.stop()
    mux = Proxy(ca, transmux=True)
    playlist, _ = mux.register(base + "/fm/index.m3u8")
    status, headers, body = mux.get(playlist)
    text = body.decode()
    check(status == 200 and "EXT-X-MAP" not in text and "#EXT-X-VERSION:3\n" in text and "init.mp4" not in text, "playlist of fmp4: %r" % text)
    segments = re.findall(r"http://127\.0\.0\.1:%d/s/[0-9a-f]{32}/\d+\.ts" % mux.port, text)
    check(len(segments) == 2 and segments[0] != segments[1], text)
    status, headers, ts = mux.get(segments[0])
    check(status == 200 and headers["content-type"] == "video/mp2t" and headers["content-length"] == str(len(ts)), "segment %d %s" % (status, headers))
    check(len(ts) > 0 and len(ts) % 188 == 0 and all(ts[i] == 0x47 for i in range(0, len(ts), 188)), "transport stream packets")
    check(mux.get(segments[1])[2] == ts, "the same fragment gives the same segment")
    status, headers, part = mux.get(segments[0], headers={"Range": "bytes=188-375"})
    check(status == 206 and part == ts[188:376] and headers["content-range"] == "bytes 188-375/%d" % len(ts), "range of a segment %d" % status)
    check(mux.get(segments[0], headers={"Range": "bytes=%d-" % len(ts)})[0] == 416, "range past the end of a segment")
    status, headers, head = mux.get(segments[0], method="HEAD")
    check(status == 200 and head == b"" and headers["content-length"] == str(len(ts)), "head of a segment")
    keyed, _ = mux.register(base + "/fm/keyed.m3u8")
    status, headers, body = mux.get(keyed)
    check(status == 200 and b"#EXT-X-MAP" in body and b".ts" not in body, "encrypted playlist is left as it is: %r" % body)
    broken, _ = mux.register(base + "/fm/bad.m3u8")
    broken_segment = re.findall(r"http://127\.0\.0\.1:%d/s/[0-9a-f]{32}/\d+\.ts" % mux.port, mux.get(broken)[2].decode())[0]
    check(mux.get(broken_segment)[0] == 502, "a map that is not mp4")
    mux.stop()
    passed.append("fragmented mp4 segments as transport streams")

    # what a client may send: only what the proxy answers
    path = url[url.index("/s/"):]
    check(status_of(proxy.raw(b"POST " + path.encode() + b" HTTP/1.1\r\nHost: x\r\n\r\n")) == 405, "post")
    check(status_of(proxy.raw(b"GET /x HTTP/1.1\r\n\r\n")) == 404, "unknown path")
    check(status_of(proxy.raw(b"GET /s/zzzz/0 HTTP/1.1\r\n\r\n")) == 404, "bad token")
    check(status_of(proxy.raw(b"GET /s/" + b"0" * 32 + b"/0 HTTP/1.1\r\n\r\n")) == 404, "unknown token")
    check(status_of(proxy.raw(b"GET /s/../../etc/passwd HTTP/1.1\r\n\r\n")) == 404, "traversal")
    check(status_of(proxy.raw(b"GET " + path.encode() + b"?x=1 HTTP/1.1\r\n\r\n")) == 404, "query")
    check(status_of(proxy.raw(b"GET " + path.rsplit("/", 1)[0].encode() + b"/99 HTTP/1.1\r\n\r\n")) == 404, "unknown entry")
    check(status_of(proxy.raw(b"hello\r\n\r\n")) == 400, "garbage")
    check(status_of(proxy.raw(b"GET " + path.encode() + b" SPDY/3\r\n\r\n")) == 400, "version")
    check(status_of(proxy.raw(b"GET " + path.encode() + b" HTTP/1.1\r\nX: " + b"a" * 9000 + b"\r\n\r\n")) == 431, "long head")
    check(status_of(proxy.raw(b"GET " + path.encode() + b" HTTP/1.1\r\nRange: bytes=0-1,5-6\r\n\r\n")) == 416, "two ranges")
    check(status_of(proxy.raw(b"GET " + path.encode() + b" HTTP/1.1\r\nRange: items=0-1\r\n\r\n")) == 416, "other unit")
    check(status_of(proxy.raw(b"GET " + path.encode() + b" HTTP/1.1\r\nRange: bytes=-\r\n\r\n")) == 416, "empty range")
    passed.append("malformed and unknown requests")

    # tokens are random, a released stream answers 404 and the others go on
    other, other_stream = proxy.register(base + "/video.mp4")
    check(other.split("/")[4] != url.split("/")[4], "token reused")
    check(proxy.command("release", str(other_stream)) == ["ok"], "release")
    check(proxy.get(other)[0] == 404 and proxy.get(url)[2] == VIDEO, "release")
    passed.append("tokens and release")

    # a player that leaves in the middle of a transfer leaves the proxy working
    big, _ = proxy.register(base + "/big")
    s = socket.create_connection(("127.0.0.1", proxy.port), timeout=20)
    s.sendall(("GET " + big[big.index("/s/"):] + " HTTP/1.1\r\n\r\n").encode())
    got = 0
    while got < 100000:
        got += len(s.recv(65536))
    s.close()
    check(proxy.get(url, headers={"Range": "bytes=0-9"})[2] == VIDEO[:10], "after an aborted transfer")
    passed.append("client goes away mid transfer")

    # the number of streams is bounded
    streams = []
    for _ in range(16):
        reply = proxy.command("register", base + "/open.mp4", "", "")
        if reply[0] != "ok":
            break
        streams.append(int(reply[2]))
    check(proxy.command("register", base + "/open.mp4", "", "")[0] == "error", "stream limit")
    for number in streams:
        proxy.command("release", str(number))
    check(proxy.command("register", base + "/open.mp4", "", "")[0] == "ok", "after the release")
    passed.append("stream limit")

    # the proxy stops while a transfer is open
    big2, _ = proxy.register(base + "/big")
    s = socket.create_connection(("127.0.0.1", proxy.port), timeout=20)
    s.sendall(("GET " + big2[big2.index("/s/"):] + " HTTP/1.1\r\n\r\n").encode())
    s.recv(1000)
    started = time.time()
    proxy.stop()
    check(time.time() - started < 10, "stop took too long")
    s.close()
    passed.append("stop with a transfer under way")

    if certs:
        tls = serve(Origin, (certs["server"], certs["key"]))
        # (the origin handler reads these ports when it builds redirects)
        secure = "https://127.0.0.1:%d" % tls.server_address[1]
        good = Proxy(certs["ca"])
        status, headers, body = good.get(good.register(secure + "/video.mp4")[0])
        check(status == 200 and body == VIDEO, "https with the right root %d" % status)
        check(good.get(good.register(secure + "/video.mp4")[0], headers={"Range": "bytes=5-9"})[2] == VIDEO[5:10], "https range")
        # the name is checked as well as the chain
        check(good.get(good.register("https://localhost:%d/video.mp4" % tls.server_address[1])[0])[0] == 502, "name that the certificate does not carry")
        # the cookie of an https stream does not go out over http, even to the same host
        good.get(good.register(secure + "/to-http")[0])
        check(seen["cookie"] and seen["cookie"][-1] is None, "the cookie went out over http: %r" % (seen["cookie"],))
        good.stop()
        wrong = Proxy(certs["other"])
        check(wrong.get(wrong.register(secure + "/video.mp4")[0])[0] == 502, "a certificate of a root that is not in the file")
        wrong.stop()
        passed.append("https: chain and name verified, the cookie stays on its scheme")
    else:
        print("skipped: https checks need the openssl command")

    for name in passed:
        print("ok:", name)
    print("stream proxy: %d groups passed" % len(passed))
    shutil.rmtree(work, ignore_errors=True)


A_PORT = B_PORT = 0
main()
