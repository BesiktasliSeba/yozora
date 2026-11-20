/* decoding, validation and compilation of a module. the binary is read once with every size checked against
   the bytes left, each body is type checked as the specification does and compiled in the same pass to a flat
   array of 32 bit words with branch targets and stack heights already worked out, so the interpreter never
   scans for the end of a block */

#include "yozora_wasm_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TYPES 100000u
#define MAX_FUNCTIONS 1000000u
#define MAX_LOCALS 50000u
#define MAX_IMPORTS 100000u
#define MAX_GLOBALS 1000000u
#define MAX_SEGMENTS 100000u
#define MAX_TABLES 100u
#define MAX_NESTING 10000u
#define MAX_FUNCTION_CODE_WORDS (64u * 1024u * 1024u)

typedef struct {
    const uint8_t *begin, *p, *end;
    yozora_wasm_status status;
    char *error;
    size_t error_capacity;
} reader;

static void fail(reader *r, yozora_wasm_status status, const char *format, ...) YW_PRINTF(3, 4);
static void fail(reader *r, yozora_wasm_status status, const char *format, ...)
{
    if (r->status != YOZORA_WASM_OK)
        return;
    r->status = status;
    if (r->error && r->error_capacity) {
        va_list arguments;
        int written;
        va_start(arguments, format);
        written = vsnprintf(r->error, r->error_capacity, format, arguments);
        va_end(arguments);
        if (written >= 0 && (size_t)written >= r->error_capacity)
            r->error[r->error_capacity - 1] = 0;
    }
}

static int ok(const reader *r) { return r->status == YOZORA_WASM_OK; }

static uint8_t read_byte(reader *r)
{
    if (r->p >= r->end) {
        fail(r, YOZORA_WASM_MALFORMED, "unexpected end at offset %lu", (unsigned long)(r->p - r->begin));
        return 0;
    }
    return *r->p++;
}

static uint64_t read_leb(reader *r, unsigned bits, int is_signed)
{
    uint64_t result = 0;
    unsigned shift = 0, i, maximum = (bits + 6) / 7;
    for (i = 0;; ++i) {
        uint8_t byte;
        if (i == maximum) {
            fail(r, YOZORA_WASM_MALFORMED, "a number is longer than %u bytes at offset %lu", maximum, (unsigned long)(r->p - r->begin));
            return 0;
        }
        byte = read_byte(r);
        if (!ok(r))
            return 0;
        result |= (uint64_t)(byte & 0x7f) << shift;
        shift += 7;
        if (byte & 0x80)
            continue;
        if (i == maximum - 1) {
            /* the bits of the last byte above the width must be zero, or for a signed number a copy of the sign */
            unsigned used = bits - 7 * i;
            if (used < 7) {
                uint8_t payload = (uint8_t)(byte & 0x7f);
                uint8_t mask = (uint8_t)((0x7f << used) & 0x7f);
                uint8_t high = (uint8_t)(payload & mask);
                int bad;
                if (is_signed)
                    bad = ((payload >> (used - 1)) & 1) ? high != mask : high != 0;
                else
                    bad = high != 0;
                if (bad) {
                    fail(r, YOZORA_WASM_MALFORMED, "a number does not fit %u bits at offset %lu", bits, (unsigned long)(r->p - r->begin));
                    return 0;
                }
            }
        }
        if (is_signed && shift < 64 && (byte & 0x40))
            result |= ~(uint64_t)0 << shift;
        return result;
    }
}

static uint32_t read_u32(reader *r) { return (uint32_t)read_leb(r, 32, 0); }
static int32_t read_s32(reader *r) { return (int32_t)(int64_t)read_leb(r, 32, 1); }
static int64_t read_s64(reader *r) { return (int64_t)read_leb(r, 64, 1); }
static int64_t read_s33(reader *r) { return (int64_t)read_leb(r, 33, 1); }

static uint32_t read_fixed32(reader *r)
{
    uint32_t v = 0;
    unsigned i;
    for (i = 0; i < 4; ++i)
        v |= (uint32_t)read_byte(r) << (8 * i);
    return v;
}

static uint64_t read_fixed64(reader *r)
{
    uint64_t lo = read_fixed32(r);
    uint64_t hi = read_fixed32(r);
    return lo | (hi << 32);
}

static int valid_utf8(const uint8_t *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        unsigned extra;
        uint32_t cp;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) {
            extra = 1;
            cp = c & 0x1f;
        } else if (c >= 0xe0 && c <= 0xef) {
            extra = 2;
            cp = c & 0x0f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            extra = 3;
            cp = c & 0x07;
        } else
            return 0;
        if (i + extra >= n)
            return 0;
        {
            unsigned k;
            for (k = 1; k <= extra; ++k) {
                if ((s[i + k] & 0xc0) != 0x80)
                    return 0;
                cp = (cp << 6) | (s[i + k] & 0x3f);
            }
        }
        if ((extra == 2 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) || (extra == 3 && (cp < 0x10000 || cp > 0x10ffff)))
            return 0;
        i += extra + 1;
    }
    return 1;
}

/* a name, copied and zero terminated */
static char *read_name(reader *r)
{
    uint32_t n = read_u32(r);
    char *out;
    if (!ok(r))
        return NULL;
    if (n > (uint32_t)(r->end - r->p)) {
        fail(r, YOZORA_WASM_MALFORMED, "a name is longer than the section");
        return NULL;
    }
    if (!valid_utf8(r->p, n)) {
        fail(r, YOZORA_WASM_MALFORMED, "a name is not UTF-8");
        return NULL;
    }
    out = (char *)malloc((size_t)n + 1);
    if (!out) {
        fail(r, YOZORA_WASM_NO_MEMORY, "out of memory");
        return NULL;
    }
    memcpy(out, r->p, n);
    out[n] = 0;
    r->p += n;
    return out;
}

static void *grow_array(reader *r, void *old, size_t count, size_t element)
{
    void *p;
    if (count && element > (size_t)-1 / count) {
        fail(r, YOZORA_WASM_TOO_LARGE, "too many elements");
        return old;
    }
    p = realloc(old, count * element);
    if (!p && count) {
        fail(r, YOZORA_WASM_NO_MEMORY, "out of memory");
        return old;
    }
    return p;
}

void yw_functype_free(yw_functype *t)
{
    free(t->params);
    free(t->results);
    t->params = t->results = NULL;
}

static int is_numtype(uint8_t t) { return t == YOZORA_WASM_I32 || t == YOZORA_WASM_I64 || t == YOZORA_WASM_F32 || t == YOZORA_WASM_F64; }
static int is_reftype(uint8_t t) { return t == YOZORA_WASM_FUNCREF || t == YOZORA_WASM_EXTERNREF; }

static uint8_t read_valtype(reader *r)
{
    uint8_t t = read_byte(r);
    if (!ok(r))
        return 0;
    if (t == 0x7b) {
        fail(r, YOZORA_WASM_UNSUPPORTED, "vector types are not supported");
        return 0;
    }
    if (!is_numtype(t) && !is_reftype(t)) {
        fail(r, YOZORA_WASM_MALFORMED, "unknown value type 0x%02x", t);
        return 0;
    }
    return t;
}

static uint8_t read_reftype(reader *r)
{
    uint8_t t = read_byte(r);
    if (ok(r) && !is_reftype(t)) {
        fail(r, YOZORA_WASM_MALFORMED, "unknown reference type 0x%02x", t);
        return 0;
    }
    return t;
}

static void read_limits(reader *r, uint32_t cap, uint32_t *minimum, uint32_t *maximum, uint8_t *has_maximum, const char *what)
{
    uint8_t flags = read_byte(r);
    *has_maximum = 0;
    *maximum = 0;
    if (!ok(r))
        return;
    if (flags > 1) {
        if (flags & 0x02)
            fail(r, YOZORA_WASM_UNSUPPORTED, "shared %s are not supported", what);
        else if (flags & 0x04)
            fail(r, YOZORA_WASM_UNSUPPORTED, "64 bit %s are not supported", what);
        else
            fail(r, YOZORA_WASM_MALFORMED, "bad limits flags 0x%02x", flags);
        return;
    }
    *minimum = read_u32(r);
    if (flags & 1) {
        *has_maximum = 1;
        *maximum = read_u32(r);
    }
    if (!ok(r))
        return;
    if (*minimum > cap || (*has_maximum && *maximum > cap)) {
        fail(r, YOZORA_WASM_INVALID, "%s limit beyond %u", what, cap);
        return;
    }
    if (*has_maximum && *maximum < *minimum)
        fail(r, YOZORA_WASM_INVALID, "%s maximum below minimum", what);
}

/* a constant expression of one instruction followed by end */
static yw_const read_const(reader *r, const yozora_wasm_module *m, uint8_t expected, uint32_t globals_visible)
{
    yw_const c;
    uint8_t op = read_byte(r);
    memset(&c, 0, sizeof(c));
    if (!ok(r))
        return c;
    switch (op) {
    case 0x41:
        c.op = YW_CONST_I32;
        c.type = YOZORA_WASM_I32;
        c.bits = (uint32_t)read_s32(r);
        break;
    case 0x42:
        c.op = YW_CONST_I64;
        c.type = YOZORA_WASM_I64;
        c.bits = (uint64_t)read_s64(r);
        break;
    case 0x43:
        c.op = YW_CONST_F32;
        c.type = YOZORA_WASM_F32;
        c.bits = read_fixed32(r);
        break;
    case 0x44:
        c.op = YW_CONST_F64;
        c.type = YOZORA_WASM_F64;
        c.bits = read_fixed64(r);
        break;
    case 0x23: {
        uint32_t g = read_u32(r);
        c.op = YW_CONST_GLOBAL;
        c.bits = g;
        if (!ok(r))
            break;
        if (g >= globals_visible || g >= m->global_count) {
            fail(r, YOZORA_WASM_INVALID, "a constant reads global %u that is not an import", g);
            break;
        }
        if (m->globals[g].is_mutable) {
            fail(r, YOZORA_WASM_INVALID, "a constant reads a mutable global");
            break;
        }
        c.type = m->globals[g].type;
        break;
    }
    case 0xd0: {
        uint8_t t = read_reftype(r);
        c.op = YW_CONST_REF_NULL;
        c.type = t;
        break;
    }
    case 0xd2: {
        uint32_t f = read_u32(r);
        c.op = YW_CONST_REF_FUNC;
        c.bits = f;
        c.type = YOZORA_WASM_FUNCREF;
        if (ok(r) && f >= m->func_count)
            fail(r, YOZORA_WASM_INVALID, "a constant names function %u that does not exist", f);
        break;
    }
    default:
        if (op == 0x6a || op == 0x6b || op == 0x6c || op == 0x7c || op == 0x7d || op == 0x7e)
            fail(r, YOZORA_WASM_UNSUPPORTED, "extended constant expressions are not supported");
        else
            fail(r, YOZORA_WASM_INVALID, "instruction 0x%02x in a constant expression", op);
        return c;
    }
    if (ok(r) && read_byte(r) != 0x0b)
        fail(r, YOZORA_WASM_INVALID, "a constant expression does not end after one instruction");
    if (ok(r) && c.type != expected)
        fail(r, YOZORA_WASM_INVALID, "a constant has type 0x%02x, 0x%02x is needed", c.type, expected);
    return c;
}

/* a one result block type points at its own byte here, the frames move when the frame stack grows so they
   cannot hold the pointer */
static const uint8_t type_bytes[256] = {
0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};

typedef struct {
    uint8_t op; /* 0 function, 1 block, 2 loop, 3 if, 4 else */
    uint32_t nparams, nresults;
    const uint8_t *params, *results;
    uint32_t height;
    uint32_t patches;
    uint32_t loop_pc;
    uint32_t else_patch;
    uint8_t unreachable;
    uint8_t skip;
} ctrl;

typedef struct {
    reader *r;
    yozora_wasm_module *m;
    const uint8_t *declared;
    uint8_t *locals;
    uint32_t local_count;
    uint8_t *values;
    uint32_t value_count, value_capacity;
    ctrl *ctrls;
    uint32_t ctrl_count, ctrl_capacity;
    uint32_t *code;
    uint32_t code_length, code_capacity;
    uint32_t max_height;
    const yw_functype *type;
} compiler;

static int emitting(const compiler *c)
{
    const ctrl *f = &c->ctrls[c->ctrl_count - 1];
    return !f->skip && !f->unreachable;
}

static void emit(compiler *c, uint32_t word)
{
    if (!ok(c->r))
        return;
    if (c->code_length == c->code_capacity) {
        uint32_t capacity = c->code_capacity ? c->code_capacity * 2 : 256;
        uint32_t *p;
        if (capacity > MAX_FUNCTION_CODE_WORDS || capacity < c->code_capacity) {
            fail(c->r, YOZORA_WASM_TOO_LARGE, "a function is too large");
            return;
        }
        p = (uint32_t *)realloc(c->code, (size_t)capacity * sizeof(uint32_t));
        if (!p) {
            fail(c->r, YOZORA_WASM_NO_MEMORY, "out of memory");
            return;
        }
        c->code = p;
        c->code_capacity = capacity;
    }
    c->code[c->code_length++] = word;
}

static void push_value(compiler *c, uint8_t t)
{
    if (!ok(c->r))
        return;
    if (c->value_count == c->value_capacity) {
        uint32_t capacity = c->value_capacity ? c->value_capacity * 2 : 64;
        uint8_t *p = (uint8_t *)realloc(c->values, capacity);
        if (!p) {
            fail(c->r, YOZORA_WASM_NO_MEMORY, "out of memory");
            return;
        }
        c->values = p;
        c->value_capacity = capacity;
    }
    c->values[c->value_count++] = t;
    if (c->value_count > c->max_height)
        c->max_height = c->value_count;
}

/* t is 0 for a value of any type: what a pop finds in unreachable code */
static uint8_t pop_value(compiler *c, uint8_t expected)
{
    ctrl *f = &c->ctrls[c->ctrl_count - 1];
    uint8_t t;
    if (!ok(c->r))
        return 0;
    if (c->value_count == f->height) {
        if (f->unreachable)
            return expected;
        fail(c->r, YOZORA_WASM_INVALID, "the stack is empty where a value of type 0x%02x is needed (offset %lu)", expected, (unsigned long)(c->r->p - c->r->begin));
        return 0;
    }
    t = c->values[--c->value_count];
    if (expected && t && t != expected) {
        fail(c->r, YOZORA_WASM_INVALID, "a value of type 0x%02x where 0x%02x is needed (offset %lu)", t, expected, (unsigned long)(c->r->p - c->r->begin));
        return 0;
    }
    return t ? t : expected;
}

static void pop_types(compiler *c, const uint8_t *types, uint32_t n)
{
    while (n--)
        pop_value(c, types[n]);
}

static void push_types(compiler *c, const uint8_t *types, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        push_value(c, types[i]);
}

static void push_ctrl(compiler *c, uint8_t op, uint32_t nparams, const uint8_t *params, uint32_t nresults, const uint8_t *results)
{
    ctrl *f;
    ctrl *parent = c->ctrl_count ? &c->ctrls[c->ctrl_count - 1] : NULL;
    if (!ok(c->r))
        return;
    if (c->ctrl_count >= MAX_NESTING) {
        fail(c->r, YOZORA_WASM_TOO_LARGE, "blocks are nested too deep");
        return;
    }
    if (c->ctrl_count == c->ctrl_capacity) {
        uint32_t capacity = c->ctrl_capacity ? c->ctrl_capacity * 2 : 16;
        ctrl *p = (ctrl *)realloc(c->ctrls, (size_t)capacity * sizeof(ctrl));
        if (!p) {
            fail(c->r, YOZORA_WASM_NO_MEMORY, "out of memory");
            return;
        }
        c->ctrls = p;
        c->ctrl_capacity = capacity;
        parent = c->ctrl_count ? &c->ctrls[c->ctrl_count - 1] : NULL;
    }
    f = &c->ctrls[c->ctrl_count++];
    memset(f, 0, sizeof(*f));
    f->op = op;
    f->nparams = nparams;
    f->params = params;
    f->nresults = nresults;
    f->results = results;
    f->height = c->value_count;
    f->skip = parent ? (uint8_t)(parent->skip || parent->unreachable) : 0;
    push_types(c, params, nparams);
}

/* the types a branch to a label carries: a loop takes its parameters, the rest their results */
static uint32_t label_arity(const ctrl *f) { return f->op == 2 ? f->nparams : f->nresults; }
static const uint8_t *label_types(const ctrl *f) { return f->op == 2 ? f->params : f->results; }

static void set_unreachable(compiler *c)
{
    ctrl *f = &c->ctrls[c->ctrl_count - 1];
    c->value_count = f->height;
    f->unreachable = 1;
}

/* the word that holds a forward branch target heads a list through the code and stores the previous link, so
   a patch needs no extra memory */
static void emit_target(compiler *c, ctrl *f)
{
    if (f->op == 2) {
        emit(c, f->loop_pc);
        return;
    }
    emit(c, f->patches);
    f->patches = c->code_length;
}

static void patch_list(compiler *c, uint32_t head, uint32_t target)
{
    while (head && ok(c->r)) {
        uint32_t index = head - 1;
        head = c->code[index];
        c->code[index] = target;
    }
}

static uint32_t stack_base(const compiler *c) { return c->local_count; }

static void emit_branch(compiler *c, uint32_t op, uint32_t depth)
{
    ctrl *target = &c->ctrls[c->ctrl_count - 1 - depth];
    emit(c, op);
    emit_target(c, target);
    emit(c, stack_base(c) + target->height);
    emit(c, label_arity(target));
}

static int read_blocktype(compiler *c, uint32_t *nparams, const uint8_t **params, uint32_t *nresults, const uint8_t **results)
{
    reader *r = c->r;
    int64_t t = read_s33(r);
    if (!ok(r))
        return 0;
    if (t == -64) { /* 0x40 */
        *nparams = 0;
        *params = NULL;
        *nresults = 0;
        *results = NULL;
        return 1;
    }
    if (t < 0) {
        uint8_t byte = (uint8_t)(t & 0x7f);
        if (byte == 0x7b) {
            fail(r, YOZORA_WASM_UNSUPPORTED, "vector types are not supported");
            return 0;
        }
        if (!is_numtype(byte) && !is_reftype(byte)) {
            fail(r, YOZORA_WASM_MALFORMED, "unknown block type");
            return 0;
        }
        *nparams = 0;
        *params = NULL;
        *nresults = 1;
        *results = &type_bytes[byte];
        return 1;
    }
    if ((uint64_t)t >= c->m->type_count) {
        fail(r, YOZORA_WASM_INVALID, "block type %lld does not exist", (long long)t);
        return 0;
    }
    *nparams = c->m->types[t].nparams;
    *params = c->m->types[t].params;
    *nresults = c->m->types[t].nresults;
    *results = c->m->types[t].results;
    return 1;
}

static int need_memory(compiler *c)
{
    if (c->m->memory_count == 0) {
        fail(c->r, YOZORA_WASM_INVALID, "a memory instruction in a module without memory");
        return 0;
    }
    return 1;
}

static int check_table(compiler *c, uint32_t t)
{
    if (t >= c->m->table_count) {
        fail(c->r, YOZORA_WASM_INVALID, "table %u does not exist", t);
        return 0;
    }
    return 1;
}

/* one memory instruction: the alignment may not be above the natural one, the offset stays an unsigned 32 bit number */
static void memory_access(compiler *c, uint32_t op, unsigned natural, uint8_t type, int is_store)
{
    uint32_t align = read_u32(c->r);
    uint32_t offset;
    if (!ok(c->r))
        return;
    if (align & 0x40) {
        fail(c->r, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
        return;
    }
    offset = read_u32(c->r);
    if (!ok(c->r) || !need_memory(c))
        return;
    if (align > natural) {
        fail(c->r, YOZORA_WASM_INVALID, "an alignment above the natural one");
        return;
    }
    if (is_store) {
        pop_value(c, type);
        pop_value(c, YOZORA_WASM_I32);
    } else {
        pop_value(c, YOZORA_WASM_I32);
        push_value(c, type);
    }
    if (emitting(c)) {
        emit(c, op);
        emit(c, offset);
    }
}

/* the operand and result types of the numeric instructions. returns the number of operands, 0 for an opcode that is not numeric */
static int numeric_signature(uint32_t op, uint8_t *a, uint8_t *b, uint8_t *out)
{
    const uint8_t i32 = YOZORA_WASM_I32, i64 = YOZORA_WASM_I64, f32 = YOZORA_WASM_F32, f64 = YOZORA_WASM_F64;
    if (op == 0x45) { *a = i32; *out = i32; return 1; }
    if (op >= 0x46 && op <= 0x4f) { *a = *b = i32; *out = i32; return 2; }
    if (op == 0x50) { *a = i64; *out = i32; return 1; }
    if (op >= 0x51 && op <= 0x5a) { *a = *b = i64; *out = i32; return 2; }
    if (op >= 0x5b && op <= 0x60) { *a = *b = f32; *out = i32; return 2; }
    if (op >= 0x61 && op <= 0x66) { *a = *b = f64; *out = i32; return 2; }
    if (op >= 0x67 && op <= 0x69) { *a = i32; *out = i32; return 1; }
    if (op >= 0x6a && op <= 0x78) { *a = *b = i32; *out = i32; return 2; }
    if (op >= 0x79 && op <= 0x7b) { *a = i64; *out = i64; return 1; }
    if (op >= 0x7c && op <= 0x8a) { *a = *b = i64; *out = i64; return 2; }
    if (op >= 0x8b && op <= 0x91) { *a = f32; *out = f32; return 1; }
    if (op >= 0x92 && op <= 0x98) { *a = *b = f32; *out = f32; return 2; }
    if (op >= 0x99 && op <= 0x9f) { *a = f64; *out = f64; return 1; }
    if (op >= 0xa0 && op <= 0xa6) { *a = *b = f64; *out = f64; return 2; }
    switch (op) {
    case 0xa7: *a = i64; *out = i32; return 1;
    case 0xa8: case 0xa9: *a = f32; *out = i32; return 1;
    case 0xaa: case 0xab: *a = f64; *out = i32; return 1;
    case 0xac: case 0xad: *a = i32; *out = i64; return 1;
    case 0xae: case 0xaf: *a = f32; *out = i64; return 1;
    case 0xb0: case 0xb1: *a = f64; *out = i64; return 1;
    case 0xb2: case 0xb3: *a = i32; *out = f32; return 1;
    case 0xb4: case 0xb5: *a = i64; *out = f32; return 1;
    case 0xb6: *a = f64; *out = f32; return 1;
    case 0xb7: case 0xb8: *a = i32; *out = f64; return 1;
    case 0xb9: case 0xba: *a = i64; *out = f64; return 1;
    case 0xbb: *a = f32; *out = f64; return 1;
    case 0xbc: *a = f32; *out = i32; return 1;
    case 0xbd: *a = f64; *out = i64; return 1;
    case 0xbe: *a = i32; *out = f32; return 1;
    case 0xbf: *a = i64; *out = f64; return 1;
    case 0xc0: case 0xc1: *a = i32; *out = i32; return 1;
    case 0xc2: case 0xc3: case 0xc4: *a = i64; *out = i64; return 1;
    default: return 0;
    }
}

static int saturating_signature(uint32_t n, uint8_t *a, uint8_t *out)
{
    switch (n) {
    case 0: case 1: *a = YOZORA_WASM_F32; *out = YOZORA_WASM_I32; return 1;
    case 2: case 3: *a = YOZORA_WASM_F64; *out = YOZORA_WASM_I32; return 1;
    case 4: case 5: *a = YOZORA_WASM_F32; *out = YOZORA_WASM_I64; return 1;
    case 6: case 7: *a = YOZORA_WASM_F64; *out = YOZORA_WASM_I64; return 1;
    default: return 0;
    }
}

static int is_declared(const compiler *c, uint32_t f) { return (c->declared[f >> 3] >> (f & 7)) & 1; }

static void compile_prefix(compiler *c)
{
    reader *r = c->r;
    uint32_t n = read_u32(r);
    uint8_t a = 0, out = 0;
    if (!ok(r))
        return;
    if (n <= 7) {
        saturating_signature(n, &a, &out);
        pop_value(c, a);
        push_value(c, out);
        if (emitting(c))
            emit(c, YW_OP_PREFIX + n);
        return;
    }
    switch (n) {
    case 8: { /* memory.init */
        uint32_t data = read_u32(r);
        uint8_t memory = read_byte(r);
        if (!ok(r))
            return;
        if (memory != 0) {
            fail(r, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
            return;
        }
        if (!need_memory(c))
            return;
        if (!c->m->has_data_count) {
            fail(r, YOZORA_WASM_INVALID, "memory.init needs a data count section");
            return;
        }
        if (data >= c->m->data_count) {
            fail(r, YOZORA_WASM_INVALID, "data segment %u does not exist", data);
            return;
        }
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 8);
            emit(c, data);
        }
        return;
    }
    case 9: { /* data.drop */
        uint32_t data = read_u32(r);
        if (!ok(r))
            return;
        if (!c->m->has_data_count) {
            fail(r, YOZORA_WASM_INVALID, "data.drop needs a data count section");
            return;
        }
        if (data >= c->m->data_count) {
            fail(r, YOZORA_WASM_INVALID, "data segment %u does not exist", data);
            return;
        }
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 9);
            emit(c, data);
        }
        return;
    }
    case 10: { /* memory.copy */
        uint8_t m0 = read_byte(r), m1 = read_byte(r);
        if (!ok(r))
            return;
        if (m0 != 0 || m1 != 0) {
            fail(r, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
            return;
        }
        if (!need_memory(c))
            return;
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        if (emitting(c))
            emit(c, YW_OP_PREFIX + 10);
        return;
    }
    case 11: { /* memory.fill */
        uint8_t m0 = read_byte(r);
        if (!ok(r))
            return;
        if (m0 != 0) {
            fail(r, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
            return;
        }
        if (!need_memory(c))
            return;
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        if (emitting(c))
            emit(c, YW_OP_PREFIX + 11);
        return;
    }
    case 12: { /* table.init */
        uint32_t elem = read_u32(r), table = read_u32(r);
        if (!ok(r) || !check_table(c, table))
            return;
        if (elem >= c->m->elem_count) {
            fail(r, YOZORA_WASM_INVALID, "element segment %u does not exist", elem);
            return;
        }
        if (c->m->elems[elem].element_type != c->m->tables[table].element_type) {
            fail(r, YOZORA_WASM_INVALID, "table.init between tables of other types");
            return;
        }
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 12);
            emit(c, elem);
            emit(c, table);
        }
        return;
    }
    case 13: { /* elem.drop */
        uint32_t elem = read_u32(r);
        if (!ok(r))
            return;
        if (elem >= c->m->elem_count) {
            fail(r, YOZORA_WASM_INVALID, "element segment %u does not exist", elem);
            return;
        }
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 13);
            emit(c, elem);
        }
        return;
    }
    case 14: { /* table.copy */
        uint32_t dst = read_u32(r), src = read_u32(r);
        if (!ok(r) || !check_table(c, dst) || !check_table(c, src))
            return;
        if (c->m->tables[dst].element_type != c->m->tables[src].element_type) {
            fail(r, YOZORA_WASM_INVALID, "table.copy between tables of other types");
            return;
        }
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, YOZORA_WASM_I32);
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 14);
            emit(c, dst);
            emit(c, src);
        }
        return;
    }
    case 15: { /* table.grow */
        uint32_t table = read_u32(r);
        if (!ok(r) || !check_table(c, table))
            return;
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, c->m->tables[table].element_type);
        push_value(c, YOZORA_WASM_I32);
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 15);
            emit(c, table);
        }
        return;
    }
    case 16: { /* table.size */
        uint32_t table = read_u32(r);
        if (!ok(r) || !check_table(c, table))
            return;
        push_value(c, YOZORA_WASM_I32);
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 16);
            emit(c, table);
        }
        return;
    }
    case 17: { /* table.fill */
        uint32_t table = read_u32(r);
        if (!ok(r) || !check_table(c, table))
            return;
        pop_value(c, YOZORA_WASM_I32);
        pop_value(c, c->m->tables[table].element_type);
        pop_value(c, YOZORA_WASM_I32);
        if (emitting(c)) {
            emit(c, YW_OP_PREFIX + 17);
            emit(c, table);
        }
        return;
    }
    default:
        fail(r, YOZORA_WASM_MALFORMED, "unknown instruction 0xfc 0x%x", n);
        return;
    }
}

static void compile_body(compiler *c, uint32_t defined_index)
{
    reader *r = c->r;
    yozora_wasm_module *m = c->m;
    uint32_t function_index = m->imported_funcs + defined_index;
    const yw_functype *type = &m->types[m->func_types[function_index]];
    (void)function_index;

    push_ctrl(c, 0, 0, NULL, type->nresults, type->results);
    while (ok(r) && c->ctrl_count) {
        uint32_t op = read_byte(r);
        uint8_t a = 0, b = 0, out = 0;
        int operands;
        if (!ok(r))
            break;
        if (op == 0xfc) {
            compile_prefix(c);
            continue;
        }
        if (op == 0xfd) {
            fail(r, YOZORA_WASM_UNSUPPORTED, "vector instructions are not supported");
            break;
        }
        if (op == 0xfe) {
            fail(r, YOZORA_WASM_UNSUPPORTED, "atomic instructions are not supported");
            break;
        }
        switch (op) {
        case 0x00:
            if (emitting(c))
                emit(c, YW_OP_UNREACHABLE);
            set_unreachable(c);
            break;
        case 0x01:
            break;
        case 0x02:
        case 0x03:
        case 0x04: {
            uint32_t np, nr;
            const uint8_t *ps, *rs;
            uint8_t kind = (uint8_t)(op == 0x02 ? 1 : (op == 0x03 ? 2 : 3));
            if (!read_blocktype(c, &np, &ps, &nr, &rs))
                break;
            if (op == 0x04)
                pop_value(c, YOZORA_WASM_I32);
            pop_types(c, ps, np);
            if (!ok(r))
                break;
            push_ctrl(c, kind, np, ps, nr, rs);
            if (!ok(r))
                break;
            {
                ctrl *f = &c->ctrls[c->ctrl_count - 1];
                if (kind == 2)
                    f->loop_pc = c->code_length;
                if (kind == 3 && emitting(c)) {
                    /* the condition leaves the stack, the parameters stay */
                    emit(c, YW_OP_IF);
                    emit(c, 0);
                    f->else_patch = c->code_length;
                }
                /* the entry height was taken after the parameters were pushed again: the frame starts below them */
                f->height = c->value_count - np;
            }
            break;
        }
        case 0x05: {
            ctrl *f = &c->ctrls[c->ctrl_count - 1];
            if (f->op != 3) {
                fail(r, YOZORA_WASM_INVALID, "else without if");
                break;
            }
            pop_types(c, f->results, f->nresults);
            if (ok(r) && c->value_count != f->height) {
                fail(r, YOZORA_WASM_INVALID, "the then branch leaves values beyond its results");
                break;
            }
            if (emitting(c)) {
                emit(c, YW_OP_JMP);
                emit(c, f->patches);
                f->patches = c->code_length;
            }
            if (f->else_patch) {
                c->code[f->else_patch - 1] = c->code_length;
                f->else_patch = 0;
            }
            f->op = 4;
            f->unreachable = 0;
            c->value_count = f->height;
            push_types(c, f->params, f->nparams);
            break;
        }
        case 0x0b: {
            ctrl *f = &c->ctrls[c->ctrl_count - 1];
            ctrl done;
            pop_types(c, f->results, f->nresults);
            if (!ok(r))
                break;
            if (c->value_count != f->height) {
                fail(r, YOZORA_WASM_INVALID, "the block leaves values beyond its results");
                break;
            }
            if (f->op == 3) {
                /* an if without else passes its parameters on: they must be its results */
                uint32_t i;
                if (f->nparams != f->nresults) {
                    fail(r, YOZORA_WASM_INVALID, "an if without else changes the stack");
                    break;
                }
                for (i = 0; i < f->nparams; ++i) {
                    if (f->params[i] != f->results[i]) {
                        fail(r, YOZORA_WASM_INVALID, "an if without else changes the stack");
                        break;
                    }
                }
                if (!ok(r))
                    break;
            }
            done = *f;
            --c->ctrl_count;
            if (done.else_patch)
                c->code[done.else_patch - 1] = c->code_length;
            if (done.op != 2)
                patch_list(c, done.patches, c->code_length);
            push_types(c, done.results, done.nresults);
            if (c->ctrl_count == 0) {
                /* the end of the function: the results are on the stack, the return takes them */
                emit(c, YW_OP_RETURN);
            }
            break;
        }
        case 0x0c: {
            uint32_t depth = read_u32(r);
            ctrl *target;
            if (!ok(r))
                break;
            if (depth >= c->ctrl_count) {
                fail(r, YOZORA_WASM_INVALID, "a branch to label %u that does not exist", depth);
                break;
            }
            target = &c->ctrls[c->ctrl_count - 1 - depth];
            pop_types(c, label_types(target), label_arity(target));
            if (emitting(c))
                emit_branch(c, YW_OP_BR, depth);
            set_unreachable(c);
            break;
        }
        case 0x0d: {
            uint32_t depth = read_u32(r);
            ctrl *target;
            if (!ok(r))
                break;
            if (depth >= c->ctrl_count) {
                fail(r, YOZORA_WASM_INVALID, "a branch to label %u that does not exist", depth);
                break;
            }
            pop_value(c, YOZORA_WASM_I32);
            target = &c->ctrls[c->ctrl_count - 1 - depth];
            pop_types(c, label_types(target), label_arity(target));
            push_types(c, label_types(target), label_arity(target));
            if (emitting(c))
                emit_branch(c, YW_OP_BR_IF, depth);
            break;
        }
        case 0x0e: {
            uint32_t count = read_u32(r), i;
            uint32_t arity = 0;
            if (!ok(r))
                break;
            if (count > MAX_FUNCTION_CODE_WORDS / 4) {
                fail(r, YOZORA_WASM_TOO_LARGE, "a branch table is too large");
                break;
            }
            pop_value(c, YOZORA_WASM_I32);
            if (emitting(c)) {
                emit(c, YW_OP_BR_TABLE);
                emit(c, count);
            }
            for (i = 0; i <= count && ok(r); ++i) {
                uint32_t depth = read_u32(r);
                ctrl *target;
                if (!ok(r))
                    break;
                if (depth >= c->ctrl_count) {
                    fail(r, YOZORA_WASM_INVALID, "a branch to label %u that does not exist", depth);
                    break;
                }
                target = &c->ctrls[c->ctrl_count - 1 - depth];
                if (i == 0)
                    arity = label_arity(target);
                else if (label_arity(target) != arity) {
                    fail(r, YOZORA_WASM_INVALID, "the labels of a branch table take different numbers of values");
                    break;
                }
                /* the labels are checked against the stack as if each one were taken, the stack is restored for the next */
                {
                    uint32_t saved = c->value_count;
                    if (arity > YOZORA_WASM_MAX_RESULTS) {
                        fail(r, YOZORA_WASM_TOO_LARGE, "a label takes too many values");
                        break;
                    }
                    pop_types(c, label_types(target), label_arity(target));
                    c->value_count = saved;
                }
                if (emitting(c)) {
                    emit_target(c, target);
                    emit(c, stack_base(c) + target->height);
                    emit(c, label_arity(target));
                }
            }
            set_unreachable(c);
            break;
        }
        case 0x0f:
            pop_types(c, type->results, type->nresults);
            if (emitting(c))
                emit(c, YW_OP_RETURN);
            set_unreachable(c);
            break;
        case 0x10: {
            uint32_t f = read_u32(r);
            const yw_functype *t;
            if (!ok(r))
                break;
            if (f >= m->func_count) {
                fail(r, YOZORA_WASM_INVALID, "a call to function %u that does not exist", f);
                break;
            }
            t = &m->types[m->func_types[f]];
            pop_types(c, t->params, t->nparams);
            push_types(c, t->results, t->nresults);
            if (emitting(c)) {
                emit(c, YW_OP_CALL);
                emit(c, f);
            }
            break;
        }
        case 0x11: {
            uint32_t ti = read_u32(r), table = read_u32(r);
            const yw_functype *t;
            if (!ok(r))
                break;
            if (ti >= m->type_count) {
                fail(r, YOZORA_WASM_INVALID, "call_indirect names type %u that does not exist", ti);
                break;
            }
            if (!check_table(c, table))
                break;
            if (m->tables[table].element_type != YOZORA_WASM_FUNCREF) {
                fail(r, YOZORA_WASM_INVALID, "call_indirect on a table that does not hold functions");
                break;
            }
            t = &m->types[ti];
            pop_value(c, YOZORA_WASM_I32);
            pop_types(c, t->params, t->nparams);
            push_types(c, t->results, t->nresults);
            if (emitting(c)) {
                emit(c, YW_OP_CALL_INDIRECT);
                emit(c, ti);
                emit(c, table);
            }
            break;
        }
        case 0x12:
        case 0x13:
            fail(r, YOZORA_WASM_UNSUPPORTED, "tail calls are not supported");
            break;
        case 0x1a:
            pop_value(c, 0);
            if (emitting(c))
                emit(c, YW_OP_DROP);
            break;
        case 0x1b: {
            uint8_t t1, t2;
            pop_value(c, YOZORA_WASM_I32);
            t1 = pop_value(c, 0);
            t2 = pop_value(c, t1);
            if (ok(r) && ((t1 && !is_numtype(t1)) || (t2 && !is_numtype(t2)))) {
                fail(r, YOZORA_WASM_INVALID, "an untyped select on references");
                break;
            }
            push_value(c, t1 ? t1 : t2);
            if (emitting(c))
                emit(c, YW_OP_SELECT);
            break;
        }
        case 0x1c: {
            uint32_t count = read_u32(r);
            uint8_t t;
            if (!ok(r))
                break;
            if (count != 1) {
                fail(r, YOZORA_WASM_INVALID, "a typed select takes one type");
                break;
            }
            t = read_valtype(r);
            if (!ok(r))
                break;
            pop_value(c, YOZORA_WASM_I32);
            pop_value(c, t);
            pop_value(c, t);
            push_value(c, t);
            if (emitting(c))
                emit(c, YW_OP_SELECT);
            break;
        }
        case 0x20:
        case 0x21:
        case 0x22: {
            uint32_t i = read_u32(r);
            if (!ok(r))
                break;
            if (i >= c->local_count) {
                fail(r, YOZORA_WASM_INVALID, "local %u does not exist", i);
                break;
            }
            if (op == 0x20)
                push_value(c, c->locals[i]);
            else if (op == 0x21)
                pop_value(c, c->locals[i]);
            else {
                pop_value(c, c->locals[i]);
                push_value(c, c->locals[i]);
            }
            if (emitting(c)) {
                emit(c, op);
                emit(c, i);
            }
            break;
        }
        case 0x23:
        case 0x24: {
            uint32_t g = read_u32(r);
            if (!ok(r))
                break;
            if (g >= m->global_count) {
                fail(r, YOZORA_WASM_INVALID, "global %u does not exist", g);
                break;
            }
            if (op == 0x23)
                push_value(c, m->globals[g].type);
            else {
                if (!m->globals[g].is_mutable) {
                    fail(r, YOZORA_WASM_INVALID, "global.set on an immutable global");
                    break;
                }
                pop_value(c, m->globals[g].type);
            }
            if (emitting(c)) {
                emit(c, op);
                emit(c, g);
            }
            break;
        }
        case 0x25:
        case 0x26: {
            uint32_t t = read_u32(r);
            if (!ok(r) || !check_table(c, t))
                break;
            if (op == 0x25) {
                pop_value(c, YOZORA_WASM_I32);
                push_value(c, m->tables[t].element_type);
            } else {
                pop_value(c, m->tables[t].element_type);
                pop_value(c, YOZORA_WASM_I32);
            }
            if (emitting(c)) {
                emit(c, op);
                emit(c, t);
            }
            break;
        }
        case 0x28: memory_access(c, op, 2, YOZORA_WASM_I32, 0); break;
        case 0x29: memory_access(c, op, 3, YOZORA_WASM_I64, 0); break;
        case 0x2a: memory_access(c, op, 2, YOZORA_WASM_F32, 0); break;
        case 0x2b: memory_access(c, op, 3, YOZORA_WASM_F64, 0); break;
        case 0x2c: case 0x2d: memory_access(c, op, 0, YOZORA_WASM_I32, 0); break;
        case 0x2e: case 0x2f: memory_access(c, op, 1, YOZORA_WASM_I32, 0); break;
        case 0x30: case 0x31: memory_access(c, op, 0, YOZORA_WASM_I64, 0); break;
        case 0x32: case 0x33: memory_access(c, op, 1, YOZORA_WASM_I64, 0); break;
        case 0x34: case 0x35: memory_access(c, op, 2, YOZORA_WASM_I64, 0); break;
        case 0x36: memory_access(c, op, 2, YOZORA_WASM_I32, 1); break;
        case 0x37: memory_access(c, op, 3, YOZORA_WASM_I64, 1); break;
        case 0x38: memory_access(c, op, 2, YOZORA_WASM_F32, 1); break;
        case 0x39: memory_access(c, op, 3, YOZORA_WASM_F64, 1); break;
        case 0x3a: memory_access(c, op, 0, YOZORA_WASM_I32, 1); break;
        case 0x3b: memory_access(c, op, 1, YOZORA_WASM_I32, 1); break;
        case 0x3c: memory_access(c, op, 0, YOZORA_WASM_I64, 1); break;
        case 0x3d: memory_access(c, op, 1, YOZORA_WASM_I64, 1); break;
        case 0x3e: memory_access(c, op, 2, YOZORA_WASM_I64, 1); break;
        case 0x3f:
        case 0x40: {
            uint8_t memory = read_byte(r);
            if (!ok(r))
                break;
            if (memory != 0) {
                fail(r, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
                break;
            }
            if (!need_memory(c))
                break;
            if (op == 0x40)
                pop_value(c, YOZORA_WASM_I32);
            push_value(c, YOZORA_WASM_I32);
            if (emitting(c))
                emit(c, op);
            break;
        }
        case 0x41: {
            int32_t v = read_s32(r);
            push_value(c, YOZORA_WASM_I32);
            if (emitting(c)) {
                emit(c, op);
                emit(c, (uint32_t)v);
            }
            break;
        }
        case 0x42: {
            uint64_t v = (uint64_t)read_s64(r);
            push_value(c, YOZORA_WASM_I64);
            if (emitting(c)) {
                emit(c, op);
                emit(c, (uint32_t)v);
                emit(c, (uint32_t)(v >> 32));
            }
            break;
        }
        case 0x43: {
            uint32_t v = read_fixed32(r);
            push_value(c, YOZORA_WASM_F32);
            if (emitting(c)) {
                emit(c, op);
                emit(c, v);
            }
            break;
        }
        case 0x44: {
            uint64_t v = read_fixed64(r);
            push_value(c, YOZORA_WASM_F64);
            if (emitting(c)) {
                emit(c, op);
                emit(c, (uint32_t)v);
                emit(c, (uint32_t)(v >> 32));
            }
            break;
        }
        case 0xd0: {
            uint8_t t = read_reftype(r);
            push_value(c, t);
            if (emitting(c))
                emit(c, op);
            break;
        }
        case 0xd1: {
            uint8_t t = pop_value(c, 0);
            if (ok(r) && t && !is_reftype(t)) {
                fail(r, YOZORA_WASM_INVALID, "ref.is_null on a value that is not a reference");
                break;
            }
            push_value(c, YOZORA_WASM_I32);
            if (emitting(c))
                emit(c, op);
            break;
        }
        case 0xd2: {
            uint32_t f = read_u32(r);
            if (!ok(r))
                break;
            if (f >= m->func_count) {
                fail(r, YOZORA_WASM_INVALID, "ref.func names function %u that does not exist", f);
                break;
            }
            if (!is_declared(c, f)) {
                fail(r, YOZORA_WASM_INVALID, "ref.func of function %u that no export, element or global declares", f);
                break;
            }
            push_value(c, YOZORA_WASM_FUNCREF);
            if (emitting(c)) {
                emit(c, op);
                emit(c, f);
            }
            break;
        }
        default:
            operands = numeric_signature(op, &a, &b, &out);
            if (!operands) {
                fail(r, YOZORA_WASM_MALFORMED, "unknown instruction 0x%02x at offset %lu", op, (unsigned long)(r->p - r->begin - 1));
                break;
            }
            if (operands == 2)
                pop_value(c, b);
            pop_value(c, a);
            push_value(c, out);
            if (emitting(c))
                emit(c, op);
            break;
        }
    }
}

static void free_module_contents(yozora_wasm_module *m)
{
    uint32_t i, j;
    for (i = 0; i < m->type_count; ++i)
        yw_functype_free(&m->types[i]);
    free(m->types);
    free(m->func_types);
    for (i = 0; m->defined && i < m->func_count - m->imported_funcs; ++i)
        free(m->defined[i].code);
    free(m->defined);
    free(m->tables);
    free(m->memories);
    free(m->globals);
    free(m->global_inits);
    for (i = 0; i < m->import_count; ++i) {
        free(m->imports[i].module);
        free(m->imports[i].field);
    }
    free(m->imports);
    for (i = 0; i < m->export_count; ++i)
        free(m->exports[i].name);
    free(m->exports);
    for (i = 0; i < m->elem_count; ++i)
        free(m->elems[i].items);
    free(m->elems);
    free(m->datas);
    for (j = 0; j < m->custom_count; ++j)
        free(m->customs[j].name);
    free(m->customs);
    free(m->image);
}

void yozora_wasm_module_retain(yozora_wasm_module *m)
{
    if (m)
        ++m->refs;
}

void yozora_wasm_module_release(yozora_wasm_module *m)
{
    if (!m || --m->refs)
        return;
    free_module_contents(m);
    free(m);
}

static void read_types(reader *r, yozora_wasm_module *m)
{
    uint32_t n = read_u32(r), i;
    if (!ok(r))
        return;
    if (n > MAX_TYPES) {
        fail(r, YOZORA_WASM_TOO_LARGE, "too many types");
        return;
    }
    m->types = (yw_functype *)grow_array(r, NULL, n, sizeof(yw_functype));
    if (!ok(r))
        return;
    memset(m->types, 0, (size_t)n * sizeof(yw_functype));
    m->type_count = n;
    for (i = 0; i < n && ok(r); ++i) {
        uint8_t form = read_byte(r);
        uint32_t np, nr, k;
        if (!ok(r))
            break;
        if (form != 0x60) {
            fail(r, form == 0x5f || form == 0x5e || form == 0x50 || form == 0x4f ? YOZORA_WASM_UNSUPPORTED : YOZORA_WASM_MALFORMED, "type form 0x%02x", form);
            break;
        }
        np = read_u32(r);
        if (ok(r) && np > YOZORA_WASM_MAX_PARAMS) {
            fail(r, YOZORA_WASM_TOO_LARGE, "a function takes too many parameters");
            break;
        }
        m->types[i].params = (uint8_t *)malloc(np ? np : 1);
        if (!m->types[i].params) {
            fail(r, YOZORA_WASM_NO_MEMORY, "out of memory");
            break;
        }
        m->types[i].nparams = np;
        for (k = 0; k < np && ok(r); ++k)
            m->types[i].params[k] = read_valtype(r);
        nr = read_u32(r);
        if (ok(r) && nr > YOZORA_WASM_MAX_RESULTS) {
            fail(r, YOZORA_WASM_TOO_LARGE, "a function returns too many values");
            break;
        }
        m->types[i].results = (uint8_t *)malloc(nr ? nr : 1);
        if (!m->types[i].results) {
            fail(r, YOZORA_WASM_NO_MEMORY, "out of memory");
            break;
        }
        m->types[i].nresults = nr;
        for (k = 0; k < nr && ok(r); ++k)
            m->types[i].results[k] = read_valtype(r);
    }
}

static void build_import_spaces(reader *r, yozora_wasm_module *m);

static void read_imports(reader *r, yozora_wasm_module *m)
{
    uint32_t n = read_u32(r), i;
    if (!ok(r))
        return;
    if (n > MAX_IMPORTS) {
        fail(r, YOZORA_WASM_TOO_LARGE, "too many imports");
        return;
    }
    m->imports = (yw_import *)grow_array(r, NULL, n, sizeof(yw_import));
    if (!ok(r))
        return;
    memset(m->imports, 0, (size_t)n * sizeof(yw_import));
    m->import_count = n;
    for (i = 0; i < n && ok(r); ++i) {
        yw_import *im = &m->imports[i];
        im->module = read_name(r);
        im->field = read_name(r);
        im->kind = read_byte(r);
        if (!ok(r))
            break;
        switch (im->kind) {
        case YOZORA_WASM_KIND_FUNCTION:
            im->type_index = read_u32(r);
            if (ok(r) && im->type_index >= m->type_count) {
                fail(r, YOZORA_WASM_INVALID, "an import names type %u that does not exist", im->type_index);
                break;
            }
            ++m->imported_funcs;
            break;
        case YOZORA_WASM_KIND_TABLE:
            im->table.element_type = read_reftype(r);
            read_limits(r, YOZORA_WASM_MAX_TABLE_ELEMENTS, &im->table.minimum, &im->table.maximum, &im->table.has_maximum, "table");
            ++m->imported_tables;
            break;
        case YOZORA_WASM_KIND_MEMORY:
            read_limits(r, YOZORA_WASM_MAX_PAGES, &im->memory.minimum, &im->memory.maximum, &im->memory.has_maximum, "memory");
            ++m->imported_memories;
            break;
        case YOZORA_WASM_KIND_GLOBAL: {
            im->global.type = read_valtype(r);
            im->global.is_mutable = read_byte(r);
            if (ok(r) && im->global.is_mutable > 1)
                fail(r, YOZORA_WASM_MALFORMED, "bad mutability");
            ++m->imported_globals;
            break;
        }
        default:
            fail(r, YOZORA_WASM_MALFORMED, "import kind %u", im->kind);
            break;
        }
    }
    if (ok(r))
        build_import_spaces(r, m);
}

/* imports come first in each index space, laid out as soon as the import section is read, definitions add
   after them */
static void build_import_spaces(reader *r, yozora_wasm_module *m)
{
    uint32_t i, fi = 0, ti = 0, mi = 0, gi = 0;
    m->func_count = m->imported_funcs;
    m->func_types = (uint32_t *)grow_array(r, NULL, m->imported_funcs ? m->imported_funcs : 1, sizeof(uint32_t));
    m->tables = (yw_tabletype *)grow_array(r, NULL, m->imported_tables ? m->imported_tables : 1, sizeof(yw_tabletype));
    m->memories = (yw_memtype *)grow_array(r, NULL, m->imported_memories ? m->imported_memories : 1, sizeof(yw_memtype));
    m->globals = (yw_globaltype *)grow_array(r, NULL, m->imported_globals ? m->imported_globals : 1, sizeof(yw_globaltype));
    m->global_inits = (yw_const *)grow_array(r, NULL, m->imported_globals ? m->imported_globals : 1, sizeof(yw_const));
    if (!ok(r))
        return;
    memset(m->global_inits, 0, (size_t)(m->imported_globals ? m->imported_globals : 1) * sizeof(yw_const));
    m->table_count = m->imported_tables;
    m->memory_count = m->imported_memories;
    m->global_count = m->imported_globals;
    for (i = 0; i < m->import_count; ++i) {
        switch (m->imports[i].kind) {
        case YOZORA_WASM_KIND_FUNCTION: m->func_types[fi++] = m->imports[i].type_index; break;
        case YOZORA_WASM_KIND_TABLE: m->tables[ti++] = m->imports[i].table; break;
        case YOZORA_WASM_KIND_MEMORY: m->memories[mi++] = m->imports[i].memory; break;
        default: m->globals[gi++] = m->imports[i].global; break;
        }
    }
    if (m->imported_memories > 1)
        fail(r, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
}

static int exports_unique(const yozora_wasm_module *m)
{
    uint32_t i, j;
    for (i = 0; i < m->export_count; ++i) {
        for (j = i + 1; j < m->export_count; ++j) {
            if (strcmp(m->exports[i].name, m->exports[j].name) == 0)
                return 0;
        }
    }
    return 1;
}

yozora_wasm_status yozora_wasm_module_create(const uint8_t *bytes, size_t size, yozora_wasm_module **out, char *error, size_t error_capacity)
{
    reader r;
    yozora_wasm_module *m = NULL;
    uint32_t defined_count = 0, code_count = 0;
    uint8_t *declared = NULL;
    uint8_t last_id = 0;
    int seen_function_section = 0, seen_code_section = 0;
    uint32_t i;

    if (out)
        *out = NULL;
    if (error && error_capacity)
        error[0] = 0;
    memset(&r, 0, sizeof(r));
    r.status = YOZORA_WASM_OK;
    r.error = error;
    r.error_capacity = error_capacity;
    if (size > YOZORA_WASM_MAX_MODULE_BYTES) {
        fail(&r, YOZORA_WASM_TOO_LARGE, "a module is larger than %u bytes", YOZORA_WASM_MAX_MODULE_BYTES);
        return r.status;
    }
    m = (yozora_wasm_module *)calloc(1, sizeof(*m));
    if (!m) {
        fail(&r, YOZORA_WASM_NO_MEMORY, "out of memory");
        return r.status;
    }
    m->refs = 1;
    m->image = (uint8_t *)malloc(size ? size : 1);
    if (!m->image) {
        fail(&r, YOZORA_WASM_NO_MEMORY, "out of memory");
        free(m);
        return r.status;
    }
    memcpy(m->image, bytes, size);
    m->image_size = (uint32_t)size;
    r.begin = r.p = m->image;
    r.end = m->image + size;

    if (size < 8 || memcmp(r.p, "\0asm", 4) != 0) {
        fail(&r, YOZORA_WASM_MALFORMED, "bad magic number");
        goto done;
    }
    if (r.p[4] != 1 || r.p[5] != 0 || r.p[6] != 0 || r.p[7] != 0) {
        fail(&r, YOZORA_WASM_MALFORMED, "unsupported version");
        goto done;
    }
    r.p += 8;

    while (ok(&r) && r.p < r.end) {
        uint8_t id = read_byte(&r);
        uint32_t section_size = read_u32(&r);
        reader s;
        if (!ok(&r))
            break;
        if (section_size > (uint32_t)(r.end - r.p)) {
            fail(&r, YOZORA_WASM_MALFORMED, "section %u is longer than the module", id);
            break;
        }
        s = r;
        s.begin = r.begin;
        s.end = r.p + section_size;
        r.p += section_size;
        /* sections come in order, the data count between element and code, custom ones anywhere */
        if (id != 0) {
            static const uint8_t rank_of[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 10 };
            if (id > 12) {
                fail(&r, YOZORA_WASM_MALFORMED, "unknown section %u", id);
                break;
            }
            if (last_id && rank_of[id] <= rank_of[last_id]) {
                fail(&r, YOZORA_WASM_MALFORMED, "section %u out of order", id);
                break;
            }
            last_id = id;
        }
        switch (id) {
        case 0: {
            char *name = read_name(&s);
            yw_custom *grown;
            if (!ok(&s)) {
                r.status = s.status;
                free(name);
                break;
            }
            grown = (yw_custom *)grow_array(&r, m->customs, (size_t)m->custom_count + 1, sizeof(yw_custom));
            if (!ok(&r)) {
                free(name);
                break;
            }
            m->customs = grown;
            m->customs[m->custom_count].name = name;
            m->customs[m->custom_count].bytes = s.p;
            m->customs[m->custom_count].size = (uint32_t)(s.end - s.p);
            ++m->custom_count;
            continue;
        }
        case 1:
            read_types(&s, m);
            break;
        case 2:
            read_imports(&s, m);
            break;
        case 3: {
            uint32_t n = read_u32(&s);
            seen_function_section = 1;
            if (!ok(&s))
                break;
            if (n > MAX_FUNCTIONS) {
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many functions");
                break;
            }
            m->func_types = (uint32_t *)grow_array(&s, m->func_types, (size_t)m->imported_funcs + n ? (size_t)m->imported_funcs + n : 1, sizeof(uint32_t));
            if (!ok(&s))
                break;
            defined_count = n;
            for (i = 0; i < n && ok(&s); ++i) {
                uint32_t type_index = read_u32(&s);
                if (ok(&s) && type_index >= m->type_count)
                    fail(&s, YOZORA_WASM_INVALID, "function %u has type %u that does not exist", i, type_index);
                m->func_types[m->imported_funcs + i] = type_index;
            }
            if (ok(&s))
                m->func_count = m->imported_funcs + n;
            break;
        }
        case 4: {
            uint32_t n = read_u32(&s);
            if (!ok(&s))
                break;
            if (n + m->imported_tables > MAX_TABLES) {
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many tables");
                break;
            }
            m->tables = (yw_tabletype *)grow_array(&s, m->tables, (size_t)m->imported_tables + n ? (size_t)m->imported_tables + n : 1, sizeof(yw_tabletype));
            if (!ok(&s))
                break;
            m->table_count = m->imported_tables + n;
            for (i = 0; i < n && ok(&s); ++i) {
                yw_tabletype *t = &m->tables[m->imported_tables + i];
                t->element_type = read_reftype(&s);
                read_limits(&s, YOZORA_WASM_MAX_TABLE_ELEMENTS, &t->minimum, &t->maximum, &t->has_maximum, "table");
            }
            break;
        }
        case 5: {
            uint32_t n = read_u32(&s);
            if (!ok(&s))
                break;
            if (n + m->imported_memories > 1) {
                fail(&s, n + m->imported_memories > 1 ? YOZORA_WASM_UNSUPPORTED : YOZORA_WASM_INVALID, "several memories are not supported");
                break;
            }
            m->memories = (yw_memtype *)grow_array(&s, m->memories, (size_t)m->imported_memories + n ? (size_t)m->imported_memories + n : 1, sizeof(yw_memtype));
            if (!ok(&s))
                break;
            m->memory_count = m->imported_memories + n;
            for (i = 0; i < n && ok(&s); ++i) {
                yw_memtype *t = &m->memories[m->imported_memories + i];
                read_limits(&s, YOZORA_WASM_MAX_PAGES, &t->minimum, &t->maximum, &t->has_maximum, "memory");
            }
            break;
        }
        case 6: {
            uint32_t n = read_u32(&s);
            if (!ok(&s))
                break;
            if (n > MAX_GLOBALS) {
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many globals");
                break;
            }
            m->globals = (yw_globaltype *)grow_array(&s, m->globals, (size_t)m->imported_globals + n ? (size_t)m->imported_globals + n : 1, sizeof(yw_globaltype));
            m->global_inits = (yw_const *)grow_array(&s, m->global_inits, (size_t)m->imported_globals + n ? (size_t)m->imported_globals + n : 1, sizeof(yw_const));
            if (!ok(&s))
                break;
            m->global_count = m->imported_globals + n;
            for (i = 0; i < n && ok(&s); ++i) {
                uint32_t gi = m->imported_globals + i;
                m->globals[gi].type = read_valtype(&s);
                m->globals[gi].is_mutable = read_byte(&s);
                if (ok(&s) && m->globals[gi].is_mutable > 1)
                    fail(&s, YOZORA_WASM_MALFORMED, "bad mutability");
                if (ok(&s))
                    m->global_inits[gi] = read_const(&s, m, m->globals[gi].type, m->imported_globals);
            }
            break;
        }
        case 7: {
            uint32_t n = read_u32(&s);
            if (!ok(&s))
                break;
            if (n > MAX_FUNCTIONS) {
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many exports");
                break;
            }
            m->exports = (yw_export *)grow_array(&s, NULL, n ? n : 1, sizeof(yw_export));
            if (!ok(&s))
                break;
            memset(m->exports, 0, (size_t)n * sizeof(yw_export));
            m->export_count = n;
            for (i = 0; i < n && ok(&s); ++i) {
                yw_export *e = &m->exports[i];
                uint32_t limit = 0;
                e->name = read_name(&s);
                e->kind = read_byte(&s);
                e->index = read_u32(&s);
                if (!ok(&s))
                    break;
                switch (e->kind) {
                case YOZORA_WASM_KIND_FUNCTION: limit = m->func_count; break;
                case YOZORA_WASM_KIND_TABLE: limit = m->table_count; break;
                case YOZORA_WASM_KIND_MEMORY: limit = m->memory_count; break;
                case YOZORA_WASM_KIND_GLOBAL: limit = m->global_count; break;
                default: fail(&s, YOZORA_WASM_MALFORMED, "export kind %u", e->kind); break;
                }
                if (ok(&s) && e->index >= limit)
                    fail(&s, YOZORA_WASM_INVALID, "export %u names an index that does not exist", i);
            }
            if (ok(&s) && !exports_unique(m))
                fail(&s, YOZORA_WASM_INVALID, "two exports have the same name");
            break;
        }
        case 8:
            m->start = read_u32(&s);
            m->has_start = 1;
            if (ok(&s) && m->start >= m->func_count)
                fail(&s, YOZORA_WASM_INVALID, "the start function does not exist");
            else if (ok(&s)) {
                const yw_functype *t = &m->types[m->func_types[m->start]];
                if (t->nparams || t->nresults)
                    fail(&s, YOZORA_WASM_INVALID, "the start function takes or returns values");
            }
            break;
        case 9: {
            uint32_t n = read_u32(&s);
            if (!ok(&s))
                break;
            if (n > MAX_SEGMENTS) {
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many element segments");
                break;
            }
            m->elems = (yw_elemseg *)grow_array(&s, NULL, n ? n : 1, sizeof(yw_elemseg));
            if (!ok(&s))
                break;
            memset(m->elems, 0, (size_t)n * sizeof(yw_elemseg));
            m->elem_count = n;
            for (i = 0; i < n && ok(&s); ++i) {
                yw_elemseg *e = &m->elems[i];
                uint32_t flags = read_u32(&s), k;
                int uses_exprs, explicit_table;
                if (!ok(&s))
                    break;
                if (flags > 7) {
                    fail(&s, YOZORA_WASM_MALFORMED, "element segment flags %u", flags);
                    break;
                }
                e->element_type = YOZORA_WASM_FUNCREF;
                e->mode = (flags & 1) ? ((flags & 2) ? YW_SEGMENT_DECLARATIVE : YW_SEGMENT_PASSIVE) : YW_SEGMENT_ACTIVE;
                explicit_table = (flags & 3) == 2;
                uses_exprs = (flags & 4) != 0;
                if (e->mode == YW_SEGMENT_ACTIVE) {
                    if (explicit_table)
                        e->table = read_u32(&s);
                    if (ok(&s) && e->table >= m->table_count) {
                        fail(&s, YOZORA_WASM_INVALID, "an element segment fills table %u that does not exist", e->table);
                        break;
                    }
                    e->offset = read_const(&s, m, YOZORA_WASM_I32, m->imported_globals);
                }
                if (e->mode != YW_SEGMENT_ACTIVE || explicit_table) {
                    uint8_t kind = read_byte(&s);
                    if (ok(&s)) {
                        if (uses_exprs) {
                            if (!is_reftype(kind))
                                fail(&s, YOZORA_WASM_MALFORMED, "bad reference type");
                            else
                                e->element_type = kind;
                        } else if (kind != 0)
                            fail(&s, YOZORA_WASM_MALFORMED, "bad element kind");
                    }
                }
                if (ok(&s) && e->mode == YW_SEGMENT_ACTIVE && m->tables[e->table].element_type != e->element_type)
                    fail(&s, YOZORA_WASM_INVALID, "an element segment of one type fills a table of another");
                e->count = read_u32(&s);
                if (ok(&s) && e->count > (uint32_t)(s.end - s.p)) {
                    fail(&s, YOZORA_WASM_MALFORMED, "an element segment is longer than its section");
                    break;
                }
                if (ok(&s) && e->count > YOZORA_WASM_MAX_TABLE_ELEMENTS) {
                    fail(&s, YOZORA_WASM_TOO_LARGE, "an element segment is too large");
                    break;
                }
                e->items = (yw_const *)grow_array(&s, NULL, e->count ? e->count : 1, sizeof(yw_const));
                if (!ok(&s))
                    break;
                for (k = 0; k < e->count && ok(&s); ++k) {
                    if (uses_exprs)
                        e->items[k] = read_const(&s, m, e->element_type, m->imported_globals);
                    else {
                        uint32_t f = read_u32(&s);
                        memset(&e->items[k], 0, sizeof(yw_const));
                        e->items[k].op = YW_CONST_REF_FUNC;
                        e->items[k].type = YOZORA_WASM_FUNCREF;
                        e->items[k].bits = f;
                        if (ok(&s) && f >= m->func_count)
                            fail(&s, YOZORA_WASM_INVALID, "an element names function %u that does not exist", f);
                    }
                }
            }
            break;
        }
        case 12:
            m->data_count = read_u32(&s);
            m->has_data_count = 1;
            if (ok(&s) && m->data_count > MAX_SEGMENTS)
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many data segments");
            break;
        case 10: {
            uint32_t n = read_u32(&s);
            seen_code_section = 1;
            if (!ok(&s))
                break;
            if (n != defined_count) {
                fail(&s, YOZORA_WASM_MALFORMED, "%u function bodies for %u functions", n, defined_count);
                break;
            }
            code_count = n;
            /* the functions an export, an element segment or a global declares may be named by ref.func */
            declared = (uint8_t *)calloc(((size_t)m->func_count >> 3) + 1, 1);
            if (!declared) {
                fail(&s, YOZORA_WASM_NO_MEMORY, "out of memory");
                break;
            }
            for (i = 0; i < m->export_count; ++i) {
                if (m->exports[i].kind == YOZORA_WASM_KIND_FUNCTION)
                    declared[m->exports[i].index >> 3] |= (uint8_t)(1u << (m->exports[i].index & 7));
            }
            for (i = 0; i < m->elem_count; ++i) {
                uint32_t k;
                for (k = 0; k < m->elems[i].count; ++k) {
                    if (m->elems[i].items[k].op == YW_CONST_REF_FUNC)
                        declared[m->elems[i].items[k].bits >> 3] |= (uint8_t)(1u << (m->elems[i].items[k].bits & 7));
                }
            }
            for (i = m->imported_globals; i < m->global_count; ++i) {
                if (m->global_inits[i].op == YW_CONST_REF_FUNC)
                    declared[m->global_inits[i].bits >> 3] |= (uint8_t)(1u << (m->global_inits[i].bits & 7));
            }
            m->defined = (yw_funcdef *)calloc(n ? n : 1, sizeof(yw_funcdef));
            if (!m->defined) {
                fail(&s, YOZORA_WASM_NO_MEMORY, "out of memory");
                break;
            }
            for (i = 0; i < n && ok(&s); ++i) {
                uint32_t body_size = read_u32(&s);
                reader b;
                compiler c;
                uint32_t groups, total, k;
                const yw_functype *type = &m->types[m->func_types[m->imported_funcs + i]];
                if (!ok(&s))
                    break;
                if (body_size > (uint32_t)(s.end - s.p)) {
                    fail(&s, YOZORA_WASM_MALFORMED, "a function body is longer than the section");
                    break;
                }
                b = s;
                b.end = s.p + body_size;
                s.p += body_size;
                memset(&c, 0, sizeof(c));
                c.r = &b;
                c.m = m;
                c.declared = declared;
                c.type = type;
                groups = read_u32(&b);
                total = type->nparams;
                {
                    /* the locals are the parameters and then the declared ones */
                    const uint8_t *scan = b.p;
                    uint32_t g;
                    reader probe = b;
                    uint64_t sum = type->nparams;
                    for (g = 0; g < groups && ok(&probe); ++g) {
                        uint32_t count = read_u32(&probe);
                        sum += count;
                        read_valtype(&probe);
                        if (sum > MAX_LOCALS)
                            fail(&probe, YOZORA_WASM_TOO_LARGE, "a function has too many locals");
                    }
                    (void)scan;
                    if (!ok(&probe)) {
                        s.status = probe.status;
                        break;
                    }
                    total = (uint32_t)sum;
                }
                c.locals = (uint8_t *)malloc(total ? total : 1);
                if (!c.locals) {
                    fail(&s, YOZORA_WASM_NO_MEMORY, "out of memory");
                    break;
                }
                c.local_count = total;
                for (k = 0; k < type->nparams; ++k)
                    c.locals[k] = type->params[k];
                {
                    uint32_t g, at = type->nparams;
                    for (g = 0; g < groups; ++g) {
                        uint32_t count = read_u32(&b);
                        uint8_t t = read_valtype(&b);
                        while (count--)
                            c.locals[at++] = t;
                    }
                }
                if (ok(&b))
                    compile_body(&c, i);
                if (ok(&b) && b.p != b.end)
                    fail(&b, YOZORA_WASM_MALFORMED, "a function body has bytes after its end");
                if (ok(&b) && c.ctrl_count != 0)
                    fail(&b, YOZORA_WASM_MALFORMED, "a function body ends inside a block");
                if (!ok(&b)) {
                    s.status = b.status;
                    free(c.locals);
                    free(c.values);
                    free(c.ctrls);
                    free(c.code);
                    break;
                }
                m->defined[i].code = c.code;
                m->defined[i].code_length = c.code_length;
                m->defined[i].locals = total;
                m->defined[i].nparams = type->nparams;
                m->defined[i].nresults = type->nresults;
                m->defined[i].max_stack = total + c.max_height + 4;
                free(c.locals);
                free(c.values);
                free(c.ctrls);
            }
            if (ok(&s) && s.p != s.end)
                fail(&s, YOZORA_WASM_MALFORMED, "the code section has bytes after the last body");
            break;
        }
        case 11: {
            uint32_t n = read_u32(&s);
            if (!ok(&s))
                break;
            if (n > MAX_SEGMENTS) {
                fail(&s, YOZORA_WASM_TOO_LARGE, "too many data segments");
                break;
            }
            if (m->has_data_count && n != m->data_count) {
                fail(&s, YOZORA_WASM_MALFORMED, "the data count section says %u segments, the data section has %u", m->data_count, n);
                break;
            }
            m->datas = (yw_dataseg *)grow_array(&s, NULL, n ? n : 1, sizeof(yw_dataseg));
            if (!ok(&s))
                break;
            memset(m->datas, 0, (size_t)n * sizeof(yw_dataseg));
            m->data_count = n;
            for (i = 0; i < n && ok(&s); ++i) {
                yw_dataseg *d = &m->datas[i];
                uint32_t flags = read_u32(&s), len;
                if (!ok(&s))
                    break;
                if (flags > 2) {
                    fail(&s, YOZORA_WASM_MALFORMED, "data segment flags %u", flags);
                    break;
                }
                d->mode = (flags == 1) ? YW_SEGMENT_PASSIVE : YW_SEGMENT_ACTIVE;
                if (flags == 2) {
                    uint32_t memory = read_u32(&s);
                    if (ok(&s) && memory != 0) {
                        fail(&s, YOZORA_WASM_UNSUPPORTED, "several memories are not supported");
                        break;
                    }
                }
                if (d->mode == YW_SEGMENT_ACTIVE) {
                    if (m->memory_count == 0) {
                        fail(&s, YOZORA_WASM_INVALID, "a data segment in a module without memory");
                        break;
                    }
                    d->offset = read_const(&s, m, YOZORA_WASM_I32, m->imported_globals);
                }
                len = read_u32(&s);
                if (ok(&s) && len > (uint32_t)(s.end - s.p)) {
                    fail(&s, YOZORA_WASM_MALFORMED, "a data segment is longer than its section");
                    break;
                }
                d->bytes = s.p;
                d->size = len;
                s.p += len;
            }
            break;
        }
        default:
            fail(&r, YOZORA_WASM_MALFORMED, "unknown section %u", id);
            break;
        }
        if (ok(&s) && id != 0 && s.p != s.end && id != 10)
            fail(&s, YOZORA_WASM_MALFORMED, "section %u has %lu bytes after its contents", id, (unsigned long)(s.end - s.p));
        if (!ok(&s) && ok(&r)) {
            r.status = s.status;
        }
    }
    if (ok(&r)) {
        if (seen_function_section != 0 && !seen_code_section && defined_count != 0)
            fail(&r, YOZORA_WASM_MALFORMED, "functions without a code section");
        else if (!seen_function_section && seen_code_section && code_count != 0)
            fail(&r, YOZORA_WASM_MALFORMED, "a code section without functions");
        else if (m->has_data_count && m->datas == NULL && m->data_count != 0)
            fail(&r, YOZORA_WASM_MALFORMED, "the data count section promises segments that are not there");
    }
done:
    free(declared);
    if (!ok(&r)) {
        yozora_wasm_module_release(m);
        return r.status;
    }
    if (out)
        *out = m;
    else
        yozora_wasm_module_release(m);
    return YOZORA_WASM_OK;
}

yozora_wasm_status yozora_wasm_validate(const uint8_t *bytes, size_t size, char *error, size_t error_capacity)
{
    return yozora_wasm_module_create(bytes, size, NULL, error, error_capacity);
}

uint32_t yozora_wasm_module_import_count(const yozora_wasm_module *m) { return m->import_count; }
uint32_t yozora_wasm_module_export_count(const yozora_wasm_module *m) { return m->export_count; }

static void describe(const yozora_wasm_module *m, uint8_t kind, uint32_t index, const yw_import *im, yozora_wasm_externtype *type)
{
    memset(type, 0, sizeof(*type));
    type->kind = kind;
    switch (kind) {
    case YOZORA_WASM_KIND_FUNCTION: {
        uint32_t ti = im ? im->type_index : m->func_types[index];
        type->function.nparams = m->types[ti].nparams;
        type->function.nresults = m->types[ti].nresults;
        type->function.params = m->types[ti].params;
        type->function.results = m->types[ti].results;
        break;
    }
    case YOZORA_WASM_KIND_TABLE: {
        const yw_tabletype *t = im ? &im->table : &m->tables[index];
        type->value_type = t->element_type;
        type->minimum = t->minimum;
        type->maximum = t->maximum;
        type->has_maximum = t->has_maximum;
        break;
    }
    case YOZORA_WASM_KIND_MEMORY: {
        const yw_memtype *t = im ? &im->memory : &m->memories[index];
        type->minimum = t->minimum;
        type->maximum = t->maximum;
        type->has_maximum = t->has_maximum;
        break;
    }
    default: {
        const yw_globaltype *t = im ? &im->global : &m->globals[index];
        type->value_type = t->type;
        type->mutable_global = t->is_mutable;
        break;
    }
    }
}

int yozora_wasm_module_import(const yozora_wasm_module *m, uint32_t index, const char **module_name, const char **field_name, yozora_wasm_externtype *type)
{
    if (index >= m->import_count)
        return 0;
    *module_name = m->imports[index].module;
    *field_name = m->imports[index].field;
    describe(m, m->imports[index].kind, 0, &m->imports[index], type);
    return 1;
}

int yozora_wasm_module_export(const yozora_wasm_module *m, uint32_t index, const char **name, yozora_wasm_externtype *type)
{
    if (index >= m->export_count)
        return 0;
    *name = m->exports[index].name;
    describe(m, m->exports[index].kind, m->exports[index].index, NULL, type);
    return 1;
}

int yozora_wasm_module_custom_section(const yozora_wasm_module *m, const char *name, uint32_t index, const uint8_t **bytes, size_t *size)
{
    uint32_t i, seen = 0;
    for (i = 0; i < m->custom_count; ++i) {
        if (strcmp(m->customs[i].name, name) == 0) {
            if (seen++ == index) {
                *bytes = m->customs[i].bytes;
                *size = m->customs[i].size;
                return 1;
            }
        }
    }
    return 0;
}
