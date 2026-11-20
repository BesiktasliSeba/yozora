from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent

# the video sits at (10,10) to (210,110), the buttons below it. the webm source comes first and must be skipped
PAGES = {
    "/": b"""<!doctype html><html><head><meta charset=utf-8><title>media</title>
<style>body{margin:0}button{position:absolute;left:10px;width:120px;height:30px;margin:0}</style></head><body>
<video id=v controls width=200 height=100 style="position:absolute;left:10px;top:10px" poster="/poster.png">
<source src="/a.webm" type="video/webm"><source src="/clip.mp4" type='video/mp4; codecs="avc1.42E01E, mp4a.40.2"'>Your browser cannot play this video</video>
<button style="top:150px" onclick="v.pause()">pause</button>
<button style="top:200px" onclick="v.currentTime=42">seek</button>
<button style="top:250px" onclick="new Audio('/song.mp3').play()">audio</button>
<button style="top:300px" onclick="v.play()">play</button>
<button style="top:350px" onclick="v.volume=0.25;v.muted=true;v.playbackRate=1.5">settings</button>
<script>
var v = document.getElementById('v');
function show() { document.title = 'p=' + v.paused + ' t=' + v.currentTime.toFixed(1) + ' d=' + v.duration + ' e=' + v.ended; }
['play', 'playing', 'pause', 'timeupdate', 'durationchange', 'ended'].forEach(function (name) { v.addEventListener(name, show); });
v.addEventListener('error', function () { document.title = 'error:' + v.error.code; });
</script></body></html>""",
    "/blocked": b"""<!doctype html><title>start</title><video id=v controls src="/clip.mp4"></video>
<script>document.getElementById('v').play().then(function () { document.title = 'played'; }, function (e) { document.title = 'blocked:' + e.name; });</script>""",
    "/types": b"""<!doctype html><title>start</title><video id=v></video><script>
var v = document.getElementById('v');
document.title = [v.canPlayType('video/mp4'), v.canPlayType('video/mp4; codecs="avc1.42E01E"'), v.canPlayType('video/webm'),
    v.canPlayType('application/vnd.apple.mpegurl'), v.canPlayType('audio/ogg; codecs=opus'), v instanceof HTMLVideoElement,
    document.createElement('audio') instanceof HTMLAudioElement, typeof v.play].join('|');</script>""",
    "/unsupported": b"""<!doctype html><title>start</title><video id=v controls style="position:absolute;left:10px;top:10px;width:200px;height:100px"><source src="/a.webm" type="video/webm"></video>
<script>var v = document.getElementById('v'); setTimeout(function(){document.title='network:'+v.networkState},100);</script>""",
    "/scheme": b"""<!doctype html><title>start</title><video id=v controls src="file:///etc/passwd" style="position:absolute;left:10px;top:10px;width:200px;height:100px"></video>
<script>var v = document.getElementById('v'); v.addEventListener('error', function () { document.title = 'error:' + v.error.code; });</script>""",
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = PAGES.get(self.path, b"")
        self.send_response(200 if body else 404)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def render(binary, port, path, actions):
    with tempfile.TemporaryDirectory(prefix="yozora-media-") as directory:
        environment = dict(os.environ, YOZORA_RENDER_ACTIONS=actions)
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}{path}", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=90, env=environment)
    assert result.returncode == 0, result
    lines = result.stderr.splitlines()
    commands = [line[len("yozora-render: media command "):] for line in lines if line.startswith("yozora-render: media command ")]
    title = [line[len("yozora-render: title "):] for line in lines if line.startswith("yozora-render: title ")][-1]
    return commands, title


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    origin = f"http://127.0.0.1:{port}"

    def fields(commands):
        result = []
        for command in commands:
            match = re.fullmatch(r"(\d+) id (\d+) video (\d+) value (\S+) muted (\d+) url (.*?) title (.*)", command)
            assert match, command
            result.append((int(match[1]), int(match[2]), int(match[3]), float(match[4]), int(match[5]), match[6], match[7]))
        return result

    # metadata loads open a player before playback, the native controls wait for its readiness
    ready = "tap:100,60;media:1,5,12.5;media:1,7,200,100;media:1,8,1;media:1,9,0"
    commands, title = render(binary, port, "/", ready + ";tap:60,315")
    parsed = fields(commands)
    assert parsed[0] == (1, 1, 1, 0, 0, origin + "/clip.mp4", "media"), parsed
    assert (5, 1, 0, 1, 0, "", "") in parsed, parsed
    assert any(command[:4] == (2, 1, 0, 1) for command in parsed), parsed
    # time progresses while playing, pausing before the time event gives an exact observable position
    commands, title = render(binary, port, "/", ready + ";tap:60,315;media:1,1,0;media:1,2,0;media:1,4,3.5")
    assert title == "p=true t=3.5 d=12.5 e=false", title
    # script pause and seek address the same player; seeks need a known seekable duration
    commands, title = render(binary, port, "/", ready + ";tap:60,315;tap:60,165;tap:60,215;media:1,10,12.5")
    parsed = fields(commands)
    assert any(command[:4] == (3, 1, 0, 0) for command in parsed), parsed
    assert any(command[:4] == (4, 1, 0, 12.5) for command in parsed), parsed
    # ended reflects the finite duration reported by the actual player
    commands, title = render(binary, port, "/", ready + ";tap:60,315;media:1,1,0;media:1,3,0")
    assert title == "p=true t=12.5 d=12.5 e=true", title
    # an audio element uses its own id while the video keeps its metadata player
    commands, title = render(binary, port, "/", "tap:60,265")
    assert any(command[0] == 1 and command[2] == 0 and command[5] == origin + "/song.mp3" for command in fields(commands)), commands
    # autoplay is refused, although preloading metadata is allowed
    commands, title = render(binary, port, "/blocked", "tap:300,400")
    assert not any(command[0] == 2 for command in fields(commands)) and title == "blocked:NotAllowedError", (commands, title)
    # only the formats the system plays are announced
    commands, title = render(binary, port, "/types", "tap:300,400")
    assert title == "maybe|probably||maybe||true|true|function", title
    # a page with nothing the system plays gets an error event and no player
    commands, title = render(binary, port, "/unsupported", "tap:100,60")
    assert commands == [] and title == "network:3", (commands, title)
    # addresses other than http and https never reach the player
    commands, title = render(binary, port, "/scheme", "tap:100,60")
    assert commands == [] and title == "error:4", (commands, title)

    server.shutdown()
    print("core media: sources, events, commands, gestures, formats and refused addresses passed")


if __name__ == "__main__":
    main()
