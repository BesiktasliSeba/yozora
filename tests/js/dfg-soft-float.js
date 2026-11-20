// darwin armv7 passes C doubles in integer registers, including their return values
var checks = 0;
function equal(actual, expected) {
    if (actual !== expected)
        throw new Error("double ABI regression: " + actual + " != " + expected);
    checks++;
}
function radix(value, base) { return value.toString(base); }
function hex(value) { return value.toString(16); }
function store(array, index, value) { array[index] = value; return array[index]; }
function prepend(array, value) { array.unshift(value); return array[0]; }
function math(value, power) {
    return [Math.sin(value), Math.cos(value), Math.tan(value), Math.atan2(value, power),
        Math.pow(value, power), Math.exp(value), Math.log(value), Math.cbrt(value)].join("|");
}
function invalid(value, base) {
    try { return value.toString(base); }
    catch (error) { return error.name; }
}
for (var i = 0; i < 100; i++) {
    equal(radix(255.5, 16), "ff.8");
    equal(radix(-10.25, 2), "-1010.01");
    equal(radix(2147483648.5, 16), "80000000.8");
    equal(radix(1.5, 36), "1.i");
    equal(hex(-255.5), "-ff.8");
    equal(hex(Infinity), "Infinity");
    equal(hex(NaN), "NaN");
    equal(invalid(1.5, 1), "RangeError");
    equal(invalid(1.5, 37), "RangeError");
    equal(radix(1.5, 10), "1.5");
    equal(radix(-0, 16), "0");
    equal(store([0.25], i % 8 + 8, i + 0.5), i + 0.5);
    equal(prepend([0.25], i + 0.5), i + 0.5);
    math(i / 8 + 0.5, i % 4 + 0.5);
}
var output = [];
for (var i = 0; i < 16; i++)
    output.push(math(i / 8 + 0.5, i % 4 + 0.5));
"dfg double ABI: passed " + checks + "\n" + output.join("\n");
