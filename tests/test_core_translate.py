"""the page translation library in the isolated world of a page: it finds the texts a reader sees, hands them out in the order of the screen, puts the
translations where they came from (and only there), follows what the page adds, and gives the page back as it was"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent

PAGE = """<!doctype html><html lang="fr" dir="ltr"><head><meta charset=utf-8><title>Bonjour le monde</title></head>
<body style='margin:0;font:16px sans-serif'>
<h1>Bonjour</h1>
<p id=p1>Le renard   brun <b>rapide</b> saute.</p>
<p class=notranslate id=skip1>Ne pas traduire</p>
<p translate=no id=skip2>Aussi non</p>
<pre id=skip3>code visible ici</pre>
<code id=skip4>var x = 1</code>
<script id=skip5>var secret = 'texte du script'</script>
<style>.hidden{display:none}</style>
<p class=hidden id=skip6>Texte caché</p>
<textarea id=skip7>champ de texte</textarea>
<input id=field placeholder="Rechercher ici" title="Un titre">
<img id=picture alt="Une image" width=10 height=10>
<p id=digits>12345 67890</p>
<div style='height:3000px'></div>
<p id=far>Texte tout en bas de la page</p>
</body></html>"""

CHECK = """
(function () {
  var out = {};
  var first = JSON.parse(yzTr.collect());
  out.first = first;
  var texts = JSON.parse(yzTr.get(first.from, first.lengths.length));
  out.texts = texts;
  // translate by upper casing inside brackets, in an order that is not the order of the page
  var answers = texts.map(function (t) { return '[' + t.toUpperCase() + ']'; });
  out.put = yzTr.put(first.from, JSON.stringify(answers));
  yzTr.watch(true, 'ru');
  function text(id) { return document.getElementById(id).textContent; }
  out.after = {
    p1: text('p1'), skip1: text('skip1'), skip2: text('skip2'), skip3: text('skip3'), skip4: text('skip4'), skip5: text('skip5'), skip6: text('skip6'),
    skip7: document.getElementById('skip7').value, digits: text('digits'), far: text('far'), h1: document.querySelector('h1').textContent,
    placeholder: document.getElementById('field').getAttribute('placeholder'), title_attr: document.getElementById('field').getAttribute('title'),
    alt: document.getElementById('picture').getAttribute('alt'), doc_title: document.title,
    lang: document.documentElement.getAttribute('lang'), dir: document.documentElement.getAttribute('dir')
  };
  // a second collect finds nothing new until the page adds something
  out.again = JSON.parse(yzTr.collect()).lengths.length;
  out.dirty_before = yzTr.dirty();
  var added = document.createElement('p');
  added.id = 'added';
  added.textContent = 'Un nouveau texte';
  document.body.insertBefore(added, document.getElementById('far'));
  return JSON.stringify(out);
})()
"""

AFTER = """
(function () {
  var out = {};
  out.dirty = yzTr.dirty();
  var next = JSON.parse(yzTr.collect());
  out.next = next;
  out.texts = JSON.parse(yzTr.get(next.from, next.lengths.length));
  yzTr.put(next.from, JSON.stringify(out.texts.map(function (t) { return '[' + t.toUpperCase() + ']'; })));
  out.added = document.getElementById('added').textContent;
  // the page rewrites a translated text: the library takes it as new text and not as its own
  document.getElementById('p1').firstChild.nodeValue = 'Nouveau texte du serveur';
  out.rewritten_dirty = yzTr.dirty();
  var again = JSON.parse(yzTr.collect());
  out.rewritten = JSON.parse(yzTr.get(again.from, again.lengths.length));
  yzTr.restore();
  out.restored = {
    p1: document.getElementById('p1').textContent, h1: document.querySelector('h1').textContent, far: document.getElementById('far').textContent,
    placeholder: document.getElementById('field').getAttribute('placeholder'), doc_title: document.title,
    lang: document.documentElement.getAttribute('lang'), dir: document.documentElement.getAttribute('dir'), added: document.getElementById('added').textContent
  };
  return JSON.stringify(out);
})()
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def run(binary, port, script, directory):
    path = Path(directory) / "script.js"
    path.write_text(script)
    env = {"YOZORA_RENDER_SCRIPT": str(path), "PATH": "/usr/bin:/bin"}
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}/", str(Path(directory) / "out.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120, env=env)
    assert result.returncode == 0, result.stderr[-1500:]
    for line in result.stderr.splitlines():
        if line.startswith("yozora-render: script result "):
            return json.loads(line[len("yozora-render: script result "):])
    raise AssertionError("no script result: " + result.stderr[-1500:])


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    with tempfile.TemporaryDirectory() as directory:
        first = run(binary, port, CHECK, directory)
        texts = first["texts"]
        # what a reader sees is handed out, in the order of the page with the screen first; whitespace is collapsed
        assert "Bonjour" in texts and "Le renard brun" in texts and "rapide" in texts and "saute." in texts, texts
        assert "Rechercher ici" in texts and "Un titre" in texts and "Une image" in texts and "Bonjour le monde" in texts, texts
        assert "Texte tout en bas de la page" in texts
        # what must stay as it is
        for skipped in ("Ne pas traduire", "Aussi non", "code visible ici", "var x = 1", "texte du script", "Texte caché", "champ de texte", "12345 67890"):
            assert not any(skipped in text for text in texts), (skipped, texts)
        # the screen first: the far text is the last, the title of the page is the very last
        assert texts.index("Bonjour") < texts.index("Texte tout en bas de la page"), texts
        assert texts[-1] == "Bonjour le monde", texts
        after = first["after"]
        assert after["h1"] == "[BONJOUR]", after
        assert after["p1"] == "[LE RENARD BRUN] [RAPIDE] [SAUTE.]", after
        assert after["far"] == "[TEXTE TOUT EN BAS DE LA PAGE]", after
        assert after["placeholder"] == "[RECHERCHER ICI]" and after["title_attr"] == "[UN TITRE]" and after["alt"] == "[UNE IMAGE]", after
        assert after["doc_title"] == "[BONJOUR LE MONDE]", after
        assert after["skip1"] == "Ne pas traduire" and after["skip2"] == "Aussi non" and after["skip3"] == "code visible ici", after
        assert after["skip4"] == "var x = 1" and after["skip5"] == "var secret = 'texte du script'" and after["skip6"] == "Texte caché", after
        assert after["skip7"] == "champ de texte" and after["digits"] == "12345 67890", after
        assert after["lang"] == "ru" and after["dir"] == "ltr", after
        assert first["again"] == 0, first
        print("translate library: first pass,", len(texts), "texts")
        # one script per page load: the second run repeats the first on a fresh page and then goes on, so the
        # whole sequence is one script and the second part runs in the same load as the first
        combined = "(function () { var first = JSON.parse(" + CHECK.strip() + "); var second = JSON.parse(" + AFTER.strip() + "); return JSON.stringify({ first: first, second: second }); })()"
        result = run(binary, port, combined, directory)
        second = result["second"]
        assert second["dirty"] == "1", second
        assert second["texts"] == ["Un nouveau texte"], second
        assert second["added"] == "[UN NOUVEAU TEXTE]", second
        assert second["rewritten_dirty"] == "1" and second["rewritten"] == ["Nouveau texte du serveur"], second
        restored = second["restored"]
        assert restored["h1"] == "Bonjour" and restored["far"] == "Texte tout en bas de la page", restored
        assert restored["p1"] == "Nouveau texte du serveurrapide saute.", restored
        assert restored["placeholder"] == "Rechercher ici" and restored["doc_title"] == "Bonjour le monde", restored
        assert restored["lang"] == "fr" and restored["dir"] == "ltr", restored
        assert restored["added"] == "Un nouveau texte", restored
    server.shutdown()
    print("translate library: collect, put, watch, restore and the page rewriting its text passed")


if __name__ == "__main__":
    main()
