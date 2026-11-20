from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# a carousel like the YouTube Shorts feed: the feed has no scroll of its own, the page follows the finger with
# a transform and calls preventDefault so the browser does not scroll. the title carries what the page saw
CAROUSEL = """<!doctype html><meta charset=utf-8><body style='margin:0;overflow:hidden'>
<div id=wrap style='height:600px;overflow:hidden'><div id=list style='height:3000px;background:#468'>feed</div></div>
<script>
var wrap=document.getElementById('wrap'),list=document.getElementById('list'),y0=0,offset=0,log={s:0,m:0,e:0,mouse:0};
function show(){document.title='s='+log.s+' m='+log.m+' e='+log.e+' mouse='+log.mouse+' offset='+Math.round(offset)}
wrap.addEventListener('touchstart',function(e){log.s++;y0=e.touches[0].clientY;if(e.cancelable)e.preventDefault();show()},{passive:false});
wrap.addEventListener('touchmove',function(e){log.m++;offset=e.touches[0].clientY-y0;list.style.transform='translateY('+offset+'px)';e.preventDefault();show()},{passive:false});
wrap.addEventListener('touchend',function(e){log.e++;show()});
document.addEventListener('mousedown',function(){log.mouse++;show()});
show();
</script>"""
# a page that only watches: nothing is prevented, so the interface keeps scrolling the page
WATCHER = """<!doctype html><meta charset=utf-8><body style='margin:0'>
<div style='height:3000px;background:#684'>page</div>
<script>
var log={s:0,m:0,e:0,mouse:0};
function show(){document.title='s='+log.s+' m='+log.m+' e='+log.e+' mouse='+log.mouse}
document.addEventListener('touchstart',function(){log.s++;show()},{passive:true});
document.addEventListener('touchmove',function(){log.m++;show()},{passive:true});
document.addEventListener('touchend',function(){log.e++;show()});
document.addEventListener('mousedown',function(){log.mouse++;show()});
show();
</script>"""
PAGES = {"/carousel": CAROUSEL, "/watcher": WATCHER}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = PAGES[self.path].encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def drag(binary, url, directory, gesture):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "touch.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_TOUCH=gesture))
    assert result.returncode == 0, result
    claimed = re.search(r"touch claimed (\d+)", result.stderr)
    title = re.search(r"yozora-render: title (.*)", result.stderr)
    assert claimed and title, result.stderr
    fields = dict(part.split("=") for part in title.group(1).split())
    return int(claimed.group(1)), {key: int(value) for key, value in fields.items()}


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    with tempfile.TemporaryDirectory(prefix="yozora-touch-") as directory:
        # a finger goes down at 300 and drags up by 20 points ten times: the carousel follows and claims the gesture
        claimed, seen = drag(binary, base + "/carousel", directory, "100,300,10,-20")
        assert claimed == 1, (claimed, seen)
        assert seen["s"] == 1 and seen["m"] == 10 and seen["e"] == 1, seen
        assert seen["offset"] == -200, seen
        # touch events are not mouse events: a drag must not press a mouse button, or it would select text under the finger
        assert seen["mouse"] == 0, seen
        # a page that does not prevent anything gets the events and leaves the scrolling to the interface
        claimed, seen = drag(binary, base + "/watcher", directory, "100,300,10,-20")
        assert claimed == 0, (claimed, seen)
        assert seen["s"] == 1 and seen["m"] == 10 and seen["e"] == 1 and seen["mouse"] == 0, seen
    server.shutdown()
    print("core touch: a carousel followed 10 touchmove events and claimed the gesture, a page that prevents nothing did not, no mouse events")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_touch.py yozora-render")
    main()
