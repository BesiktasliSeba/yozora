#!/usr/bin/env python3
"""runs scripts that recurse until the stack is full in the render tool on a jailbroken phone, not part of meow test.

usage: stack_device.py SSH_TARGET HOST_ADDRESS
SSH_TARGET is the phone, for example root@DEVICE_ADDRESS, HOST_ADDRESS the address of this machine as the phone sees it.
YOZORA_DEVICE_SSH_CONFIG names an ssh config file for old phones, YOZORA_DEVICE_DIR the directory of yozora-render on the phone
(default /var/root/yozora-probe), sshpass reads SSHPASS.

the stack of the page thread is 8 MB and JavaScriptCore stops scripts at 5 MB. a conditional branch of the interpreter assembly that
the linker moved off its target crashed the process when the overflow was found by a vm entry, which depends on how deep a frame
lands against the limit, so the same page runs with several limits, with and without the jit
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import subprocess
import sys
import threading

CASES = [
    ("fn", "function r(){return r()+1}r()"),
    ("getter", "var o={get a(){return this.a}};o.a"),
    ("getter-proto", "var p={get a(){return this.a}};var o=Object.create(p);o.a"),
    ("setter", "var o={set a(v){this.a=v}};o.a=1"),
    ("toString", "var o={toString:function(){return ''+o}};''+o"),
    ("proxy", "var p=new Proxy({},{get:function(t,k,r){return r[k]}});p.x"),
    ("map", "function r(a){return a.map(function(){return r(a)})}r([1])"),
    ("json", "var o={toJSON:function(){return JSON.stringify(o)}};JSON.stringify(o)"),
]
PAGE = ("<!doctype html><title>stack</title><script>var out=[];"
        "function t(name,body){try{new Function(body)();out.push(name+':no-throw')}catch(e){out.push(name+':'+(e&&e.name))}}"
        + "".join(f"t({name!r},{body!r});" for name, body in CASES)
        + "console.log('stack '+out.join(' | '));</script>").encode()
LIMITS = [None, 1792, 2048, 3584]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(PAGE)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(PAGE)


def main(target, address):
    server = ThreadingHTTPServer(("0.0.0.0", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    directory = os.environ.get("YOZORA_DEVICE_DIR", "/var/root/yozora-probe")
    ssh = (["sshpass", "-e"] if "SSHPASS" in os.environ else []) + ["ssh", "-o", "LogLevel=ERROR"]
    if "YOZORA_DEVICE_SSH_CONFIG" in os.environ:
        ssh += ["-F", os.environ["YOZORA_DEVICE_SSH_CONFIG"]]
    failures = 0
    for name, extra in [("jit", ""), ("interpreter", "YOZORA_NO_JIT=1")]:
        for limit in LIMITS:
            environment = extra + (f" JSC_maxPerThreadStackUsage={limit * 1024}" if limit else "")
            command = (f"cd {directory} && {environment} YOZORA_RENDER_TIMEOUT=60 ./yozora-render /Applications/Yozora.app/fonts "
                       f"/Applications/Yozora.app/ca-bundle.crt http://{address}:{server.server_address[1]}/ stack.png 2>&1; echo exit=$?")
            result = subprocess.run(ssh + [target, command], capture_output=True, text=True, timeout=120)
            lines = result.stdout.splitlines()
            summary = next((line for line in lines if "console: log: stack " in line), "")
            ok = "exit=0" in lines and "crash signal" not in result.stdout and all(f"{case}:RangeError" in summary for case, _ in CASES)
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {name} limit {limit or 'default'}: {summary[:160] or result.stdout[-200:]}")
    server.shutdown()
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
