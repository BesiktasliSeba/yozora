function check(condition, name) {
    if (!condition) throw new Error(name);
}

class Counter {
    #value = 0;
    increment() { return ++this.#value; }
}
const counter = new Counter();
check(counter.increment() === 1 && counter.increment() === 2, 'private fields');
check([1, 2, 3].map(value => value * 2).join(',') === '2,4,6', 'arrow functions');
const {a, ...rest} = {a: 1, b: 2};
check(a === 1 && rest.b === 2, 'destructuring and spread');
check(({nested: {value: 7}})?.nested?.value === 7, 'optional chaining');
check((null ?? 9) === 9 && (0 ?? 9) === 0, 'nullish coalescing');
check(2n ** 100n === 1267650600228229401496703205376n, 'bigint');
const map = new Map([[NaN, 3], ['a', 1]]);
check(map.get(NaN) === 3 && new Set([1, 1, 2]).size === 2, 'map and set');
const buffer = new ArrayBuffer(16);
const view = new DataView(buffer);
view.setUint32(0, 0x12345678, true);
check(new Uint8Array(buffer)[0] === 0x78 && view.getUint32(0, true) === 0x12345678,
      'arraybuffer and dataview');
check(new BigInt64Array([7n, -8n])[1] === -8n, 'bigint typed array');
check(/(?<word>\p{Letter}+)/u.exec('灯り').groups.word === '灯り', 'unicode regexp');
function* sequence() { yield 1; yield 2; }
check([...sequence()].join(',') === '1,2', 'generators');
const proxy = new Proxy({a: 3}, {get: (object, name) => Reflect.get(object, name) * 2});
check(proxy.a === 6, 'proxy and reflect');
check(new Intl.NumberFormat('en-US').format(1234.5) === '1,234.5', 'intl numbers');
check(new Intl.PluralRules('en').select(1) === 'one', 'intl plural rules');
check(new Intl.Segmenter('en', {granularity: 'word'}).segment('hello world')
    [Symbol.iterator]().next().value.segment === 'hello', 'intl segmenter');
check(new Intl.DateTimeFormat('en-US', {timeZone: 'UTC', year: 'numeric'})
    .format(new Date('2024-01-01T00:00:00Z')) === '2024', 'intl timezone');
async function answer() { return (await Promise.resolve(40)) + 2; }
answer().then(value => check(value === 42, 'async await'));
Promise.all([Promise.resolve(1), 2]).then(values => check(values.join(',') === '1,2', 'promise all'));
Promise.any([Promise.reject('first'), Promise.resolve(7)])
    .then(value => check(value === 7, 'promise any'));
'features passed';
