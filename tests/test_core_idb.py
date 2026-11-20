from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# every run opens the same database, adds one record and reports the count, the index lookup and a transaction
# that aborts
PAGE = """<!doctype html><meta charset=utf-8><title>start</title><script>
var out={};function done(){document.title='count='+out.count+' hits='+out.hits+' aborted='+out.aborted+' version='+out.version}
var open=indexedDB.open('notes',2);
open.onupgradeneeded=function(e){var db=open.result;if(e.oldVersion<1){var s=db.createObjectStore('rows',{keyPath:'id',autoIncrement:true});s.createIndex('kind','kind')}};
open.onerror=function(){document.title='error '+open.error};
open.onsuccess=function(){var db=open.result;out.version=db.version;
 var tx=db.transaction('rows','readwrite');tx.objectStore('rows').add({kind:'note',text:'row'});
 tx.oncomplete=function(){
  var t2=db.transaction('rows','readonly');var s=t2.objectStore('rows');
  s.count().onsuccess=function(e){out.count=e.target.result};
  s.index('kind').getAll('note').onsuccess=function(e){out.hits=e.target.result.length};
  t2.oncomplete=function(){
   var t3=db.transaction('rows','readwrite');t3.objectStore('rows').add({kind:'lost'});t3.onabort=function(){
     var t4=db.transaction('rows','readonly');t4.objectStore('rows').index('kind').getAll('lost').onsuccess=function(e){out.aborted=e.target.result.length};
     t4.oncomplete=function(){db.close();done()}};t3.abort()}}};
</script>""".encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)


def run(binary, url, data, clear=False):
    environment = dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_SETTLE_MS="800")
    if data:
        environment["YOZORA_RENDER_SITE_DATA"] = str(data)
    if clear:
        environment["YOZORA_RENDER_CLEAR_SITE_DATA"] = "1"
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, "/dev/null", "320", "480"],
                            capture_output=True, text=True, timeout=120, env=environment)
    assert result.returncode == 0, result
    found = re.findall(r"yozora-render: title (.*)", result.stderr)
    assert found, result.stderr
    return found[-1]


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    other = f"http://localhost:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-idb-") as directory:
        data = Path(directory) / "profile"
        data.mkdir()
        # without a folder the database lives for one run
        assert run(binary, url, None) == "count=1 hits=1 aborted=0 version=2"
        assert run(binary, url, None) == "count=1 hits=1 aborted=0 version=2"
        # with one it stays: a second run finds the record of the first, an aborted transaction leaves nothing
        assert run(binary, url, data) == "count=1 hits=1 aborted=0 version=2"
        assert run(binary, url, data) == "count=2 hits=2 aborted=0 version=2"
        assert (data / "IndexedDB").is_dir()
        # another origin has databases of its own
        assert run(binary, other, data) == "count=1 hits=1 aborted=0 version=2"
        # clearing the site data deletes them
        assert run(binary, url, data, clear=True) == "count=1 hits=1 aborted=0 version=2"
    server.shutdown()
    print("core idb: a database keeps its records between runs, an abort leaves nothing, origins are apart, clear site data deletes them")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_idb.py yozora-render")
    main()
