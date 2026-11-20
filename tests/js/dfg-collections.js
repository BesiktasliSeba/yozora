// 32-bit MapStorage must produce a tagged cell or an empty value for for-of iterators
var checks = 0;
function equal(actual, expected) {
    if (actual !== expected)
        throw new Error("collection regression: " + actual + " != " + expected);
    checks++;
}
function setSum(set) { var sum = 0; for (var value of set) sum += value; return sum; }
function setEntries(set) { var sum = 0; for (var pair of set.entries()) sum += pair[0] + pair[1]; return sum; }
function mapSum(map) { var sum = 0; for (var pair of map) sum += pair[0] + pair[1]; return sum; }
function mapKeys(map) { var sum = 0; for (var key of map.keys()) sum += key; return sum; }
function mapValues(map) { var sum = 0; for (var value of map.values()) sum += value; return sum; }
function mutation(set) {
    var sum = 0;
    for (var value of set) {
        sum += value;
        if (value === 1) { set.delete(2); set.add(4); }
        if (value === 3) { set.clear(); set.add(5); }
    }
    return sum;
}
var set = new Set(), map = new Map();
for (var i = 0; i < 13; i++) { set.add(i); map.set(i, i * 2); }
for (var i = 0; i < 500; i++) {
    equal(setSum(set), 78);
    equal(setEntries(set), 156);
    equal(mapSum(map), 234);
    equal(mapKeys(map), 78);
    equal(mapValues(map), 156);
    equal(mutation(new Set([1, 2, 3])), 9);
}
equal(setSum(new Set()), 0);
equal(setEntries(new Set()), 0);
equal(mapSum(new Map()), 0);
equal(mapKeys(new Map()), 0);
equal(mapValues(new Map()), 0);
set.clear(); map.clear();
equal(setSum(set), 0);
equal(mapSum(map), 0);
set.add(-0); set.add(NaN); set.add(NaN);
equal(set.size, 2);
equal(set.has(0), true);
equal(set.has(NaN), true);
"dfg collections: passed " + checks;
