(function (n) {
    'use strict';
    // the WebAssembly JavaScript API over core/wasm. n holds the natives of core/core/YozoraWasm.cpp, nothing
    // of it stays on the page. types are one letter: i i32, l i64, f f32, d f64, F funcref, E externref. an
    // i64 crosses as two numbers, low then high 32 bits

    var kinds = ['function', 'table', 'memory', 'global'];
    var isView = ArrayBuffer.isView;
    var defineProperty = Object.defineProperty;
    var MASK32 = 0xffffffffn;

    // length 1 like the error classes of the engine: (message, ...options)
    class CompileError extends Error { constructor(message, ...options) { super(message, ...options); } }
    class LinkError extends Error { constructor(message, ...options) { super(message, ...options); } }
    class RuntimeError extends Error { constructor(message, ...options) { super(message, ...options); } }
    [[CompileError, 'CompileError'], [LinkError, 'LinkError'], [RuntimeError, 'RuntimeError']].forEach(function (pair) {
        defineProperty(pair[0].prototype, 'name', { value: pair[1], writable: true, configurable: true, enumerable: false });
        defineProperty(pair[0], 'name', { value: pair[1], configurable: true });
    });

    var classes = { compile: CompileError, link: LinkError, runtime: RuntimeError, range: RangeError, type: TypeError };

    // an engine failure becomes the error class the API names, anything a page threw passes unchanged
    function convert(error, prefix) {
        if (!n.isStatus(error))
            return error;
        return new classes[error.kind](prefix + error.message);
    }

    function guarded(prefix, action) {
        try {
            return action();
        } catch (error) {
            throw convert(error, prefix);
        }
    }

    function brand(object, table, name) {
        var handle = table.get(object);
        if (handle === undefined)
            throw new TypeError('Receiver is not a ' + name);
        return handle;
    }

    function toIndex(value, what) {
        var number = Number(value);
        if (!Number.isFinite(number))
            throw new TypeError(what + ' must be convertible to a valid number');
        number = Math.trunc(number);
        if (number < 0 || number > 4294967295)
            throw new TypeError(what + ' must be in the unsigned long range');
        return number;
    }

    function toBytes(source, prefix) {
        if (source instanceof ArrayBuffer)
            return source;
        if (isView(source))
            return new Uint8Array(source.buffer, source.byteOffset, source.byteLength);
        throw new TypeError(prefix + 'Argument 0 must be a buffer source');
    }

    // values: JavaScript to the flat form and back
    function lowHigh(big) {
        var unsigned = BigInt.asUintN(64, big);
        return [Number(unsigned & MASK32), Number(unsigned >> 32n)];
    }

    function joinHighLow(low, high) {
        return BigInt.asIntN(64, (BigInt(high) << 32n) | BigInt(low));
    }

    var functionHandles = new WeakMap();
    var wrapperCache = new Map();

    function cached(handle, make) {
        var id = n.identity(handle);
        var weak = wrapperCache.get(id);
        var held = weak && weak.deref();
        if (held)
            return held;
        var made = make();
        wrapperCache.set(id, new WeakRef(made));
        return made;
    }

    function toFlat(type, value, out) {
        switch (type) {
        case 'i':
            out.push(value | 0);
            break;
        case 'l': {
            var pair = lowHigh(BigInt.asIntN(64, value));
            out.push(pair[0], pair[1]);
            break;
        }
        case 'f':
            out.push(Math.fround(value));
            break;
        case 'd':
            out.push(+value);
            break;
        case 'F':
            if (value === null) {
                out.push(null);
                break;
            }
            var handle = functionHandles.get(value);
            if (handle === undefined)
                throw new TypeError('argument is not an exported WebAssembly function or null');
            out.push(handle);
            break;
        default:
            out.push(value === null ? null : value);
            break;
        }
    }

    function fromFlat(type, flat, at) {
        switch (type) {
        case 'l':
            return joinHighLow(flat[at], flat[at + 1]);
        case 'F':
            return flat[at] === null ? null : functionFor(flat[at]);
        default:
            return flat[at];
        }
    }

    function width(type) {
        return type === 'l' ? 2 : 1;
    }

    // one object per engine function, so what a table, an export and an import give back for the same function compares equal
    function functionFor(handle) {
        return cached(handle, function () {
            var info = n.funcInfo(handle);
            var parameters = info[0], results = info[1];
            // an arrow function: an exported function is not a constructor
            var exported = (...args) => {
                var flat = [];
                for (var i = 0; i < parameters.length; ++i)
                    toFlat(parameters[i], args[i], flat);
                var answer;
                try {
                    answer = n.call(handle, flat);
                } catch (error) {
                    throw convert(error, '');
                }
                if (results.length === 0)
                    return undefined;
                if (results.length === 1)
                    return fromFlat(results[0], answer, 0);
                var values = [], at = 0;
                for (var j = 0; j < results.length; ++j) {
                    values.push(fromFlat(results[j], answer, at));
                    at += width(results[j]);
                }
                return values;
            };
            defineProperty(exported, 'name', { value: String(info[2]), configurable: true });
            defineProperty(exported, 'length', { value: parameters.length, configurable: true });
            functionHandles.set(exported, handle);
            return exported;
        });
    }

    // a page function made callable by the engine: flat values in, flat values out
    function hostFunction(callable, parameters, results) {
        return function (flat) {
            var args = [], at = 0;
            for (var i = 0; i < parameters.length; ++i) {
                args.push(fromFlat(parameters[i], flat, at));
                at += width(parameters[i]);
            }
            var answer = callable.apply(undefined, args);
            var out = [];
            if (results.length === 0)
                return out;
            if (results.length === 1) {
                toFlat(results[0], answer, out);
                return out;
            }
            if (answer === null || typeof answer !== 'object' && typeof answer !== 'function')
                throw new TypeError('multiple return values must be iterable');
            var list = Array.from(answer);
            if (list.length !== results.length)
                throw new TypeError('multiple return values have the wrong length');
            for (var j = 0; j < results.length; ++j)
                toFlat(results[j], list[j], out);
            return out;
        };
    }

    var moduleHandles = new WeakMap();
    var instanceHandles = new WeakMap();
    var memoryHandles = new WeakMap();
    var tableHandles = new WeakMap();
    var globalHandles = new WeakMap();

    var typeNames = { i: 'i32', l: 'i64', f: 'f32', d: 'f64', F: 'anyfunc', E: 'externref' };

    class Module {
        constructor(bytes) {
            if (new.target === undefined)
                throw new TypeError("WebAssembly.Module must be invoked with 'new'");
            if (arguments.length === 0)
                throw new TypeError('WebAssembly.Module(): Argument 0 must be a buffer source');
            var source = toBytes(bytes, 'WebAssembly.Module(): ');
            moduleHandles.set(this, guarded('WebAssembly.Module(): ', function () { return n.moduleCreate(source); }));
        }

        static imports(module) {
            var handle = brand(module, moduleHandles, 'WebAssembly.Module');
            return n.moduleImports(handle).map(function (entry) {
                return { module: entry[0], name: entry[1], kind: kinds[entry[2]] };
            });
        }

        static exports(module) {
            var handle = brand(module, moduleHandles, 'WebAssembly.Module');
            return n.moduleExports(handle).map(function (entry) {
                return { name: entry[0], kind: kinds[entry[1]] };
            });
        }

        static customSections(module, name) {
            var handle = brand(module, moduleHandles, 'WebAssembly.Module');
            if (arguments.length < 2)
                throw new TypeError('WebAssembly.Module.customSections(): Argument 1 is required');
            return n.customSections(handle, String(name));
        }
    }

    function importEntries(module, importObject) {
        var handle = moduleHandles.get(module);
        var wanted = n.moduleImports(handle);
        if (wanted.length && (importObject === undefined || importObject === null || typeof importObject !== 'object' && typeof importObject !== 'function'))
            throw new TypeError('WebAssembly.Instance(): Argument 1 must be an object');
        return wanted.map(function (entry, index) {
            var where = 'WebAssembly.Instance(): Import #' + index + ' "' + entry[0] + '"';
            var space = importObject[entry[0]];
            if (space === null || typeof space !== 'object' && typeof space !== 'function')
                throw new TypeError(where + ': module is not an object or function');
            var value = space[entry[1]];
            where += ' "' + entry[1] + '"';
            switch (entry[2]) {
            case 0:
                if (typeof value !== 'function')
                    throw new LinkError(where + ': function import requires a callable');
                if (functionHandles.has(value))
                    return functionHandles.get(value);
                return hostFunction(value, entry[3], entry[4]);
            case 1:
                if (!tableHandles.has(value))
                    throw new LinkError(where + ': table import requires a WebAssembly.Table');
                return tableHandles.get(value);
            case 2:
                if (!memoryHandles.has(value))
                    throw new LinkError(where + ': memory import must be a WebAssembly.Memory object');
                return memoryHandles.get(value);
            default:
                if (globalHandles.has(value))
                    return globalHandles.get(value);
                if (entry[6])
                    throw new LinkError(where + ': imported mutable global must be a WebAssembly.Global object');
                var type = entry[3];
                if (type === 'l') {
                    if (typeof value !== 'bigint')
                        throw new LinkError(where + ': global import must be a BigInt');
                } else if (type === 'i' || type === 'f' || type === 'd') {
                    if (typeof value !== 'number')
                        throw new LinkError(where + ': global import must be a number');
                } else if (type === 'F') {
                    if (value !== null && !functionHandles.has(value))
                        throw new LinkError(where + ': imported funcref global must be null or an exported function');
                }
                var flat = [];
                toFlat(type, value, flat);
                return n.globalCreate(type, false, flat);
            }
        });
    }

    function exportsOf(instanceHandle, moduleHandle) {
        var exported = Object.create(null);
        n.instanceExports(instanceHandle, moduleHandle).forEach(function (entry) {
            var handle = entry[2], value;
            switch (entry[1]) {
            case 0: value = functionFor(handle); break;
            case 1: value = tableFor(handle); break;
            case 2: value = memoryFor(handle); break;
            default: value = globalFor(handle); break;
            }
            defineProperty(exported, entry[0], { value: value, enumerable: true, writable: false, configurable: false });
        });
        return Object.freeze(exported);
    }

    function makeInstance(instance, module, importObject) {
        var handle = moduleHandles.get(module);
        var entries = importEntries(module, importObject);
        var made = guarded('WebAssembly.Instance(): ', function () { return n.instantiate(handle, entries); });
        instanceHandles.set(instance, made);
        var exported = exportsOf(made, handle);
        defineProperty(instance, 'exports', { value: exported, enumerable: true, configurable: true, writable: false });
    }

    class Instance {
        constructor(module, importObject) {
            if (new.target === undefined)
                throw new TypeError("WebAssembly.Instance must be invoked with 'new'");
            if (!moduleHandles.has(module))
                throw new TypeError('WebAssembly.Instance(): Argument 0 must be a WebAssembly.Module');
            makeInstance(this, module, importObject);
        }
    }

    function memoryFor(handle) {
        return cached(handle, function () {
            var memory = Object.create(Memory.prototype);
            memoryHandles.set(memory, handle);
            return memory;
        });
    }

    function readDescriptor(descriptor, what, field) {
        if (descriptor === null || typeof descriptor !== 'object')
            throw new TypeError(what + ': Argument 0 must be a ' + field + ' descriptor');
    }

    class Memory {
        constructor(descriptor) {
            if (new.target === undefined)
                throw new TypeError("WebAssembly.Memory must be invoked with 'new'");
            readDescriptor(descriptor, 'WebAssembly.Memory()', 'memory');
            if (descriptor.initial === undefined)
                throw new TypeError("WebAssembly.Memory(): Property 'initial' is required");
            var initial = toIndex(descriptor.initial, "Property 'initial'");
            var maximum = descriptor.maximum === undefined ? -1 : toIndex(descriptor.maximum, "Property 'maximum'");
            if (maximum !== -1 && maximum < initial)
                throw new RangeError("WebAssembly.Memory(): Property 'maximum': value " + maximum + ' is below the lower bound ' + initial);
            if (descriptor.shared)
                throw new TypeError('WebAssembly.Memory(): shared memory is not supported');
            var handle;
            try {
                handle = n.memoryCreate(initial, maximum);
            } catch (error) {
                throw convert(error, '');
            }
            memoryHandles.set(this, handle);
            wrapperCache.set(n.identity(handle), new WeakRef(this));
        }

        grow(delta) {
            var handle = brand(this, memoryHandles, 'WebAssembly.Memory');
            var old = n.memoryGrow(handle, toIndex(delta, 'Argument 0'));
            if (old < 0)
                throw new RangeError('WebAssembly.Memory.grow(): Maximum memory size exceeded');
            return old;
        }

        get buffer() {
            return n.memoryBuffer(brand(this, memoryHandles, 'WebAssembly.Memory'));
        }
    }

    function tableFor(handle) {
        return cached(handle, function () {
            var table = Object.create(Table.prototype);
            tableHandles.set(table, handle);
            return table;
        });
    }

    function elementLetter(name) {
        if (name === 'anyfunc' || name === 'funcref')
            return 'F';
        if (name === 'externref')
            return 'E';
        throw new TypeError("WebAssembly.Table(): Descriptor property 'element' must be a WebAssembly reference type");
    }

    function elementValue(handle, args, position) {
        var letter = n.tableType(handle);
        var flat = [];
        if (args.length > position)
            toFlat(letter, args[position], flat);
        else
            toFlat(letter, letter === 'F' ? null : undefined, flat);
        return flat[0];
    }

    class Table {
        constructor(descriptor, value) {
            if (new.target === undefined)
                throw new TypeError("WebAssembly.Table must be invoked with 'new'");
            readDescriptor(descriptor, 'WebAssembly.Table()', 'table');
            var letter = elementLetter(descriptor.element);
            if (descriptor.initial === undefined)
                throw new TypeError("WebAssembly.Table(): Property 'initial' is required");
            var initial = toIndex(descriptor.initial, "Property 'initial'");
            var maximum = descriptor.maximum === undefined ? -1 : toIndex(descriptor.maximum, "Property 'maximum'");
            if (maximum !== -1 && maximum < initial)
                throw new RangeError("WebAssembly.Table(): Property 'maximum': value " + maximum + ' is below the lower bound ' + initial);
            var fill = [];
            toFlat(letter, arguments.length > 1 ? value : (letter === 'F' ? null : undefined), fill);
            var handle;
            try {
                handle = n.tableCreate(letter, initial, maximum, fill[0]);
            } catch (error) {
                throw convert(error, '');
            }
            tableHandles.set(this, handle);
            wrapperCache.set(n.identity(handle), new WeakRef(this));
        }

        get length() {
            return n.tableSize(brand(this, tableHandles, 'WebAssembly.Table'));
        }

        get(index) {
            var handle = brand(this, tableHandles, 'WebAssembly.Table');
            var entry;
            try {
                entry = n.tableGet(handle, toIndex(index, 'Argument 0'));
            } catch (error) {
                throw convert(error, '');
            }
            return entry === null ? null : (n.tableType(handle) === 'F' ? functionFor(entry) : entry);
        }

        set(index, value) {
            var handle = brand(this, tableHandles, 'WebAssembly.Table');
            var position = toIndex(index, 'Argument 0');
            var flat = [];
            var letter = n.tableType(handle);
            toFlat(letter, arguments.length > 1 ? value : (letter === 'F' ? null : undefined), flat);
            try {
                n.tableSet(handle, position, flat[0]);
            } catch (error) {
                throw convert(error, '');
            }
        }

        grow(delta, value) {
            var handle = brand(this, tableHandles, 'WebAssembly.Table');
            var amount = toIndex(delta, 'Argument 0');
            var letter = n.tableType(handle);
            var flat = [];
            toFlat(letter, arguments.length > 1 ? value : (letter === 'F' ? null : undefined), flat);
            var old;
            try {
                old = n.tableGrow(handle, amount, flat[0]);
            } catch (error) {
                throw convert(error, '');
            }
            if (old < 0)
                throw new RangeError('WebAssembly.Table.grow(): failed to grow table by ' + amount);
            return old;
        }
    }

    function globalFor(handle) {
        return cached(handle, function () {
            var global = Object.create(Global.prototype);
            globalHandles.set(global, handle);
            return global;
        });
    }

    var letters = { i32: 'i', i64: 'l', f32: 'f', f64: 'd', externref: 'E', anyfunc: 'F', funcref: 'F' };

    function defaultOf(letter) {
        switch (letter) {
        case 'l': return 0n;
        case 'F': return null;
        case 'E': return undefined;
        default: return 0;
        }
    }

    class Global {
        constructor(descriptor, value) {
            if (new.target === undefined)
                throw new TypeError("WebAssembly.Global must be invoked with 'new'");
            readDescriptor(descriptor, 'WebAssembly.Global()', 'global');
            var letter = letters[String(descriptor.value)];
            if (!letter)
                throw new TypeError("WebAssembly.Global(): Descriptor property 'value' must be a WebAssembly type");
            var flat = [];
            toFlat(letter, value === undefined && letter !== 'E' ? defaultOf(letter) : value, flat);
            var handle;
            try {
                handle = n.globalCreate(letter, !!descriptor.mutable, flat);
            } catch (error) {
                throw convert(error, '');
            }
            globalHandles.set(this, handle);
            wrapperCache.set(n.identity(handle), new WeakRef(this));
        }

        get value() {
            var handle = brand(this, globalHandles, 'WebAssembly.Global');
            return fromFlat(n.globalType(handle)[0], n.globalGet(handle), 0);
        }

        set value(value) {
            var handle = brand(this, globalHandles, 'WebAssembly.Global');
            var info = n.globalType(handle);
            if (!info[1])
                throw new TypeError("WebAssembly.Global.value: Can't set the value of an immutable global.");
            var flat = [];
            toFlat(info[0], value, flat);
            n.globalSet(handle, flat);
        }

        valueOf() {
            return this.value;
        }
    }

    function defineTag(constructor, tag) {
        defineProperty(constructor.prototype, Symbol.toStringTag, { value: tag, configurable: true });
    }

    defineTag(Module, 'WebAssembly.Module');
    defineTag(Instance, 'WebAssembly.Instance');
    defineTag(Memory, 'WebAssembly.Memory');
    defineTag(Table, 'WebAssembly.Table');
    defineTag(Global, 'WebAssembly.Global');

    function validate(bytes) {
        if (arguments.length === 0)
            throw new TypeError('WebAssembly.validate(): Argument 0 must be a buffer source');
        return n.validate(toBytes(bytes, 'WebAssembly.validate(): '));
    }

    function compile(bytes) {
        return new Promise(function (resolve) {
            resolve(new Module(bytes));
        });
    }

    function instantiate(source, importObject) {
        return new Promise(function (resolve) {
            if (moduleHandles.has(source)) {
                resolve(new Instance(source, importObject));
                return;
            }
            var module = new Module(source);
            resolve({ module: module, instance: new Instance(module, importObject) });
        });
    }

    function streamed(response, name) {
        return Promise.resolve(response).then(function (value) {
            if (!value || typeof value.arrayBuffer !== 'function')
                throw new TypeError('WebAssembly.' + name + '(): Argument 0 must be a Response or a promise for one');
            if (!value.ok)
                throw new TypeError('WebAssembly.' + name + '(): HTTP status code is not ok');
            var type = value.headers && value.headers.get('content-type');
            if (!type || type.split(';')[0].trim().toLowerCase() !== 'application/wasm')
                throw new TypeError('WebAssembly.' + name + "(): Incorrect response MIME type. Expected 'application/wasm'.");
            return value.arrayBuffer();
        });
    }

    var WebAssembly = {};
    [['Module', Module], ['Instance', Instance], ['Memory', Memory], ['Table', Table], ['Global', Global],
        ['CompileError', CompileError], ['LinkError', LinkError], ['RuntimeError', RuntimeError]].forEach(function (pair) {
        defineProperty(WebAssembly, pair[0], { value: pair[1], writable: true, configurable: true, enumerable: false });
    });
    // the operations of a namespace are enumerable, its classes are not
    [['validate', validate], ['compile', compile], ['instantiate', instantiate]].forEach(function (pair) {
        defineProperty(WebAssembly, pair[0], { value: pair[1], writable: true, configurable: true, enumerable: true });
    });
    defineProperty(WebAssembly, 'compileStreaming', {
        value: function compileStreaming(response) { return streamed(response, 'compileStreaming').then(compile); },
        writable: true, configurable: true, enumerable: true
    });
    defineProperty(WebAssembly, 'instantiateStreaming', {
        value: function instantiateStreaming(response, importObject) { return streamed(response, 'instantiateStreaming').then(function (bytes) { return instantiate(bytes, importObject); }); },
        writable: true, configurable: true, enumerable: true
    });
    defineProperty(WebAssembly, Symbol.toStringTag, { value: 'WebAssembly', configurable: true });
    return WebAssembly;
})
