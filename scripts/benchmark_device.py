import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description="measure an already staged Yozora renderer over SSH")
parser.add_argument("--host", required=True, help="SSH account and device address")
parser.add_argument("--directory", required=True, help="private device directory containing the renderer")
parser.add_argument("--binary", default="yozora-render")
parser.add_argument("--resources", default="/Applications/Yozora.app")
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--suite", choices=["local", "sites"], default="local")
parser.add_argument("--repeat", type=int)
parser.add_argument("--case", action="append", choices=["blank", "article", "cards", "cards-scale1", "script", "dom", "photo", "gc", "google", "claude", "gemini", "telegram"])
parser.add_argument("--remote-port", type=int, default=49181)
args = parser.parse_args()
if not args.directory.startswith("/") or ".." in Path(args.directory).parts or args.directory.endswith("/"):
    parser.error("--directory must be an absolute device path without dot components or a trailing slash")
if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.binary):
    parser.error("--binary must be a file name inside --directory")
if args.repeat is not None and not 1 <= args.repeat <= 5:
    parser.error("--repeat must be between 1 and 5")
if not 1024 <= args.remote_port <= 65535:
    parser.error("--remote-port must be between 1024 and 65535")
OUT = args.output
OUT.mkdir(parents=True, exist_ok=True)
DEVICE = args.directory
HOST = args.host
SSH = (['sshpass', '-e'] if 'SSHPASS' in os.environ else []) + ['ssh', '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlgorithms=+ssh-rsa', '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=8', '-o', 'ServerAliveInterval=10', '-o', 'ServerAliveCountMax=2']
PAGES = {
    '/blank': b'<!doctype html><title>blank</title><body style="background:#eef">hello',
    '/article': ('<!doctype html><title>article</title><body style="margin:8px;font:16px sans-serif">' + ''.join(f'<h2>Paragraph {i}</h2><p>' + 'Text for layout and scrolling. ' * 20 + '</p>' for i in range(100))).encode(),
    '/cards': ('<!doctype html><title>cards</title><style>body{margin:0;background:#ddd;font:16px sans-serif}.card{margin:12px;padding:12px;height:90px;background:white;border-radius:10px;box-shadow:0 3px 10px #555}</style>' + ''.join(f'<div class=card><b>Card {i}</b><p>Text and a shadow</p></div>' for i in range(120))).encode(),
    '/script': b'''<!doctype html><title>script</title><body><script>const start=performance.now();let value=0;for(let i=0;i<2000000;i++)value=(value+Math.imul(i,17))|0;console.log('arithmetic_ms='+Math.round(performance.now()-start));document.title='script:'+value;</script>done''',
    '/dom': b'''<!doctype html><title>dom</title><body><script>const start=performance.now();for(let i=0;i<600;i++){let e=document.createElement('div');e.textContent='row '+i;e.style.cssText='padding:4px;border:1px solid #ccc';document.body.appendChild(e);if(i%20===0)void e.offsetHeight;}console.log('dom_ms='+Math.round(performance.now()-start));document.title='dom done';</script>''',
    '/photo': b'<!doctype html><title>photo</title><body style="margin:0"><img src="/large.jpg" width=320 height=240>',
}

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/large.jpg':
            body=(ROOT/'tests/data/gradient-3200x2400.jpg').read_bytes()
            mime='image/jpeg'
        elif self.path == '/gc':
            body=(ROOT/'tests/js/gc-garbage.html').read_bytes()
            mime='text/html'
        elif self.path in PAGES:
            body=PAGES[self.path]
            mime='text/html; charset=utf-8'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type',mime)
        self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store')
        self.end_headers()
        self.wfile.write(body)


def parse(result, name, seconds):
    record={'case':name,'status':result.returncode,'ssh_wall_s':round(seconds,3)}
    for prefix in ['timing','resources']:
        match=re.search(r'yozora-render: '+prefix+r' ([^\n]+)',result.stderr)
        if match:
            record.update({key:int(value) for key,value in re.findall(r'(\w+)=(-?\d+)',match[1])})
    phases=re.search(r'events (\d+) ms, (\d+) rendering updates (\d+) ms, (\d+) layouts, layout (\d+) ms, paint (\d+) ms in (\d+) paints',result.stderr)
    if phases:
        record.update(zip(['events_ms','render_updates','render_ms','layouts','layout_ms','paint_ms','paints'],map(int,phases.groups())))
    scroll=re.search(r'scroll (\d+) frames: paint (\d+) ms in (\d+) paints',result.stderr)
    if scroll:
        record.update(zip(['scroll_frames','scroll_paint_ms','scroll_paints'],map(int,scroll.groups())))
    values=re.findall(r'console: log: (arithmetic_ms|dom_ms)=(\d+)',result.stderr)
    record.update({key:int(value) for key,value in values})
    titles=re.findall(r'yozora-render: title ([^\n]*)',result.stderr)
    if titles: record['title']=titles[-1]
    record['errors']=[line for line in result.stderr.splitlines() if line.startswith('console: error:')][-8:]
    record['network_errors']=[line for line in result.stderr.splitlines() if line.startswith('net: failed ')][-8:]
    return record


def main():
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    records=[]
    with tempfile.TemporaryDirectory(prefix='yozora-a5-ssh-') as directory:
        socket=str(Path(directory)/'control')
        ssh=SSH+['-S',socket]
        tunnel=ssh+['-M','-fN','-o','ExitOnForwardFailure=yes','-R',f'127.0.0.1:{args.remote_port}:127.0.0.1:{server.server_port}',HOST]
        subprocess.run(tunnel,check=True,timeout=20)
        try:
            if args.suite == 'sites':
                cases=[('google','https://www.google.com/search?q=yozora+browser',{}),('claude','https://claude.ai/',{}),('gemini','https://gemini.google.com/',{}),('telegram','https://web.telegram.org/k/',{})]
                repeats=1
            else:
                cases=[(name,f'http://127.0.0.1:{args.remote_port}/'+path,extra) for name,path,extra in [
                    ('blank','blank',{}),('article','article',{'YOZORA_RENDER_SCROLL':'40,12'}),
                    ('cards','cards',{'YOZORA_RENDER_SCROLL':'40,12'}),('cards-scale1','cards',{'YOZORA_RENDER_SCROLL':'40,12','YOZORA_RENDER_SCALE':'1'}),
                    ('script','script',{}),('dom','dom',{}),('photo','photo',{}),('gc','gc',{'YOZORA_RENDER_RELEASE':'1'})]]
                repeats=3
            if args.repeat is not None:
                repeats = args.repeat
            if args.case:
                cases = [case for case in cases if case[0] in args.case]
                if len(cases) != len(set(args.case)):
                    raise RuntimeError("requested case does not belong to this suite")
            for name,url,extra in cases:
                for iteration in range(repeats):
                    label=f'{name}-{iteration+1}'
                    env={'YOZORA_RENDER_SCALE':'2','YOZORA_RENDER_SETTLE_MS':'5000' if args.suite == 'sites' else '400',**extra}
                    if args.suite == 'sites': env['YOZORA_LOG_REQUESTS']='1'
                    command_args=[DEVICE+'/'+args.binary,args.resources+'/fonts',args.resources+'/ca-bundle.crt',url,DEVICE+'/'+label+'.png','320','400']
                    output = OUT/(label+'.png')
                    if output.exists():
                        output.unlink()
                    # a failed load must not leave the screenshot from an earlier run under the same name
                    command='rm -f '+shlex.quote(DEVICE+'/'+label+'.png')+' || exit $?; export '+shlex.join([key+'='+value for key,value in env.items()])+'; printf "YOZORA_BENCH_PID=%s\\n" "$$"; exec '+shlex.join(command_args)
                    started=time.monotonic()
                    child=subprocess.Popen(ssh+[HOST,command],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                    try:
                        stdout,stderr=child.communicate(timeout=70)
                        result=subprocess.CompletedProcess(command_args,child.returncode,stdout,stderr)
                    except subprocess.TimeoutExpired as error:
                        prefix=error.stdout.decode() if isinstance(error.stdout,bytes) else (error.stdout or '')
                        found=re.search(r'YOZORA_BENCH_PID=(\d+)',prefix)
                        if found:
                            subprocess.run(ssh+[HOST,'kill -TERM '+found[1]],timeout=15,check=False,capture_output=True)
                        child.kill()
                        stdout,stderr=child.communicate()
                        result=subprocess.CompletedProcess(command_args,124,stdout,stderr+'\nbenchmark timed out after 70 s\n')
                    (OUT/(label+'.log')).write_text(result.stdout+result.stderr)
                    record=parse(result,label,time.monotonic()-started)
                    records.append(record)
                    (OUT/('sites.json' if args.suite == 'sites' else 'local.json')).write_text(json.dumps(records,indent=2)+'\n')
                    image = subprocess.run(ssh+[HOST, 'test -f '+shlex.quote(DEVICE+'/'+label+'.png')+' && cat '+shlex.quote(DEVICE+'/'+label+'.png')], capture_output=True, timeout=20)
                    if image.returncode == 0:
                        if not image.stdout.startswith(b'\x89PNG\r\n\x1a\n') or len(image.stdout) > 2 * 1024 * 1024:
                            raise RuntimeError("invalid or oversized device PNG")
                        output.write_bytes(image.stdout)
                    elif image.returncode != 1 or image.stderr:
                        raise RuntimeError("could not retrieve device PNG: "+image.stderr.decode(errors='replace'))
                    print(json.dumps(record),flush=True)
        finally:
            subprocess.run(ssh+['-O','exit',HOST],capture_output=True,timeout=15,check=False)
            server.shutdown()
            server.server_close()
    raise SystemExit(1 if any(record['status'] for record in records) else 0)

if __name__=='__main__': main()
