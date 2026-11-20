var out = [];
function t(name, v) { out.push(name + "=" + v); }
var text = "The quick brown fox jumps over 13 lazy dogs, 42 times; email: a.b-c@example.co.uk and x_y@test.org. Date 2026-10-17T12:30:45Z #tag1 #Tag_2 http://example.com/path?x=1&y=2#frag";
function run() {
  var r = [];
  r.push(text.match(/\b\w+\b/g).length);
  r.push(text.replace(/(\d+)/g, "<$1>"));
  r.push(/(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})Z/.exec(text).slice(1).join("|"));
  r.push(text.match(/[\w.-]+@[\w.-]+\.\w+/g).join(","));
  r.push(text.match(/#[A-Za-z_]\w*/g).join(","));
  r.push(/^https?:\/\/([^\/?#]+)([^?#]*)(\?[^#]*)?(#.*)?$/.exec("http://example.com/path?x=1&y=2#frag").slice(1).join("|"));
  r.push("aaa bbb ccc".split(/\s+/).join("-"));
  r.push("CamelCaseStringHere".replace(/([a-z])([A-Z])/g, "$1_$2").toLowerCase());
  r.push(/(a+)+b/.test("aaaaaaaaaaaaaaaaaaaaaab"));
  r.push(/(?:ab|cd)*ef/.exec("ababcdef")[0]);
  r.push(/(?<year>\d{4})-(?<m>\d\d)/.exec("on 2026-10").groups.year);
  r.push(/é/.test("café"));
  r.push(/[Ѐ-ӿ]+/.exec("привет мир")[0]);
  r.push("x".repeat(30).replace(/x/g, function (m, i) { return i % 10; }));
  r.push(/^(?=.*\d)(?=.*[a-z]).{6,}$/.test("abc123"));
  r.push(/(\w)\1/.exec("hello")[0]);
  r.push(/a.c/s.test("a\nc") + "," + /a.c/.test("a\nc"));
  r.push(/^\s*$/m.test("a\n \nb"));
  r.push(/(foo)?bar/.exec("bar")[1]);
  r.push("A1b2C3".replace(/[a-z]/gi, function (c) { return c === c.toUpperCase() ? c.toLowerCase() : c.toUpperCase(); }));
  r.push(/\bfox\b/i.test("The FOX"));
  r.push(JSON.stringify(/(\d+)\s*(px|em|%)/.exec("width: 120 px")));
  r.push(/x*/.exec("aaa").index);
  r.push(/(?<!a)b/.test("ab") + "," + /(?<=a)b/.test("ab"));
  return r.join("\n");
}
var last;
for (var k = 0; k < 60; k++) last = run();
t("run", last);
out.join("\n");
