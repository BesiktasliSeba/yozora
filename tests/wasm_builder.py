"""a small assembler of WebAssembly binaries for the tests of the engine: the modules are written as bytes here so no wat2wasm is needed, and the same bytes go to the
engine and to V8 for comparison"""

I32, I64, F32, F64, FUNCREF, EXTERNREF = 0x7f, 0x7e, 0x7d, 0x7c, 0x70, 0x6f


def u(n):
    out = bytearray()
    while True:
        byte = n & 0x7f
        n >>= 7
        if n:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def s(n):
    out = bytearray()
    while True:
        byte = n & 0x7f
        n >>= 7
        if (n == 0 and not byte & 0x40) or (n == -1 and byte & 0x40):
            out.append(byte)
            return bytes(out)
        out.append(byte | 0x80)


def vec(items):
    return u(len(items)) + b"".join(items)


def name(text):
    raw = text.encode()
    return u(len(raw)) + raw


def section(number, body):
    return bytes([number]) + u(len(body)) + body


def functype(params, results):
    return b"\x60" + vec([bytes([p]) for p in params]) + vec([bytes([r]) for r in results])


def limits(minimum, maximum=None):
    if maximum is None:
        return b"\x00" + u(minimum)
    return b"\x01" + u(minimum) + u(maximum)


def body(code, locals_=()):
    groups = vec([u(count) + bytes([kind]) for count, kind in locals_])
    data = groups + code + b"\x0b"
    return u(len(data)) + data


class Module:
    """types, imports, functions, tables, memories, globals, exports, a start function, element and data segments, in the order the format wants"""

    def __init__(self):
        self.types = []
        self.imports = []
        self.funcs = []
        self.tables = []
        self.memories = []
        self.globals = []
        self.exports = []
        self.start = None
        self.elements = []
        self.data = []
        self.bodies = []
        self.customs = []
        self.function_imports = 0

    def type(self, params, results):
        entry = functype(params, results)
        if entry not in self.types:
            self.types.append(entry)
        return self.types.index(entry)

    def import_func(self, module, field, params, results):
        self.imports.append(name(module) + name(field) + b"\x00" + u(self.type(params, results)))
        self.function_imports += 1
        return self.function_imports - 1

    def import_memory(self, module, field, minimum, maximum=None):
        self.imports.append(name(module) + name(field) + b"\x02" + limits(minimum, maximum))

    def import_table(self, module, field, element, minimum, maximum=None):
        self.imports.append(name(module) + name(field) + b"\x01" + bytes([element]) + limits(minimum, maximum))

    def import_global(self, module, field, kind, mutable=False):
        self.imports.append(name(module) + name(field) + b"\x03" + bytes([kind, 1 if mutable else 0]))

    def func(self, params, results, code, locals_=(), export=None):
        self.funcs.append(u(self.type(params, results)))
        self.bodies.append(body(code, locals_))
        index = self.imported_funcs() + len(self.funcs) - 1
        if export:
            self.exports.append(name(export) + b"\x00" + u(index))
        return index

    def imported_funcs(self):
        return self.function_imports

    def table(self, element, minimum, maximum=None, export=None):
        self.tables.append(bytes([element]) + limits(minimum, maximum))
        if export:
            self.exports.append(name(export) + b"\x01" + u(len(self.tables) - 1))

    def memory(self, minimum, maximum=None, export=None):
        self.memories.append(limits(minimum, maximum))
        if export:
            self.exports.append(name(export) + b"\x02" + u(len(self.memories) - 1))

    def global_(self, kind, mutable, init, export=None):
        self.globals.append(bytes([kind, 1 if mutable else 0]) + init + b"\x0b")
        if export:
            self.exports.append(name(export) + b"\x03" + u(len(self.globals) - 1))

    def export(self, label, kind, index):
        self.exports.append(name(label) + bytes([kind]) + u(index))

    def element(self, offset, indices):
        self.elements.append(b"\x00" + b"\x41" + s(offset) + b"\x0b" + vec([u(i) for i in indices]))

    def data_segment(self, offset, payload):
        self.data.append(b"\x00" + b"\x41" + s(offset) + b"\x0b" + u(len(payload)) + payload)

    def custom(self, label, payload):
        self.customs.append(section(0, name(label) + payload))

    def build(self):
        out = b"\0asm\x01\x00\x00\x00"
        if self.types:
            out += section(1, vec(self.types))
        if self.imports:
            out += section(2, vec(self.imports))
        if self.funcs:
            out += section(3, vec(self.funcs))
        if self.tables:
            out += section(4, vec(self.tables))
        if self.memories:
            out += section(5, vec(self.memories))
        if self.globals:
            out += section(6, vec(self.globals))
        if self.exports:
            out += section(7, vec(self.exports))
        if self.start is not None:
            out += section(8, u(self.start))
        if self.elements:
            out += section(9, vec(self.elements))
        if self.bodies:
            out += section(10, vec(self.bodies))
        if self.data:
            out += section(11, vec(self.data))
        return out + b"".join(self.customs)


# instructions the tests use
LOCAL_GET = lambda i: b"\x20" + u(i)
LOCAL_SET = lambda i: b"\x21" + u(i)
LOCAL_TEE = lambda i: b"\x22" + u(i)
GLOBAL_GET = lambda i: b"\x23" + u(i)
GLOBAL_SET = lambda i: b"\x24" + u(i)
I32_CONST = lambda v: b"\x41" + s(v)
I64_CONST = lambda v: b"\x42" + s(v)
CALL = lambda i: b"\x10" + u(i)
CALL_INDIRECT = lambda type_index, table=0: b"\x11" + u(type_index) + u(table)
I32_ADD, I32_SUB, I32_MUL, I32_DIV_S, I32_EQZ, I32_LT_S = b"\x6a", b"\x6b", b"\x6c", b"\x6d", b"\x45", b"\x48"
I64_ADD, I64_SUB, I64_MUL, I64_EQZ = b"\x7c", b"\x7d", b"\x7e", b"\x50"
F32_ADD, F64_ADD, F64_MUL = b"\x92", b"\xa0", b"\xa2"
I32_LOAD = lambda offset=0: b"\x28\x02" + u(offset)
I32_STORE = lambda offset=0: b"\x36\x02" + u(offset)
I32_LOAD8_U = lambda offset=0: b"\x2d\x00" + u(offset)
MEMORY_SIZE, MEMORY_GROW = b"\x3f\x00", b"\x40\x00"
UNREACHABLE, DROP, RETURN = b"\x00", b"\x1a", b"\x0f"
BLOCK = lambda result=0x40: b"\x02" + bytes([result])
LOOP = lambda result=0x40: b"\x03" + bytes([result])
IF = lambda result=0x40: b"\x04" + bytes([result])
ELSE, END = b"\x05", b"\x0b"
BR = lambda depth: b"\x0c" + u(depth)
BR_IF = lambda depth: b"\x0d" + u(depth)
REF_NULL = lambda kind: b"\xd0" + bytes([kind])
REF_FUNC = lambda i: b"\xd2" + u(i)
TABLE_GET = lambda i=0: b"\x25" + u(i)
TABLE_SET = lambda i=0: b"\x26" + u(i)
