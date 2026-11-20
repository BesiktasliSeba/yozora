// linked 32-bit DFG caches must survive repatching, getters, exceptions and OSR exits
var checks = 0;
function equal(actual, expected) {
    if (actual !== expected)
        throw new Error("property regression: " + actual + " != " + expected);
    checks++;
}
function get(o) { return o.value; }
function put(o, value) { o.value = value; return o.value; }
function byValue(o, key) { return o[key]; }
function remove(o) { return delete o.value; }
function contains(o, key) { return key in o; }
function instance(o, constructor) { return o instanceof constructor; }
function caught(o) {
    try { return o.value; }
    catch (error) { return error.message; }
}
function Box(value) { this.value = value; }
var getter = {
    stored: 0,
    get value() { return this.stored + 1; },
    set value(value) { this.stored = value - 1; }
};
var key = Symbol("key"), symbolic = {};
symbolic[key] = 37;
for (var i = 0; i < 1000; i++) {
    var box = new Box(i);
    equal(get(box), i);
    equal(put(box, i + 1), i + 1);
    equal(byValue(box, "value"), i + 1);
    equal(byValue(symbolic, key), 37);
    equal(contains(box, "value"), true);
    equal(instance(box, Box), true);
    equal(remove(box), true);
    equal(contains(box, "value"), false);
    equal(get(box), undefined);
    equal(put(getter, i), i);
    equal(caught(getter), i);
}
var throwing = { get value() { throw new Error("getter threw"); } };
equal(caught(throwing), "getter threw");
equal(get({ value: 2147483648 }), 2147483648);
equal(get({ value: "changed type" }), "changed type");
equal(get({ value: null }), null);
equal(instance({}, Box), false);
equal(byValue("abc", 1), "b");
equal(byValue([1, 2, 3], 2), 3);
"dfg properties: passed " + checks;
