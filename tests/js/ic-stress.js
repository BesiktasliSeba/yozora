var log = [];
function check(name, got) { log.push(name + "=" + got); }
function warm(f, n) { for (var k = 0; k < n; k++) f(k); return f(n); }

// own load and store through one site, plain and out of line properties
function Own(a) { this.a = a; this.b = a + 1; this.c = a + 2; this.d = a + 3; this.e = a + 4; this.f = a + 5; this.g = a + 6; this.h = a + 7; this.i = a + 8; this.j = a + 9; this.k = a + 10; }
var own = new Own(1);
function readOwn(i) { return own.a + own.e + own.k + i; }
check("own load", warm(readOwn, 400));
function writeOwn(i) { own.a = i; own.k = i * 2; return own.a + own.k; }
check("own store", warm(writeOwn, 400));

// prototype load, a replaced prototype value must invalidate the cached holder
function Base() {}
Base.prototype.v = 10;
Base.prototype.m = function () { return this.v + 1; };
function Derived() { this.own = 1; }
Derived.prototype = Object.create(Base.prototype);
var d = new Derived();
function readProto(i) { return d.v + d.m() + i; }
check("proto load", warm(readProto, 400));
Base.prototype.v = 20;
check("proto value changed", readProto(0));
Derived.prototype.v = 30;
check("proto shadowed", readProto(0));
delete Derived.prototype.v;
check("proto unshadowed", readProto(0));
Base.prototype.m = function () { return 1000; };
check("proto method replaced", readProto(0));

// miss, then the property appears on the object and on the prototype chain
var m1 = { x: 1 };
function readMiss(i) { return typeof m1.nope + (m1.nope === undefined ? "u" : "d") + i; }
check("miss", warm(readMiss, 400));
m1.nope = 5;
check("miss then own", readMiss(0));
var m2 = {};
function readMissChain(i) { return m2.late === undefined ? "u" + i : "d" + m2.late; }
warm(readMissChain, 400);
Object.prototype.late = "chain";
check("miss then chain", readMissChain(0));
delete Object.prototype.late;
check("miss chain removed", readMissChain(0));

// getters and setters on the prototype, own accessors, getter that throws
function G() { this._v = 3; }
Object.defineProperty(G.prototype, "v", { get: function () { return this._v * 2; }, set: function (x) { this._v = x; } });
var g = new G();
function accessor(i) { g.v = i; return g.v; }
check("accessor", warm(accessor, 400));
var thrower = { get boom() { throw new Error("boom"); } };
function readThrower(i) { try { return thrower.boom; } catch (e) { return e.message + i; } }
check("getter throws", warm(readThrower, 300));
var og = { get p() { return 7; } };
function readOwnGetter(i) { return og.p + i; }
check("own getter", warm(readOwnGetter, 400));

// add property transitions, each shape of the object gets its own stub
function addProps(i) { var o = {}; o.a = i; o.b = i + 1; o.c = i + 2; o.d = i + 3; o.e = i + 4; o.f = i + 5; o.g = i + 6; o.h = i + 7; o.i = i + 8; o.j = i + 9; o.k = i + 10; o.l = i + 11; return o.a + o.f + o.l; }
check("transition", warm(addProps, 400));
function addIndexed(i) { var o = { a: 1 }; o[0] = i; o.b = 2; o[1] = 3; return o.a + o.b + o[0] + o[1]; }
check("transition with indexed", warm(addIndexed, 400));

// polymorphic and megamorphic sites
var shapes = [];
for (var s = 0; s < 24; s++) { var o = { id: s }; o["p" + s] = s; o.tail = s * 2; shapes.push(o); }
function readPoly(n) { var t = 0; for (var i = 0; i < n; i++) t += shapes[i].tail + shapes[i].id; return t; }
check("poly 2", warm(function () { return readPoly(2); }, 400));
check("poly 4", warm(function () { return readPoly(4); }, 400));
check("poly 9", warm(function () { return readPoly(9); }, 400));
check("mega 24", warm(function () { return readPoly(24); }, 400));
function storePoly(n, v) { for (var i = 0; i < n; i++) shapes[i].tail = v + i; return shapes[n - 1].tail; }
check("store mega", warm(function (k) { return storePoly(24, k); }, 300));

// arrays: length, indexed loads and stores in every storage kind, holes, growth
function lenOf(a) { return a.length; }
var arrs = [[1, 2, 3], [], new Array(5), "string", { length: 9 }];
function lens() { var t = ""; for (var i = 0; i < arrs.length; i++) t += lenOf(arrs[i]) + ","; return t; }
check("length kinds", warm(lens, 400));
function intLoad(a, i) { return a[i]; }
function intArr() { var a = [1, 2, 3, 4, 5, 6, 7, 8]; var s = 0; for (var i = 0; i < 8; i++) s += intLoad(a, i); return s + "," + intLoad(a, 8) + "," + intLoad(a, -1); }
check("int array", warm(intArr, 400));
function dblArr() { var a = [1.5, 2.5, 3.5]; var s = 0; for (var i = 0; i < 3; i++) s += intLoad(a, i); return s; }
check("double array", warm(dblArr, 400));
function objArr() { var a = ["a", { v: 2 }, null, undefined]; var s = ""; for (var i = 0; i < 4; i++) s += String(intLoad(a, i)); return s; }
check("object array", warm(objArr, 400));
function holeArr() { var a = [1, , 3]; a[5] = 6; var s = ""; for (var i = 0; i < 7; i++) s += (intLoad(a, i) === undefined ? "h" : intLoad(a, i)); return s; }
check("hole array", warm(holeArr, 400));
function grow(i) { var a = []; for (var k = 0; k < 40; k++) a[k] = k * i; a[100] = 1; a.length = 50; return a.length + a[39] + (a[60] === undefined ? "u" : "d"); }
check("grow", warm(grow, 300));
function typed() { var t = new Uint8Array(16); for (var i = 0; i < 16; i++) t[i] = i * 20; var s = 0; for (var i = 0; i < 16; i++) s += t[i]; var f = new Float64Array(4); f[1] = 1.25; return s + f[1] + (t[99] === undefined ? "u" : "d"); }
check("typed", warm(typed, 300));
function strIndex(i) { var s = "abcdef"; return s[i % 6] + s.length + "xyz"[1]; }
check("string index", warm(strIndex, 400));
function keyed(o, k) { return o[k]; }
function keyedKinds() { var o = { a: 1, b: 2, 3: "n" }; return [keyed(o, "a"), keyed(o, "b"), keyed(o, 3), keyed(o, "3"), keyed(o, "zz"), keyed([5, 6], 1), keyed("hey", 1)].join("|"); }
check("keyed", warm(keyedKinds, 400));
var sym = Symbol("s");
function symLoad() { var o = {}; o[sym] = 4; return keyed(o, sym) + (keyed({}, sym) === undefined ? "u" : "d"); }
check("symbol key", warm(symLoad, 300));

// dictionary objects, delete, and shape changes behind a hot site
function dict(i) { var o = {}; for (var k = 0; k < 30; k++) o["k" + k] = k; delete o.k3; delete o.k10; o.k3 = i; return o.k3 + o.k29 + (o.k10 === undefined ? "u" : "d"); }
check("dictionary", warm(dict, 100));
var shifting = { a: 1, b: 2 };
function readShifting() { return shifting.b + "," + (shifting.c === undefined ? "u" : shifting.c); }
warm(readShifting, 400);
shifting.c = 9;
check("add after cache", readShifting());
delete shifting.b;
check("delete after cache", readShifting());
Object.defineProperty(shifting, "b", { get: function () { return 77; }, configurable: true });
check("accessor after cache", readShifting());
Object.freeze(shifting);
check("freeze", readShifting());

// in, hasOwnProperty, instanceof, delete through caches
function hasIn(o) { return ("a" in o) + "," + ("toString" in o) + "," + ("zz" in o); }
function hasKinds() { return hasIn({ a: 1 }) + ";" + hasIn(Object.create({ a: 2 })) + ";" + hasIn([]); }
check("in", warm(hasKinds, 400));
function isA(x) { return (x instanceof Array) + "" + (x instanceof Derived) + (x instanceof Object); }
function isKinds() { return isA([]) + isA(d) + isA({}) + isA(Object.create(Derived.prototype)); }
check("instanceof", warm(isKinds, 400));
function del(i) { var o = { a: 1, b: 2, c: i }; delete o.b; return Object.keys(o).join("") + o.c; }
check("delete", warm(del, 400));

// proxies, global properties, this, arguments, class fields, private names
var px = new Proxy({ a: 1 }, { get: function (t, k) { return k === "a" ? 100 : t[k]; } });
function readProxy(i) { return px.a + i; }
check("proxy", warm(readProxy, 300));
var globalCounter = 0;
function bump() { globalCounter = globalCounter + 1; return globalCounter; }
check("global", warm(bump, 400));
function argsLen() { return arguments.length + arguments[0] + (arguments.callee === argsLen ? "c" : "n"); }
check("arguments", warm(function (i) { return argsLen(i, 2, 3); }, 300));
class Point { #secret = 5; x = 1; y = 2; static count = 0; constructor(z) { this.z = z; Point.count++; } get sum() { return this.x + this.y + this.z + this.#secret; } bump() { this.x += 1; return this.#secret + this.x; } }
function readPoint(i) { var p = new Point(i); return p.sum + p.bump(); }
check("class", warm(readPoint, 300));
function Dyn(kind) { if (kind) this.a = 1; this.b = 2; if (!kind) this.a = 3; }
function readDyn(i) { var x = new Dyn(i & 1), y = new Dyn(0); return x.a + x.b + y.a + y.b; }
check("shape order", warm(readDyn, 400));
function withNumberKeys(i) { var o = {}; o[1] = "a"; o[2] = "b"; o["x"] = i; return o[1] + o[2] + o.x + Object.keys(o).length; }
check("number keys", warm(withNumberKeys, 300));
function protoSwap(i) { var o = { own: 1 }; var p1 = { inherited: 10 }, p2 = { inherited: 20 }; Object.setPrototypeOf(o, i & 1 ? p1 : p2); return o.inherited + o.own; }
check("proto swap", warm(protoSwap, 400));
function protoOfNull() { var o = Object.create(null); o.k = 3; return o.k + (o.toString === undefined ? "u" : "d"); }
check("null proto", warm(protoOfNull, 300));
function chain3() { var a = { deep: 1 }, b = Object.create(a), c = Object.create(b), e = Object.create(c); return e.deep + (e.deeper === undefined ? "u" : "d"); }
check("long chain", warm(chain3, 400));

log.join("\n");
