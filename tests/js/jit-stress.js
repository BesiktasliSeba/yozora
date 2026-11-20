var fails = 0, n = 0;
function check(name, got, want) { n++; print_(name + "=" + got); }
var log = [];
function print_(s) { log.push(s); }
// arithmetic and bit ops, int overflow into doubles
function arith(a, b) { return [a + b, a - b, a * b, a / b, a % b, a & b, a | b, a ^ b, a << 3, a >> 2, a >>> 1, -a, ~a].join(","); }
check("arith int", arith(7, 3), "10,4,21,2.3333333333333335,1,3,7,4,56,1,3,-7,-8");
check("arith big", arith(2147483647, 2), "2147483649,2147483645,4294967294,1073741823.5,1,2,2147483647,2147483645,-8,536870911,1073741823,-2147483647,-2147483648");
check("arith neg", arith(-5, 2), "-3,-7,-10,-2.5,-1,0,-5,-5,-40,-2,2147483645,5,4");
check("arith dbl", arith(1.5, 0.5), "2,1,0.75,3,0,0,1,1,8,0,0,-1.5,-2");
function loopSum(n) { var s = 0; for (var i = 0; i < n; i++) s = (s + i * i) % 1000003; return s; }
for (var k = 0; k < 40; k++) loopSum(2000);
check("loopSum", loopSum(100000), 85030);
// strings
function strs(x) { return (x + "a").toUpperCase().slice(1) + x.length + x.charCodeAt(0) + x.indexOf("c"); }
for (var k = 0; k < 40; k++) strs("abcdef");
check("strs", strs("abcdef"), "BCDEFA697099");
// objects and property access, inline caches, polymorphism
function P(x, y) { this.x = x; this.y = y; }
P.prototype.sum = function () { return this.x + this.y; };
function Q(x) { this.x = x; this.z = 3; }
Q.prototype.sum = function () { return this.x * 2; };
var objs = [new P(1, 2), new Q(5), { x: 9, y: 1, sum: function () { return 100; } }, new P(3, 4)];
function polySum() { var t = 0; for (var i = 0; i < objs.length; i++) t += objs[i].sum(); return t; }
for (var k = 0; k < 200; k++) polySum();
check("poly", polySum(), 3 + 10 + 100 + 7);
// getters setters proto chains
var base = { get g() { return this._g * 2; }, set g(v) { this._g = v; } };
var derived = Object.create(base);
function gs(i) { derived.g = i; return derived.g; }
for (var k = 0; k < 100; k++) gs(k);
check("getset", gs(21), 42);
// arrays
function arr() { var a = []; for (var i = 0; i < 300; i++) a.push(i * 2); var s = 0; for (var i = 0; i < a.length; i++) s += a[i]; a.length = 10; a[20] = 1; return s + "," + a.length + "," + a[15] + "," + a.indexOf(1); }
for (var k = 0; k < 30; k++) arr();
check("arr", arr(), "89700,21,undefined,20");
function dblarr() { var a = [1.5, 2.5]; a[5] = 7.25; var s = 0; for (var i = 0; i < a.length; i++) if (a[i] !== undefined) s += a[i]; return s + "," + (3 in a) + "," + a.length; }
for (var k = 0; k < 30; k++) dblarr();
check("dblarr", dblarr(), "11.25,false,6");
// closures and recursion
function mk(n) { var c = n; return function () { return c++; }; }
var cl = mk(10); for (var k = 0; k < 200; k++) cl();
check("closure", cl(), 210);
function fibr(n) { return n < 2 ? n : fibr(n - 1) + fibr(n - 2); }
check("fib", fibr(20), 6765);
// exceptions
function thrower(i) { if (i % 3 == 0) throw new Error("e" + i); return i; }
function catcher() { var s = 0, c = 0; for (var i = 0; i < 100; i++) { try { s += thrower(i); } catch (e) { c += e.message.length; } finally { s++; } } return s + "," + c; }
for (var k = 0; k < 20; k++) catcher();
check("exc", catcher(), "3283,183");
// switch
function sw(x) { switch (x) { case 1: return "a"; case 2: return "b"; case 3: return "c"; case 100: return "d"; case "s": return "e"; default: return "z"; } }
var swr = ""; for (var k = 0; k < 300; k++) swr = sw(k % 5 + 1) + sw("s") + sw(100) + sw(-1);
check("switch", swr, "dedz");
// classes, generators, spread, destructuring, template
class A { constructor(v) { this.v = v; } get dbl() { return this.v * 2; } static make(v) { return new this(v); } }
class B extends A { constructor(v) { super(v + 1); } get dbl() { return super.dbl + 1; } }
function cls() { var o = B.make(4); return o.dbl + ":" + (o instanceof A); }
for (var k = 0; k < 100; k++) cls();
check("class", cls(), "11:true");
function* gen(n) { for (var i = 0; i < n; i++) yield i * i; }
function genSum() { var s = 0; for (var v of gen(50)) s += v; return s; }
for (var k = 0; k < 20; k++) genSum();
check("gen", genSum(), 40425);
function destr() { var { a, b: [c, d = 4], ...rest } = { a: 1, b: [2], e: 5, f: 6 }; return `${a}${c}${d}${Object.keys(rest)}`; }
for (var k = 0; k < 100; k++) destr();
check("destr", destr(), "124e,f");
function spread() { var m = Math.max(...[1, 5, 3], ...[4]); return [...[1, 2], m].join(); }
for (var k = 0; k < 100; k++) spread();
check("spread", spread(), "1,2,5");
// regexp and json and date and math
function re() { return "foo123bar45".replace(/\d+/g, function (m) { return "<" + m + ">"; }) + /(a+)(b*)/.exec("xaabbb").slice(1).join("|"); }
for (var k = 0; k < 100; k++) re();
check("regexp", re(), "foo<123>bar<45>aa|bbb");
function js() { return JSON.stringify(JSON.parse('{"a":[1,2,{"b":null}],"c":"d\\n","e":1.5e3}')); }
for (var k = 0; k < 100; k++) js();
check("json", js(), '{"a":[1,2,{"b":null}],"c":"d\\n","e":1500}');
function mth() { return [Math.floor(-1.5), Math.ceil(1.2), Math.round(2.5), Math.sqrt(16), Math.abs(-3), Math.pow(2, 10), Math.sin(0), Math.cos(0), Math.atan2(1, 1).toFixed(4), Math.min(3, 1, 2), Math.trunc(-4.7), Math.imul(65536, 65536)].join(); }
for (var k = 0; k < 100; k++) mth();
check("math", mth(), "-2,2,3,4,3,1024,0,1,0.7854,1,-4,0");
function dt() { return new Date(Date.UTC(2020, 1, 29, 12, 30)).toISOString(); }
for (var k = 0; k < 100; k++) dt();
check("date", dt(), "2020-02-29T12:30:00.000Z");
// typed arrays, maps, sets, symbols, proxies
function ta() { var f = new Float64Array(8), u = new Uint8Array(16), i32 = new Int32Array(u.buffer); for (var i = 0; i < 8; i++) f[i] = i / 4; for (var i = 0; i < 16; i++) u[i] = i * 17; return f[3] + "," + i32[1] + "," + u[15]; }
for (var k = 0; k < 100; k++) ta();
check("typed", ta(), "0.75,-2021161081,255");
function ms() { var m = new Map(), s = new Set(); for (var i = 0; i < 50; i++) { m.set("k" + i, i); s.add(i % 7); } return m.get("k33") + "," + s.size + "," + [...s].join(""); }
for (var k = 0; k < 50; k++) ms();
check("mapset", ms(), "33,7,0123456");
function prx() { var p = new Proxy({}, { get: function (t, k) { return k + "!"; } }); return p.hello + p[1]; }
for (var k = 0; k < 100; k++) prx();
check("proxy", prx(), "hello!1!");
// arguments, apply, bind, call, default params
function args() { return arguments.length + ":" + Array.prototype.slice.call(arguments, 1).join("-"); }
function ap() { return args.apply(null, [1, 2, 3]) + args.call(null, 4, 5) + args.bind(null, 9)(8); }
for (var k = 0; k < 100; k++) ap();
check("apply", ap(), "3:2-32:52:8");
function defp(a, b = a * 2, ...r) { return a + b + r.length; }
for (var k = 0; k < 100; k++) defp(1);
check("defparam", defp(1, undefined, 7, 8), 5);
// tagged async
var asyncRes = "";
(async function () { var x = await Promise.resolve(5); asyncRes = "a" + x; })();
// big loops with typeof and delete and in
function misc() { var o = { a: 1 }; var r = typeof o.a + typeof o.b + ("a" in o) + delete o.a + ("a" in o); return r; }
for (var k = 0; k < 100; k++) misc();
check("misc", misc(), "numberundefinedtruetruefalse");
// string concat in loop and number formatting
function conc() { var s = ""; for (var i = 0; i < 500; i++) s += i; return s.length + "," + (0.1 + 0.2) + "," + (123.456).toFixed(1) + "," + (255).toString(16) + "," + parseInt("0x1f") + "," + (1e21 + "") + "," + (-0 === 0); }
for (var k = 0; k < 20; k++) conc();
check("conc", conc(), "1390,0.30000000000000004,123.5,ff,31,1e+21,true");
// large int to double transitions in loop
function ovf() { var x = 1; for (var i = 0; i < 40; i++) x = x * 3; return x; }
for (var k = 0; k < 100; k++) ovf();
check("ovf", ovf(), 12157665459056929000);
function cmp() { var r = []; r.push(1 < 2, 2 <= 2, 3 > 4, "a" < "b", null == undefined, null === undefined, NaN == NaN, "1" == 1, 0 == "", [] + [] == ""); return r.join(); }
for (var k = 0; k < 100; k++) cmp();
check("cmp", cmp(), "true,true,false,true,true,false,false,true,true,true");
log.join("\n");
