"""WebGL 1 and WebGL 2 over the OpenGL ES of the host (Mesa): the page draws, reads its pixels back, and the canvas shows in the picture of the page"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import zlib

ROOT = Path(__file__).resolve().parent.parent

WEBGL1 = """<!doctype html><meta charset=utf-8><body style='margin:0;background:#fff'>
<canvas id=c width=64 height=64 style='width:128px;height:128px'></canvas>
<script>
const gl = document.getElementById('c').getContext('webgl');
if (!gl) { console.log('webgl1 FAIL no context'); }
else {
  const compile = (type, source) => { const s = gl.createShader(type); gl.shaderSource(s, source); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) console.log('webgl1 FAIL shader ' + gl.getShaderInfoLog(s)); return s; };
  const program = gl.createProgram();
  gl.attachShader(program, compile(gl.VERTEX_SHADER, 'attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }'));
  gl.attachShader(program, compile(gl.FRAGMENT_SHADER, 'precision mediump float; uniform vec4 color; void main() { gl_FragColor = color; }'));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) console.log('webgl1 FAIL link ' + gl.getProgramInfoLog(program));
  gl.useProgram(program);
  gl.clearColor(0, 0, 1, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  const buffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1]), gl.STATIC_DRAW);
  const location = gl.getAttribLocation(program, 'p');
  gl.enableVertexAttribArray(location);
  gl.vertexAttribPointer(location, 2, gl.FLOAT, false, 0, 0);
  gl.uniform4f(gl.getUniformLocation(program, 'color'), 1, 0, 0, 1);
  // the left half of the canvas only
  gl.enable(gl.SCISSOR_TEST);
  gl.scissor(0, 0, 32, 64);
  gl.drawArrays(gl.TRIANGLES, 0, 6);
  gl.disable(gl.SCISSOR_TEST);
  const pixel = new Uint8Array(4);
  gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  const right = new Uint8Array(4);
  gl.readPixels(56, 56, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, right);
  const error = gl.getError();
  const ok = pixel[0] === 255 && pixel[1] === 0 && pixel[2] === 0 && right[0] === 0 && right[2] === 255 && error === 0;
  console.log('webgl1 ' + (ok ? 'ok' : 'FAIL ' + Array.from(pixel) + ' ' + Array.from(right) + ' error ' + error)
    + ' version ' + gl.getParameter(gl.VERSION) + ' extensions ' + gl.getSupportedExtensions().length);
}
</script></body>"""

WEBGL2 = """<!doctype html><meta charset=utf-8><body style='margin:0;background:#fff'>
<canvas id=c width=64 height=64 style='width:128px;height:128px'></canvas>
<script>
const gl = document.getElementById('c').getContext('webgl2');
if (!gl) { console.log('webgl2 FAIL no context'); }
else {
  const compile = (type, source) => { const s = gl.createShader(type); gl.shaderSource(s, source); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) console.log('webgl2 FAIL shader ' + gl.getShaderInfoLog(s)); return s; };
  const program = gl.createProgram();
  gl.attachShader(program, compile(gl.VERTEX_SHADER, `#version 300 es
    in vec2 p; in float shift; void main() { gl_Position = vec4(p.x * 0.25 + shift, p.y, 0.0, 1.0); }`));
  gl.attachShader(program, compile(gl.FRAGMENT_SHADER, `#version 300 es
    precision mediump float; out vec4 color; void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }`));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) console.log('webgl2 FAIL link ' + gl.getProgramInfoLog(program));
  gl.useProgram(program);
  const vao = gl.createVertexArray();
  gl.bindVertexArray(vao);
  const quad = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, quad);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1]), gl.STATIC_DRAW);
  const p = gl.getAttribLocation(program, 'p');
  gl.enableVertexAttribArray(p);
  gl.vertexAttribPointer(p, 2, gl.FLOAT, false, 0, 0);
  const shifts = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, shifts);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-0.5, 0.5]), gl.STATIC_DRAW);
  const shift = gl.getAttribLocation(program, 'shift');
  gl.enableVertexAttribArray(shift);
  gl.vertexAttribPointer(shift, 1, gl.FLOAT, false, 0, 0);
  gl.vertexAttribDivisor(shift, 1);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  // two instances, each a quarter of the width, one on each side of the middle
  gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, 2);
  // an immutable 3D texture and a uint readback prove the ES 3.0 entry points
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, texture);
  gl.texStorage3D(gl.TEXTURE_3D, 1, gl.RGBA8, 2, 2, 2);
  const left = new Uint8Array(4), middle = new Uint8Array(4), right = new Uint8Array(4);
  gl.readPixels(16, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, left);
  gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, middle);
  gl.readPixels(48, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, right);
  const error = gl.getError();
  const ok = left[1] === 255 && right[1] === 255 && middle[1] === 0 && error === 0;
  console.log('webgl2 ' + (ok ? 'ok' : 'FAIL ' + Array.from(left) + ' ' + Array.from(middle) + ' ' + Array.from(right) + ' error ' + error)
    + ' version ' + gl.getParameter(gl.VERSION) + ' extensions ' + gl.getSupportedExtensions().length);
}
</script></body>"""

PAGES = {"/webgl1.html": WEBGL1, "/webgl2.html": WEBGL2}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        page = PAGES.get(self.path)
        if page is None:
            self.send_error(404)
            return
        body = page.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset, idat, width, height, kind = 8, b"", 0, 0, 0
    while offset < len(data):
        length, name = struct.unpack(">I4s", data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        if name == b"IHDR":
            width, height, depth, kind = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and kind in (2, 6)
        elif name == b"IDAT":
            idat += body
        offset += length + 12
    channels = 4 if kind == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 255
            elif filter_type == 2:
                line[i] = (line[i] + up) & 255
            elif filter_type == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif filter_type == 4:
                estimate = left + up - corner
                distance = [abs(estimate - left), abs(estimate - up), abs(estimate - corner)]
                best = (left, up, corner)[distance.index(min(distance))]
                line[i] = (line[i] + best) & 255
        rows.append(line)
        previous = line
    return width, height, channels, rows


def pixel(image, x, y):
    _, _, channels, rows = image
    return tuple(rows[y][x * channels:x * channels + 3])


def run(binary, port, name, directory):
    output = Path(directory) / (name + ".png")
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}/{name}.html", str(output), "256", "256"],
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr[-2000:]
    return result.stderr + result.stdout, output


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    with tempfile.TemporaryDirectory() as directory:
        for name in ("webgl1", "webgl2"):
            log, output = run(binary, port, name, directory)
            lines = [line for line in log.splitlines() if name in line and "console" in line]
            assert any(f"{name} ok" in line for line in lines), (name, lines or log[-1500:])
            image = read_png(output)
            # the canvas stands at the top left, 128 by 128 points
            if name == "webgl1":
                assert pixel(image, 20, 64) == (255, 0, 0), pixel(image, 20, 64)
                assert pixel(image, 108, 64) == (0, 0, 255), pixel(image, 108, 64)
            else:
                assert pixel(image, 32, 64) == (0, 255, 0), pixel(image, 32, 64)
                assert pixel(image, 64, 64) == (0, 0, 0), pixel(image, 64, 64)
                assert pixel(image, 96, 64) == (0, 255, 0), pixel(image, 96, 64)
            print(lines[0].split("console:")[-1].strip())
    server.shutdown()
    print("webgl: contexts, shaders, instancing, readback and the canvas in the page picture passed")


if __name__ == "__main__":
    main()
