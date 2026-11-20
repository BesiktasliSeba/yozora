import base64
import hashlib
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import ssl
import struct
import zlib
import subprocess
import sys
import tempfile
import threading
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_core_loading import ROOT

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
events = []
reports = []
lock = threading.Lock()
blocked_release = threading.Event()


def record(text):
    with lock:
        events.append(text)


def read_exact(connection, count):
    data = b""
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


def read_frame(connection):
    first, second = read_exact(connection, 2)
    length = second & 0x7f
    if length == 126:
        length = struct.unpack(">H", read_exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", read_exact(connection, 8))[0]
    masked = bool(second & 0x80)
    key = read_exact(connection, 4) if masked else b""
    payload = read_exact(connection, length)
    if masked:
        payload = bytes(byte ^ key[index % 4] for index, byte in enumerate(payload))
    read_frame.rsv1 = bool(first & 0x40)
    return first & 0x0f, bool(first & 0x80), masked, payload


def write_frame(connection, opcode, payload, final=True, rsv1=False):
    header = bytes([(0x80 if final else 0) | (0x40 if rsv1 else 0) | opcode])
    if len(payload) < 126:
        header += bytes([len(payload)])
    elif len(payload) < 65536:
        header += bytes([126]) + struct.pack(">H", len(payload))
    else:
        header += bytes([127]) + struct.pack(">Q", len(payload))
    connection.sendall(header + payload)


def handshake(connection, port):
    data = b""
    while b"\r\n\r\n" not in data:
        data += connection.recv(4096)
    lines = data.decode("latin-1").split("\r\n")
    path = lines[0].split()[1]
    headers = {line.split(":", 1)[0].lower(): line.split(":", 1)[1].strip() for line in lines[1:] if ":" in line}
    return path, headers


def accept(connection, headers, protocol=None, extension=None, cookie=None):
    key = headers["sec-websocket-key"].encode()
    answer = base64.b64encode(hashlib.sha1(key + GUID).digest()).decode()
    extra = f"Sec-WebSocket-Protocol: {protocol}\r\n" if protocol else ""
    extra += f"Sec-WebSocket-Extensions: {extension}\r\n" if extension else ""
    extra += f"Set-Cookie: {cookie}\r\n" if cookie else ""
    connection.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        f"Sec-WebSocket-Accept: {answer}\r\n{extra}\r\n").encode())


def serve(connection, port):
    try:
        path, headers = handshake(connection, port)
        if path == "/refuse":
            connection.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
            record("refused")
            return
        if path == "/echo":
            record("origin:" + headers.get("origin", ""))
            record("cookie:" + headers.get("cookie", ""))
            record("offered:" + headers.get("sec-websocket-protocol", ""))
            record("extensions:" + headers.get("sec-websocket-extensions", "none"))
            accept(connection, headers, "chat", cookie="wsid=77; Path=/")
            for index in range(4):
                opcode, final, masked, payload = read_frame(connection)
                record(f"{'text' if opcode == 1 else 'binary'}:{payload.hex()}:masked={masked}:final={final}")
                # the Blob and the text after it are only recorded, in the order they were sent
                if index < 2:
                    write_frame(connection, opcode, payload)
            # a message in three fragments with a ping between them, which must be answered before the message ends
            write_frame(connection, 1, b"fr", final=False)
            write_frame(connection, 9, b"p1")
            write_frame(connection, 0, b"ag", final=False)
            write_frame(connection, 0, b"ment")
            opcode, _, _, payload = read_frame(connection)
            record(f"pong:{opcode}:{payload.decode()}")
            write_frame(connection, 1, b"x" * 100000)
            write_frame(connection, 8, struct.pack(">H", 4001) + b"bye")
            opcode, _, _, payload = read_frame(connection)
            record(f"close:{opcode}:{struct.unpack('>H', payload[:2])[0]}")
        elif path == "/deflate":
            record("deflate-offer:" + headers.get("sec-websocket-extensions", "none"))
            accept(connection, headers, extension="permessage-deflate; client_no_context_takeover; server_no_context_takeover")
            inflater = zlib.decompressobj(-15)
            opcode, final, masked, payload = read_frame(connection)
            compressed = read_frame.rsv1
            text = inflater.decompress(payload + b"\x00\x00\xff\xff") if compressed else payload
            record(f"deflate-in:{compressed}:{len(payload)}<{len(text)}:{text[:3].decode()}")
            deflater = zlib.compressobj(wbits=-15)
            body = deflater.compress(b"B" * 20000) + deflater.flush(zlib.Z_SYNC_FLUSH)
            write_frame(connection, 1, body[:-4], rsv1=True)
            write_frame(connection, 1, b"tiny")
            # nine megabytes of zeros that fit in a few kilobytes, above the limit the client allows
            bomb = zlib.compressobj(wbits=-15)
            body = bomb.compress(b"\x00" * (9 * 1024 * 1024)) + bomb.flush(zlib.Z_SYNC_FLUSH)
            write_frame(connection, 2, body[:-4], rsv1=True)
            opcode, _, _, payload = read_frame(connection)
            record(f"bomb:{opcode}:{struct.unpack('>H', payload[:2])[0]}")
        elif path == "/takeover":
            # the compression context lives on between messages, both ways
            accept(connection, headers, extension="permessage-deflate")
            inflater = zlib.decompressobj(-15)
            deflater = zlib.compressobj(wbits=-15)
            for index in range(2):
                opcode, final, masked, payload = read_frame(connection)
                text = inflater.decompress(payload + b"\x00\x00\xff\xff")
                record(f"takeover-in:{read_frame.rsv1}:{len(text)}")
                body = deflater.compress(text[::-1]) + deflater.flush(zlib.Z_SYNC_FLUSH)
                write_frame(connection, 1, body[:-4], rsv1=True)
            read_frame(connection)
        elif path == "/worker":
            accept(connection, headers)
            opcode, _, _, payload = read_frame(connection)
            record(f"worker:{payload.decode()}")
            write_frame(connection, 1, b"from-server")
            read_frame(connection)
        elif path == "/badutf8":
            accept(connection, headers)
            write_frame(connection, 1, b"\xff\xfe")
            opcode, _, _, payload = read_frame(connection)
            record(f"badutf8:{opcode}:{struct.unpack('>H', payload[:2])[0]}")
        elif path == "/pageclose":
            accept(connection, headers)
            opcode, _, _, payload = read_frame(connection)
            record(f"pageclose:{opcode}:{struct.unpack('>H', payload[:2])[0]}:{payload[2:].decode()}")
            write_frame(connection, 8, payload)
        elif path == "/blocked":
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            accept(connection, headers)
            # leave the send queue full so its protocol-error close must take the five second timeout
            read_exact(connection, 1)
            connection.sendall(bytes([0x81, 0x81, 1, 2, 3, 4, ord("a") ^ 1]))
            blocked_release.wait(15)
        elif path == "/masked":
            accept(connection, headers)
            # a server frame with the mask bit set breaks the protocol
            connection.sendall(bytes([0x81, 0x81, 1, 2, 3, 4, ord("a") ^ 1]))
            opcode, _, _, payload = read_frame(connection)
            record(f"masked:{opcode}:{struct.unpack('>H', payload[:2])[0]}")
    except (EOFError, ConnectionError, OSError):
        record("connection error")
    finally:
        connection.close()


def listen():
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    port = listener.getsockname()[1]

    def run():
        while True:
            try:
                connection, _ = listener.accept()
            except OSError:
                return
            connection.settimeout(10)
            threading.Thread(target=serve, args=(connection, port), daemon=True).start()
    threading.Thread(target=run, daemon=True).start()
    return listener, port


def listen_tls(directory):
    certificate = Path(directory) / "server.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(certificate), "-out", str(certificate),
                    "-subj", "/CN=127.0.0.1", "-days", "1"], check=True, capture_output=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(certificate))
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)

    def run():
        while True:
            try:
                connection, _ = listener.accept()
            except OSError:
                return
            connection.settimeout(10)
            try:
                with context.wrap_socket(connection, server_side=True) as secure:
                    secure.recv(1)
                    record("tls handshake accepted")
            except (ssl.SSLError, OSError):
                record("tls refused by the client")
    threading.Thread(target=run, daemon=True).start()
    return listener, listener.getsockname()[1]


def page(port, closed_port, tls_port):
    return f"""<!doctype html><meta charset=utf-8><title>waiting</title><script>
var log = [], got = [];
log.push('doc:' + document.cookie);
function done() {{ var request = new XMLHttpRequest(); request.open('GET', '/report?' + encodeURIComponent(log.join('|'))); request.send(); }}
function open(path, protocol) {{ return new WebSocket('ws://127.0.0.1:{port}' + path, protocol); }}
function step(name, path, then) {{
    var socket = open(path);
    socket.onerror = function () {{ log.push(name + '-error'); }};
    socket.onclose = function (event) {{ log.push(name + '-close:' + event.code + ':' + event.wasClean); then(); }};
    return socket;
}}
function repeatStep(name, path, count, next) {{
    function again() {{ step(name, path, function () {{ if (--count) again(); else next(); }}); }}
    again();
}}
var steps = [
    function (next) {{
        var a = open('/echo', 'chat');
        a.binaryType = 'arraybuffer';
        a.onopen = function () {{
            log.push('open:' + a.protocol + ':' + a.readyState);
            a.send('h\u00e9llo');
            a.send(new Uint8Array([1, 2, 3, 250]).buffer);
            a.send(new Blob(['blob!']));
            a.send('after');
            log.push('buffered:' + (a.bufferedAmount > 0));
        }};
        a.onmessage = function (event) {{
            got.push(typeof event.data === 'string' ? 't:' + event.data.length + ':' + event.data.slice(0, 8) : 'b:' + new Uint8Array(event.data).join(','));
        }};
        a.onclose = function (event) {{
            log.push('echo-close:' + event.code + ':' + event.reason + ':' + event.wasClean);
            log.push(got.join(';'));
            log.push('set-cookie:' + (document.cookie.indexOf('wsid=77') >= 0));
            next();
        }};
    }},
    function (next) {{
        var d = open('/deflate');
        var seen = [];
        d.binaryType = 'arraybuffer';
        d.onopen = function () {{ log.push('deflate-ext:' + d.extensions); d.send('A'.repeat(5000)); }};
        d.onmessage = function (event) {{ seen.push(typeof event.data === 'string' ? event.data.length + ':' + event.data[0] : 'bin'); }};
        d.onerror = function () {{ log.push('deflate-error'); }};
        d.onclose = function (event) {{ log.push('deflate:' + seen.join(',') + ':' + event.code); next(); }};
    }},
    function (next) {{
        var t = open('/takeover'), count = 0, text = ('lorem ipsum dolor sit amet ' + 'x'.repeat(200)), out = [];
        t.onopen = function () {{ t.send(text); t.send(text + '!'); }};
        t.onmessage = function (event) {{
            out.push(event.data === (count ? '!' : '') + text.split('').reverse().join(''));
            if (++count == 2) {{ log.push('takeover:' + out.join(',')); t.close(); }}
        }};
        t.onclose = function () {{ next(); }};
    }},
    function (next) {{
        var worker = new Worker('/worker.js?port={port}');
        worker.onmessage = function (event) {{ log.push('worker:' + event.data); worker.terminate(); next(); }};
        worker.onerror = function (event) {{ log.push('worker-error'); next(); }};
    }},
    function (next) {{ step('refuse', '/refuse', next); }},
    function (next) {{ repeatStep('utf8', '/badutf8', 8, next); }},
    function (next) {{ repeatStep('masked', '/masked', 8, next); }},
    function (next) {{
        var d = open('/blocked'), started;
        d.onopen = function () {{
            started = performance.now();
            d.send(new ArrayBuffer(8 * 1024 * 1024));
            d.send(new ArrayBuffer(8 * 1024 * 1024));
        }};
        d.onerror = function () {{ log.push('blocked-error'); }};
        d.onclose = function (event) {{
            log.push('blocked-close:' + event.code + ':' + event.wasClean);
            log.push('blocked-ms:' + Math.round(performance.now() - started)); next();
        }};
    }},
    function (next) {{
        var d = step('pageclose', '/pageclose', next);
        d.onopen = function () {{ d.close(1000, 'done'); }};
    }},
    function (next) {{
        // a certificate no root vouches for must not be accepted
        var t = new WebSocket('wss://127.0.0.1:{tls_port}/');
        t.onerror = function () {{ log.push('tls-error'); }};
        t.onclose = function (event) {{ log.push('tls-close:' + event.code + ':' + event.wasClean); next(); }};
    }},
    function (next) {{
        var e = new WebSocket('ws://127.0.0.1:{closed_port}/');
        e.onerror = function () {{ log.push('refused-error'); }};
        e.onclose = function (event) {{ log.push('refused-close:' + event.code); next(); }};
    }}
];
(function run(index) {{ if (index == steps.length) done(); else steps[index](function () {{ run(index + 1); }}); }})(0);
</script>""".encode()


def worker_script():
    return b"""var port = location.search.split('=')[1];
var socket = new WebSocket('ws://127.0.0.1:' + port + '/worker');
socket.onopen = function () { socket.send('hello'); };
socket.onmessage = function (event) { postMessage(event.data); };
socket.onerror = function () { postMessage('error'); };
"""


def main():
    binary = str(Path(sys.argv[1]).resolve())
    holder = tempfile.TemporaryDirectory(prefix="yozora-websocket-")
    directory = holder.name
    listener, socket_port = listen()
    tls_listener, tls_port = listen_tls(directory)
    # a port nothing listens on: bound once to get a free number, then released
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    closed_port = probe.getsockname()[1]
    probe.close()
    body = page(socket_port, closed_port, tls_port)

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, *arguments):
            pass

        def do_GET(self):
            if self.path.startswith("/report?"):
                reports.append(urllib.parse.unquote(self.path.split("?", 1)[1]))
                self.send_response(200)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path.startswith("/worker.js"):
                script = worker_script()
                self.send_response(200)
                self.send_header("Content-Type", "text/javascript")
                self.send_header("Content-Length", str(len(script)))
                self.end_headers()
                self.wfile.write(script)
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Set-Cookie", "session=abc123; Path=/")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    environment = dict(os.environ, YOZORA_RENDER_SETTLE_MS="9000")
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                             f"http://127.0.0.1:{server.server_address[1]}/", str(Path(directory) / "page.png")],
                            capture_output=True, text=True, timeout=90, env=environment)
    blocked_release.set()
    assert result.returncode == 0, result
    assert reports, result.stdout + result.stderr
    title = reports[-1]
    duration = re.search(r"\|blocked-ms:(\d+)", title)
    assert duration and 4000 <= int(duration[1]) < 8000, title
    title = re.sub(r"\|blocked-ms:\d+", "", title)
    expected = ["doc:session=abc123", "open:chat:1", "buffered:true", "echo-close:4001:bye:true",
                "t:5:héllo;b:1,2,3,250;t:8:fragment;t:100000:xxxxxxxx", "set-cookie:true",
                "deflate-ext:permessage-deflate; client_no_context_takeover; server_no_context_takeover",
                "deflate-error", "deflate:20000:B,4:t:1006", "takeover:true,true", "worker:from-server",
                "refuse-error", "refuse-close:1006:false", *(["utf8-error", "utf8-close:1006:false"] * 8),
                *(["masked-error", "masked-close:1006:false"] * 8), "blocked-error", "blocked-close:1006:false", "pageclose-close:1000:true", "tls-error", "tls-close:1006:false",
                "refused-error", "refused-close:1006"]
    # the accepted parameters come back in the order of a hash table
    title = title.replace("permessage-deflate; server_no_context_takeover; client_no_context_takeover", "permessage-deflate; client_no_context_takeover; server_no_context_takeover")
    assert title.split("|") == expected, title
    with lock:
        seen = list(events)
    port = server.server_address[1]
    assert f"origin:http://127.0.0.1:{port}" in seen, seen
    assert "cookie:session=abc123" in seen, seen
    assert "offered:chat" in seen, seen
    assert any(item.startswith("extensions:permessage-deflate") for item in seen), seen
    assert "text:68c3a96c6c6f:masked=True:final=True" in seen, seen
    assert "binary:010203fa:masked=True:final=True" in seen, seen
    assert "binary:626c6f6221:masked=True:final=True" in seen, seen
    assert "text:6166746572:masked=True:final=True" in seen, seen
    assert seen.index("binary:626c6f6221:masked=True:final=True") < seen.index("text:6166746572:masked=True:final=True"), seen
    assert any(item.startswith("deflate-offer:permessage-deflate") for item in seen), seen
    assert any(item.startswith("deflate-in:True:") and item.endswith("<5000:AAA") for item in seen), seen
    assert "bomb:8:1009" in seen, seen
    assert "takeover-in:True:227" in seen and "takeover-in:True:228" in seen, seen
    assert "worker:hello" in seen, seen
    assert "pong:10:p1" in seen, seen
    assert "close:8:4001" in seen, seen
    assert seen.count("badutf8:8:1007") == 8, seen
    assert seen.count("masked:8:1002") == 8, seen
    assert "pageclose:8:1000:done" in seen, seen
    assert "refused" in seen, seen
    assert "tls refused by the client" in seen and "tls handshake accepted" not in seen, seen
    server.shutdown()
    listener.close()
    tls_listener.close()
    holder.cleanup()
    print("core websocket: handshake with cookie and origin, masked text and binary, fragments around a ping, a 100 KB message, close codes both ways, refusal, bad UTF-8 and a masked server frame, an untrusted wss certificate, Set-Cookie, a Blob in order, permessage-deflate with a decompression bomb and a worker passed")


if __name__ == "__main__":
    main()
