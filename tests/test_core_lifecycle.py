from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tests'))
from test_core_loading import read_png


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path == '/busy-idb':
            body = b'''<!doctype html><title>idb-starting</title><script>
let request=indexedDB.open('freeze',1);
request.onupgradeneeded=()=>request.result.createObjectStore('store');
request.onsuccess=()=>{let transaction=request.result.transaction('store','readwrite');
 let store=transaction.objectStore('store');function again(){store.put('busy',1).onsuccess=again}
 again();document.title='idb-busy'};</script>'''
        elif self.path in ('/idb-open', '/idb-closed', '/idb-aborted'):
            mode = self.path[1:]
            body = ('''<!doctype html><title>idb-starting</title><script>
let request=indexedDB.open('freeze-idle',1);
request.onupgradeneeded=()=>{request.result.createObjectStore('store');
 if('MODE'==='idb-aborted')request.transaction.abort()};
request.onerror=event=>{event.preventDefault();document.title='idb-aborted'};
request.onsuccess=()=>{let db=request.result,transaction=db.transaction('store','readwrite');
 window.savedDatabase=db;window.savedTransaction=transaction;transaction.objectStore('store').put('done',1);
 transaction.oncomplete=()=>{if('MODE'==='idb-closed')db.close();document.title='MODE'}};
</script>'''.replace('MODE', mode)).encode()
        elif self.path == '/wake':
            body = b'''<!doctype html><body style="background:red"><script>
setTimeout(()=>{let last=performance.now(),delays=[];function frame(t){
 delays.push(Math.round(t-last));last=t;
 if(delays.length<8)requestAnimationFrame(frame);else{
  document.body.style.background='blue';console.log('wake-frames '+JSON.stringify(delays));
 }}requestAnimationFrame(frame)},300)</script>'''
        elif self.path == '/media':
            body = b'<!doctype html><video preload=metadata src="/clip.mp4"></video>'
        elif self.path == '/form':
            body = b'''<!doctype html><style>body{margin:0;background:red}input{width:200px;height:40px}</style>
<input id=field><script>let count=0,hiddenTicks=0;setInterval(()=>{
 if(document.hidden)++hiddenTicks;
 document.title=field.value+'|'+(++count)+'|'+document.hidden+'|hidden='+hiddenTicks;
},50);document.onvisibilitychange=()=>{if(!document.hidden)document.body.style.background='#0000ff'}</script>'''
        else:
            body = b'<!doctype html><title>quiet</title><body style="margin:0;background:blue">quiet'
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def render(binary, url, directory, actions=None, back_via=None, scale=None):
    environment = dict(os.environ, YOZORA_RENDER_SETTLE_MS='1500')
    if scale is not None:
        environment['YOZORA_RENDER_SCALE'] = str(scale)
    if back_via:
        environment['YOZORA_RENDER_BACK_VIA'] = back_via
    if actions:
        environment.update(YOZORA_RENDER_ACTIONS=actions, YOZORA_RENDER_SETTLE_MS='400')
    result = subprocess.run([binary, str(ROOT / 'app/fonts'), str(ROOT / 'app/ca-bundle.crt'),
                             url, str(directory / 'page.png')],
                            capture_output=True, text=True, timeout=45, env=environment)
    assert result.returncode == 0, result.stderr
    assert (directory / 'page.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), result.stderr
    return result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        with tempfile.TemporaryDirectory(prefix='yozora-lifecycle-') as directory:
            directory = Path(directory)
            quiet = render(binary, base + '/', directory)
            passes = int(re.search(r'core thread passes (\d+)', quiet)[1])
            # a perpetual 60 Hz tick needs at least 90 passes during the settle interval alone
            assert 5 <= passes < 45, quiet
            resized = render(binary, base + '/', directory, 'resize;resize;resize')
            pattern = r'(\d+) rendering updates \d+ ms, (\d+) layouts'
            assert re.search(pattern, quiet).groups() == re.search(pattern, resized).groups(), (quiet, resized)
            economy = render(binary, base + '/', directory, 'economy:1;economy:1', scale=2)
            assert 'tile scales 1 to 1' in economy, economy
            restored_scale = render(binary, base + '/', directory, 'economy:1;economy:0', scale=2)
            assert 'tile scales 2 to 2' in restored_scale, restored_scale
            form = render(binary, base + '/form', directory,
                          f'tap:30,20;type:kept;suspend;tab:{base}/;suspend;resume;resume', back_via=base + '/')
            before = re.findall(r'yozora-render: suspend accepted 1 title (kept\|\d+\|false)', form)
            frozen = re.findall(r'yozora-render: after suspend title (kept\|\d+\|false)', form)
            assert len(before) == 2 and before == frozen and before[0] == before[1], form
            peer = re.search(r'second tab title quiet passes (\d+)', form)
            assert peer and int(peer[1]) > 0, form
            resumed = re.findall(r'yozora-render: after resume title kept\|(\d+)\|false', form)
            assert len(resumed) == 2 and int(resumed[0]) > int(before[0].split('|')[1]), form
            assert int(resumed[1]) > int(resumed[0]), form
            assert not re.search(r'hidden=[1-9]', form), form
            assert base + '/form back=0 forward=1' in form, form
            _, _, rows = read_png(directory / 'page.png')
            assert bytes(rows[200][200 * 4:201 * 4]) == bytes([0, 0, 255, 255]), (bytes(rows[200][200 * 4:201 * 4]), form)
            media = render(binary, base + '/media', directory, 'suspend;suspend')
            assert media.count('suspend accepted 0') == 2, media
            busy_idb = render(binary, base + '/busy-idb', directory, 'suspend;suspend')
            assert busy_idb.count('suspend accepted 0 title idb-busy') == 2, busy_idb
            idle_idb = render(binary, base + '/idb-open', directory, 'suspend;suspend')
            assert idle_idb.count('suspend accepted 0 title idb-open') == 2, idle_idb
            # a closed database must not prevent suspension while its finished transaction wrapper stays alive
            for mode in ('idb-closed', 'idb-aborted'):
                released_idb = render(binary, base + '/' + mode, directory, 'suspend;resume;suspend')
                assert released_idb.count('suspend accepted 1 title ' + mode) == 2, released_idb
            awakened = render(binary, base + '/wake', directory)
            times = re.search(r'wake-frames \[([0-9,]+)\]', awakened)
            assert times, awakened
            delays = [int(value) for value in times[1].split(',')]
            assert len(delays) == 8 and delays[0] < 80 and sum(delays) < 400, awakened
            _, _, rows = read_png(directory / 'page.png')
            assert bytes(rows[200][200 * 4:201 * 4]) == bytes([0, 0, 255, 255]), awakened
            # teardown of a suspended document must cancel its timers without resuming the page
            render(binary, base + '/form', directory, 'suspend')
    finally:
        server.shutdown()
        server.server_close()
    print(f'core lifecycle: {passes} quiet passes, unchanged resize, live economy scale, form state, suspended timers, independent tab, resumed pixels, media and indexeddb refusal, animation wake and suspended teardown passed')


if __name__ == '__main__':
    main()
