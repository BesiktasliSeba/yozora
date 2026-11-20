from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


def tone_wave(rate, seconds, frequency, bits=16, channels=1):
    frames = int(rate * seconds)
    body = bytearray()
    for n in range(frames):
        value = math.sin(2 * math.pi * frequency * n / rate)
        for _ in range(channels):
            if bits == 16:
                body += struct.pack("<h", int(value * 30000))
            else:
                body += struct.pack("<f", value * 0.9)
    fmt = 1 if bits == 16 else 3
    block = channels * bits // 8
    header = b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, fmt, channels, rate, rate * block, block, bits)
    return header + b"data" + struct.pack("<I", len(body)) + bytes(body)


FILES = {
    "/tone.wav": ("audio/wav", tone_wave(8000, 0.1, 440)),
    "/tone-float.wav": ("audio/wav", tone_wave(22050, 0.1, 1000, bits=32, channels=2)),
    # a header that claims more data than the file has, and bytes that are no audio at all
    "/short.wav": ("audio/wav", tone_wave(8000, 0.1, 440)[:200]),
    "/garbage.bin": ("application/octet-stream", bytes(range(256)) * 8),
}

PAGE = """<!doctype html><meta charset=utf-8><body><button id=b style='width:200px;height:100px'>go</button>
<script>
var R = {pending: 0};
function done(name, value) { R[name] = value; R.pending--; }
function start() { R.pending++; }
function fetchBuffer(url) {
    return new Promise(function (resolve, reject) {
        var x = new XMLHttpRequest();
        x.open('GET', url);
        x.responseType = 'arraybuffer';
        x.onload = function () { resolve(x.response); };
        x.onerror = function () { reject(new Error('fetch ' + url)); };
        x.send();
    });
}
function stats(data) {
    var peak = 0, crossings = 0, finite = true;
    for (var i = 0; i < data.length; i++) {
        if (!isFinite(data[i])) finite = false;
        if (Math.abs(data[i]) > peak) peak = Math.abs(data[i]);
        if (i && (data[i - 1] < 0) != (data[i] < 0)) crossings++;
    }
    return {peak: peak, crossings: crossings, finite: finite};
}
R.api = {AudioContext: typeof AudioContext, webkit: typeof webkitAudioContext, offline: typeof OfflineAudioContext, analyser: typeof AnalyserNode,
         convolver: typeof ConvolverNode, panner: typeof PannerNode, worklet: typeof AudioWorkletNode};
function offlineSine() {
    start();
    var c = new OfflineAudioContext(1, 44100, 44100);
    var o = c.createOscillator(), g = c.createGain();
    o.frequency.value = 440;
    g.gain.value = 0.5;
    o.connect(g); g.connect(c.destination); o.start();
    c.startRendering().then(function (b) { var s = stats(b.getChannelData(0)); s.length = b.length; s.rate = b.sampleRate; done('sine', s); }, function (e) { done('sine', 'error ' + e); });
}
function decodeTests() {
    start(); start(); start(); start();
    var c = new OfflineAudioContext(2, 44100, 44100);
    fetchBuffer('/tone.wav').then(function (data) { return c.decodeAudioData(data); }).then(function (b) {
        var s = stats(b.getChannelData(0)); s.length = b.length; s.channels = b.numberOfChannels; s.rate = b.sampleRate; done('wave', s);
    }, function (e) { done('wave', 'error ' + e); });
    fetchBuffer('/tone-float.wav').then(function (data) { return c.decodeAudioData(data); }).then(function (b) {
        var s = stats(b.getChannelData(1)); s.length = b.length; s.channels = b.numberOfChannels; done('float', s);
    }, function (e) { done('float', 'error ' + e); });
    fetchBuffer('/garbage.bin').then(function (data) { return c.decodeAudioData(data); }).then(function () { done('garbage', 'decoded'); }, function (e) { done('garbage', e.name); });
    fetchBuffer('/short.wav').then(function (data) { return c.decodeAudioData(data); }).then(function (b) { done('short', b.length); }, function (e) { done('short', e.name); });
}
function convolverTest() {
    start();
    var c = new OfflineAudioContext(1, 8192, 44100);
    var o = c.createOscillator(), v = c.createConvolver(), g = c.createGain();
    var impulse = c.createBuffer(1, 64, 44100);
    impulse.getChannelData(0)[0] = 1;
    v.normalize = false;
    v.buffer = impulse;
    g.gain.value = 0.5;
    o.frequency.value = 1000;
    o.connect(g); g.connect(v); v.connect(c.destination); o.start();
    c.startRendering().then(function (b) { done('convolver', stats(b.getChannelData(0))); }, function (e) { done('convolver', 'error ' + e); });
}
function pannerTest() {
    start();
    var c = new OfflineAudioContext(2, 8192, 44100);
    var o = c.createOscillator(), p = c.createPanner();
    p.panningModel = 'HRTF';
    p.positionX.value = 3;
    o.frequency.value = 500;
    o.connect(p); p.connect(c.destination); o.start();
    c.startRendering().then(function (b) { done('panner', {left: stats(b.getChannelData(0)).peak, right: stats(b.getChannelData(1)).peak}); }, function (e) { done('panner', 'error ' + e); });
}
function filterTest() {
    start();
    var c = new OfflineAudioContext(1, 8192, 44100);
    var o = c.createOscillator(), f = c.createBiquadFilter(), d = c.createDynamicsCompressor(), w = c.createWaveShaper();
    f.type = 'lowpass'; f.frequency.value = 800;
    w.curve = new Float32Array([-1, 0, 1]);
    o.type = 'sawtooth'; o.frequency.value = 300;
    o.connect(f); f.connect(d); d.connect(w); w.connect(c.destination); o.start();
    c.startRendering().then(function (b) { done('filters', stats(b.getChannelData(0))); }, function (e) { done('filters', 'error ' + e); });
}
// the probe of the test runs in an isolated world, so the results go to an attribute of the page
setInterval(function () { document.documentElement.setAttribute('data-r', JSON.stringify(R)); }, 50);
window.onload = function () { offlineSine(); decodeTests(); convolverTest(); pannerTest(); filterTest(); };
// a realtime context needs a gesture: the click of the test starts one, plays a tone that lies exactly on a bin of the analyser and reads the clock
document.getElementById('b').onclick = function () {
    start();
    var c = new AudioContext();
    R.created = c.state;
    var o = c.createOscillator(), a = c.createAnalyser();
    a.fftSize = 2048;
    o.frequency.value = 128 * c.sampleRate / 2048;
    o.connect(a); a.connect(c.destination); o.start();
    c.resume().then(function () {
        var t0 = c.currentTime;
        setTimeout(function () {
            var data = new Float32Array(a.frequencyBinCount);
            a.getFloatFrequencyData(data);
            var best = 0;
            for (var i = 1; i < data.length; i++) if (data[i] > data[best]) best = i;
            done('realtime', {state: c.state, rate: c.sampleRate, elapsed: c.currentTime - t0, bin: best, level: data[best], base: data[best + 40]});
        }, 1200);
    }, function (e) { done('realtime', 'error ' + e); });
};
</script>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path == "/":
            kind, body = "text/html; charset=utf-8", PAGE.encode()
        elif self.path in FILES:
            kind, body = FILES[self.path]
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def run(binary, url, directory):
    probe = Path(directory) / "probe.js"
    probe.write_text("(function(){return document.documentElement.getAttribute('data-r')})()")
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "audio.png"), "320", "480"],
                            capture_output=True, text=True, timeout=240,
                            env=dict(os.environ, YOZORA_RENDER_SETTLE_MS="2500", YOZORA_RENDER_ACTIONS="tap:100,50;wait:2200", YOZORA_RENDER_SCRIPT=str(probe)))
    assert result.returncode == 0, result
    found = re.search(r"script result (.*)", result.stderr)
    assert found, result.stderr
    return json.loads(found.group(1))


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="yozora-webaudio-") as directory:
        r = run(binary, f"http://127.0.0.1:{server.server_address[1]}/", directory)
    server.shutdown()
    assert r["pending"] == 0, ("tests did not finish", r)
    assert r["api"]["AudioContext"] == "function" and r["api"]["offline"] == "function" and r["api"]["analyser"] == "function", r["api"]
    # a sine of 440 Hz at a gain of 0.5 for one second: the amplitude and 880 zero crossings
    sine = r["sine"]
    assert sine["length"] == 44100 and sine["rate"] == 44100 and abs(sine["peak"] - 0.5) < 0.01 and abs(sine["crossings"] - 880) <= 2, sine
    # a wave of 8000 Hz comes out at the rate of the context, a stereo float wave keeps its channels
    wave = r["wave"]
    assert wave["channels"] == 1 and abs(wave["length"] - 4410) <= 2 and wave["peak"] > 0.8 and wave["finite"], wave
    assert abs(wave["crossings"] - 88) <= 3, wave
    assert r["float"]["channels"] == 2 and abs(r["float"]["length"] - 4410) <= 2 and r["float"]["peak"] > 0.8, r["float"]
    # what no decoder takes is an EncodingError, a header that promises more than the file has plays what is there
    assert r["garbage"] == "EncodingError", r["garbage"]
    # 200 bytes of file hold 78 frames at 8000 Hz, which is 430 at the rate of the context
    assert isinstance(r["short"], int) and abs(r["short"] - 430) <= 3, r["short"]
    # a convolver with a single sample is the signal itself: the transform of the port, forward and inverse, is exact enough
    conv = r["convolver"]
    assert conv["finite"] and abs(conv["peak"] - 0.5) < 0.02 and abs(conv["crossings"] - 1000 * 8192 / 44100 * 2) < 6, conv
    # a source on the right is louder in the right ear: the HRTF database was loaded
    panner = r["panner"]
    assert panner["right"] > 0.05 and panner["right"] > panner["left"] * 1.2, panner
    assert r["filters"]["finite"] and r["filters"]["peak"] > 0.01, r["filters"]
    # the clock of a context follows the frames the device asked for, and the analyser sees the tone on its bin
    realtime = r["realtime"]
    assert isinstance(realtime, dict), realtime
    assert realtime["state"] == "running" and 0.9 < realtime["elapsed"] < 1.6, realtime
    assert abs(realtime["bin"] - 128) <= 1 and realtime["level"] - realtime["base"] > 40, realtime
    print("core webaudio: oscillator, gain, decodeAudioData (wave 16 bit, float, refused files), convolver, HRTF panner, filters, analyser and the clock of a realtime context passed")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_webaudio.py yozora-render")
    main()
