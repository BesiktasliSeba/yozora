/* instance objects, instantiation and the interpreter. a function runs on the value stack of its context:
   parameters are the first slots of its frame, then locals, then operands, so a call copies no arguments and
   a return moves only the results */

#include "yozora_wasm_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef yozora_wasm_slot slot;

#define DEFAULT_MAX_DEPTH 20000u
#define DEFAULT_STACK_SLOTS (512u * 1024u)

struct yozora_wasm_memory {
    uint32_t refs;
    uint8_t *data;
    uint32_t pages;
    uint32_t maximum;
    uint8_t has_maximum;
    uint32_t generation;
    yozora_wasm_memory_observer observer;
    void (*observer_destroy)(void *user);
    void *observer_user;
};

struct yozora_wasm_table {
    uint32_t refs;
    uint8_t type;
    slot *elements;
    uint32_t size;
    uint32_t maximum;
    uint8_t has_maximum;
};

struct yozora_wasm_global {
    uint32_t refs;
    uint8_t type;
    uint8_t is_mutable;
    slot value;
};

struct yozora_wasm_func {
    uint32_t refs;
    uint8_t is_host;
    yozora_wasm_instance *instance;
    uint32_t index;
    const yw_funcdef *definition;
    yw_functype owned_type;
    const yw_functype *type;
    yozora_wasm_host_function host;
    void *user;
    void (*finalize)(void *user);
};

struct yozora_wasm_instance {
    uint32_t refs;
    yozora_wasm_module *module;
    yozora_wasm_func **funcs;
    yozora_wasm_func *own;
    yozora_wasm_table **tables;
    yozora_wasm_memory **memories;
    yozora_wasm_global **globals;
    uint8_t *dropped_data;
    uint8_t *dropped_elements;
};

typedef struct {
    const uint32_t *pc;
    slot *fp;
    const yw_funcdef *definition;
    yozora_wasm_instance *instance;
} frame;

struct yozora_wasm_context {
    slot *stack;
    slot *sp;
    uint32_t stack_slots;
    frame *frames;
    uint32_t depth;
    uint32_t max_depth;
    int allocated;
    const char *trap;
    yozora_wasm_interrupt interrupt;
    void *interrupt_user;
};

static const char *const trap_unreachable = "unreachable";
static const char *const trap_memory = "memory access out of bounds";
static const char *const trap_divide = "divide by zero";
static const char *const trap_remainder = "remainder by zero";
static const char *const trap_overflow = "divide result unrepresentable";
static const char *const trap_conversion = "float unrepresentable in integer range";
static const char *const trap_table = "table index is out of bounds";
static const char *const trap_null = "null function or function signature mismatch";
static const char *const trap_stack = "Maximum call stack size exceeded";
static const char *const trap_segment = "data segment or element segment out of bounds";
static const char *const trap_signature = "null function or function signature mismatch";

yozora_wasm_memory *yozora_wasm_memory_create(uint32_t minimum, uint32_t maximum, int has_maximum)
{
    yozora_wasm_memory *m;
    if (minimum > YOZORA_WASM_MAX_PAGES || (has_maximum && maximum < minimum))
        return NULL;
    m = (yozora_wasm_memory *)calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->refs = 1;
    m->pages = minimum;
    m->maximum = maximum;
    m->has_maximum = has_maximum ? 1 : 0;
    if (minimum) {
        m->data = (uint8_t *)calloc(minimum, YOZORA_WASM_PAGE_SIZE);
        if (!m->data) {
            free(m);
            return NULL;
        }
    }
    return m;
}

/* the count of a memory is atomic, a JavaScript buffer over it lets go of its reference when the collector
   frees the buffer, on whatever thread that is */
void yozora_wasm_memory_retain(yozora_wasm_memory *m)
{
    if (m)
        __atomic_add_fetch(&m->refs, 1, __ATOMIC_RELAXED);
}

void yozora_wasm_memory_release(yozora_wasm_memory *m)
{
    if (!m || __atomic_sub_fetch(&m->refs, 1, __ATOMIC_ACQ_REL))
        return;
    if (m->observer_destroy)
        m->observer_destroy(m->observer_user);
    free(m->data);
    free(m);
}

void yozora_wasm_memory_set_observer(yozora_wasm_memory *m, yozora_wasm_memory_observer before_grow, void (*destroy)(void *user), void *user)
{
    if (!m)
        return;
    m->observer = before_grow;
    m->observer_destroy = destroy;
    m->observer_user = user;
}

void *yozora_wasm_memory_observer_user(const yozora_wasm_memory *m) { return m ? m->observer_user : NULL; }

uint8_t *yozora_wasm_memory_data(yozora_wasm_memory *m) { return m->data; }
size_t yozora_wasm_memory_size(const yozora_wasm_memory *m) { return (size_t)m->pages * YOZORA_WASM_PAGE_SIZE; }
uint32_t yozora_wasm_memory_pages(const yozora_wasm_memory *m) { return m->pages; }
uint32_t yozora_wasm_memory_generation(const yozora_wasm_memory *m) { return m->generation; }

int64_t yozora_wasm_memory_grow(yozora_wasm_memory *m, uint32_t delta)
{
    uint64_t wanted = (uint64_t)m->pages + delta;
    uint32_t old = m->pages;
    uint8_t *grown;
    uint64_t limit = m->has_maximum ? m->maximum : YOZORA_WASM_MAX_PAGES;
    if (limit > YOZORA_WASM_MAX_PAGES)
        limit = YOZORA_WASM_MAX_PAGES;
    /* what the system cannot give fails below and is the -1 of the specification, but the byte count of a 32 bit size_t must not wrap first */
    if (wanted > limit || wanted > SIZE_MAX / YOZORA_WASM_PAGE_SIZE)
        return -1;
    /* the observer drops views of the bytes before realloc may move them. a growth by nothing drops them too,
       the JavaScript API gives every growth a new buffer */
    if (m->observer)
        m->observer(m->observer_user);
    if (delta == 0)
        return old;
    grown = (uint8_t *)realloc(m->data, (size_t)wanted * YOZORA_WASM_PAGE_SIZE);
    if (!grown)
        return -1;
    memset(grown + (size_t)old * YOZORA_WASM_PAGE_SIZE, 0, (size_t)delta * YOZORA_WASM_PAGE_SIZE);
    m->data = grown;
    m->pages = (uint32_t)wanted;
    ++m->generation;
    return old;
}

yozora_wasm_func *yozora_wasm_func_create_host(uint32_t nparams, const uint8_t *params, uint32_t nresults, const uint8_t *results, yozora_wasm_host_function function,
    void *user, void (*finalize)(void *user))
{
    yozora_wasm_func *f;
    if (nparams > YOZORA_WASM_MAX_PARAMS || nresults > YOZORA_WASM_MAX_RESULTS || !function)
        return NULL;
    f = (yozora_wasm_func *)calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->refs = 1;
    f->is_host = 1;
    f->owned_type.nparams = nparams;
    f->owned_type.nresults = nresults;
    f->owned_type.params = (uint8_t *)malloc(nparams ? nparams : 1);
    f->owned_type.results = (uint8_t *)malloc(nresults ? nresults : 1);
    if (!f->owned_type.params || !f->owned_type.results) {
        yw_functype_free(&f->owned_type);
        free(f);
        return NULL;
    }
    if (nparams)
        memcpy(f->owned_type.params, params, nparams);
    if (nresults)
        memcpy(f->owned_type.results, results, nresults);
    f->type = &f->owned_type;
    f->host = function;
    f->user = user;
    f->finalize = finalize;
    return f;
}

void yozora_wasm_func_retain(yozora_wasm_func *f)
{
    if (!f)
        return;
    if (f->is_host)
        ++f->refs;
    else
        yozora_wasm_instance_retain(f->instance);
}

void yozora_wasm_func_release(yozora_wasm_func *f)
{
    if (!f)
        return;
    if (!f->is_host) {
        yozora_wasm_instance_release(f->instance);
        return;
    }
    if (--f->refs)
        return;
    if (f->finalize)
        f->finalize(f->user);
    yw_functype_free(&f->owned_type);
    free(f);
}

yozora_wasm_functype yozora_wasm_func_type(const yozora_wasm_func *f)
{
    yozora_wasm_functype t;
    t.nparams = f->type->nparams;
    t.nresults = f->type->nresults;
    t.params = f->type->params;
    t.results = f->type->results;
    return t;
}

yozora_wasm_instance *yozora_wasm_func_instance(const yozora_wasm_func *f) { return f->is_host ? NULL : f->instance; }
uint32_t yozora_wasm_func_index(const yozora_wasm_func *f) { return f->index; }
void *yozora_wasm_func_user(const yozora_wasm_func *f) { return f->is_host ? f->user : NULL; }

static int same_type(const yw_functype *a, const yw_functype *b)
{
    return a->nparams == b->nparams && a->nresults == b->nresults && (!a->nparams || !memcmp(a->params, b->params, a->nparams))
        && (!a->nresults || !memcmp(a->results, b->results, a->nresults));
}

static void ref_retain(uint8_t type, slot v)
{
    if (type == YOZORA_WASM_FUNCREF && v)
        yozora_wasm_func_retain((yozora_wasm_func *)(uintptr_t)v);
}

static void ref_release(uint8_t type, slot v)
{
    if (type == YOZORA_WASM_FUNCREF && v)
        yozora_wasm_func_release((yozora_wasm_func *)(uintptr_t)v);
}

yozora_wasm_table *yozora_wasm_table_create(uint8_t element_type, uint32_t minimum, uint32_t maximum, int has_maximum, slot initial)
{
    yozora_wasm_table *t;
    uint32_t i;
    if (minimum > YOZORA_WASM_MAX_TABLE_ELEMENTS || (has_maximum && maximum < minimum) || (element_type != YOZORA_WASM_FUNCREF && element_type != YOZORA_WASM_EXTERNREF))
        return NULL;
    t = (yozora_wasm_table *)calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->refs = 1;
    t->type = element_type;
    t->size = minimum;
    t->maximum = maximum;
    t->has_maximum = has_maximum ? 1 : 0;
    if (minimum) {
        t->elements = (slot *)calloc(minimum, sizeof(slot));
        if (!t->elements) {
            free(t);
            return NULL;
        }
        for (i = 0; i < minimum; ++i) {
            t->elements[i] = initial;
            ref_retain(element_type, initial);
        }
    }
    return t;
}

void yozora_wasm_table_retain(yozora_wasm_table *t)
{
    if (t)
        ++t->refs;
}

void yozora_wasm_table_release(yozora_wasm_table *t)
{
    uint32_t i;
    if (!t || --t->refs)
        return;
    for (i = 0; i < t->size; ++i)
        ref_release(t->type, t->elements[i]);
    free(t->elements);
    free(t);
}

uint32_t yozora_wasm_table_size(const yozora_wasm_table *t) { return t->size; }
uint8_t yozora_wasm_table_type(const yozora_wasm_table *t) { return t->type; }

int yozora_wasm_table_get(const yozora_wasm_table *t, uint32_t index, slot *out)
{
    if (index >= t->size)
        return 0;
    *out = t->elements[index];
    return 1;
}

static void table_store(yozora_wasm_table *t, uint32_t index, slot value)
{
    ref_retain(t->type, value);
    ref_release(t->type, t->elements[index]);
    t->elements[index] = value;
}

int yozora_wasm_table_set(yozora_wasm_table *t, uint32_t index, slot value)
{
    if (index >= t->size)
        return 0;
    table_store(t, index, value);
    return 1;
}

int64_t yozora_wasm_table_grow(yozora_wasm_table *t, uint32_t delta, slot initial)
{
    uint64_t wanted = (uint64_t)t->size + delta;
    uint32_t old = t->size, i;
    slot *grown;
    uint64_t limit = t->has_maximum ? t->maximum : YOZORA_WASM_MAX_TABLE_ELEMENTS;
    if (limit > YOZORA_WASM_MAX_TABLE_ELEMENTS)
        limit = YOZORA_WASM_MAX_TABLE_ELEMENTS;
    if (wanted > limit)
        return -1;
    if (delta == 0)
        return old;
    grown = (slot *)realloc(t->elements, (size_t)wanted * sizeof(slot));
    if (!grown)
        return -1;
    t->elements = grown;
    for (i = old; i < wanted; ++i) {
        grown[i] = initial;
        ref_retain(t->type, initial);
    }
    t->size = (uint32_t)wanted;
    return old;
}

yozora_wasm_global *yozora_wasm_global_create(uint8_t type, int is_mutable, slot value)
{
    yozora_wasm_global *g = (yozora_wasm_global *)calloc(1, sizeof(*g));
    if (!g)
        return NULL;
    g->refs = 1;
    g->type = type;
    g->is_mutable = is_mutable ? 1 : 0;
    g->value = value;
    ref_retain(type, value);
    return g;
}

void yozora_wasm_global_retain(yozora_wasm_global *g)
{
    if (g)
        ++g->refs;
}

void yozora_wasm_global_release(yozora_wasm_global *g)
{
    if (!g || --g->refs)
        return;
    ref_release(g->type, g->value);
    free(g);
}

slot yozora_wasm_global_get(const yozora_wasm_global *g) { return g->value; }
uint8_t yozora_wasm_global_type(const yozora_wasm_global *g) { return g->type; }
int yozora_wasm_global_mutable(const yozora_wasm_global *g) { return g->is_mutable; }

static void global_store(yozora_wasm_global *g, slot value)
{
    ref_retain(g->type, value);
    ref_release(g->type, g->value);
    g->value = value;
}

int yozora_wasm_global_set(yozora_wasm_global *g, slot value)
{
    if (!g->is_mutable)
        return 0;
    global_store(g, value);
    return 1;
}

yozora_wasm_context *yozora_wasm_context_create(void)
{
    yozora_wasm_context *c = (yozora_wasm_context *)calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->max_depth = DEFAULT_MAX_DEPTH;
    c->stack_slots = DEFAULT_STACK_SLOTS;
    return c;
}

void yozora_wasm_context_destroy(yozora_wasm_context *c)
{
    if (!c)
        return;
    free(c->stack);
    free(c->frames);
    free(c);
}

void yozora_wasm_context_set_interrupt(yozora_wasm_context *c, yozora_wasm_interrupt hook, void *user)
{
    if (!c)
        return;
    c->interrupt = hook;
    c->interrupt_user = user;
}

void yozora_wasm_context_set_limits(yozora_wasm_context *c, uint32_t max_depth, uint32_t stack_slots)
{
    if (!c || c->allocated)
        return;
    if (max_depth)
        c->max_depth = max_depth;
    if (stack_slots >= 1024)
        c->stack_slots = stack_slots;
}

static int context_allocate(yozora_wasm_context *c)
{
    if (c->allocated)
        return 1;
    c->stack = (slot *)malloc((size_t)c->stack_slots * sizeof(slot));
    c->frames = (frame *)malloc((size_t)c->max_depth * sizeof(frame));
    if (!c->stack || !c->frames) {
        free(c->stack);
        free(c->frames);
        c->stack = NULL;
        c->frames = NULL;
        return 0;
    }
    c->sp = c->stack;
    c->depth = 0;
    c->allocated = 1;
    return 1;
}

static float f32_of(slot v)
{
    uint32_t bits = (uint32_t)v;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static double f64_of(slot v)
{
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

static slot slot_f32(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static slot slot_f64(double d)
{
    slot v;
    memcpy(&v, &d, sizeof(v));
    return v;
}

static uint32_t clz32(uint32_t x)
{
    uint32_t n = 0;
    if (!x)
        return 32;
    if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xff000000u)) { n += 8; x <<= 8; }
    if (!(x & 0xf0000000u)) { n += 4; x <<= 4; }
    if (!(x & 0xc0000000u)) { n += 2; x <<= 2; }
    if (!(x & 0x80000000u))
        n += 1;
    return n;
}

static uint32_t ctz32(uint32_t x)
{
    uint32_t n = 0;
    if (!x)
        return 32;
    if (!(x & 0x0000ffffu)) { n += 16; x >>= 16; }
    if (!(x & 0x000000ffu)) { n += 8; x >>= 8; }
    if (!(x & 0x0000000fu)) { n += 4; x >>= 4; }
    if (!(x & 0x00000003u)) { n += 2; x >>= 2; }
    if (!(x & 0x00000001u))
        n += 1;
    return n;
}

static uint32_t popcnt32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0f0f0f0fu;
    return (x * 0x01010101u) >> 24;
}

static uint64_t clz64(uint64_t x)
{
    return (x >> 32) ? clz32((uint32_t)(x >> 32)) : 32 + clz32((uint32_t)x);
}

static uint64_t ctz64(uint64_t x)
{
    return (uint32_t)x ? ctz32((uint32_t)x) : 32 + ctz32((uint32_t)(x >> 32));
}

static uint64_t popcnt64(uint64_t x) { return popcnt32((uint32_t)x) + popcnt32((uint32_t)(x >> 32)); }

static uint32_t sar32(uint32_t a, uint32_t s)
{
    uint32_t r;
    s &= 31;
    r = a >> s;
    if ((a & 0x80000000u) && s)
        r |= ~(0xffffffffu >> s);
    return r;
}

static uint64_t sar64(uint64_t a, uint64_t s)
{
    uint64_t r;
    s &= 63;
    r = a >> s;
    if ((a & 0x8000000000000000ull) && s)
        r |= ~(0xffffffffffffffffull >> s);
    return r;
}

static float min32(float a, float b)
{
    if (a != a || b != b)
        return a + b;
    if (a == 0 && b == 0)
        return signbit(a) ? a : b;
    return a < b ? a : b;
}

static float max32(float a, float b)
{
    if (a != a || b != b)
        return a + b;
    if (a == 0 && b == 0)
        return signbit(a) ? b : a;
    return a > b ? a : b;
}

static double min64(double a, double b)
{
    if (a != a || b != b)
        return a + b;
    if (a == 0 && b == 0)
        return signbit(a) ? a : b;
    return a < b ? a : b;
}

static double max64(double a, double b)
{
    if (a != a || b != b)
        return a + b;
    if (a == 0 && b == 0)
        return signbit(a) ? b : a;
    return a > b ? a : b;
}

static slot sat_i32_s(double d)
{
    if (d != d)
        return 0;
    if (d <= -2147483648.0)
        return (uint32_t)INT32_MIN;
    if (d >= 2147483647.0 + 1.0)
        return (uint32_t)INT32_MAX;
    return (uint32_t)(int32_t)d;
}

static slot sat_i32_u(double d)
{
    if (d != d || d <= 0)
        return 0;
    if (d >= 4294967296.0)
        return 0xffffffffu;
    return (uint32_t)d;
}

static slot sat_i64_s(double d)
{
    if (d != d)
        return 0;
    if (d <= -9223372036854775808.0)
        return (uint64_t)INT64_MIN;
    if (d >= 9223372036854775808.0)
        return (uint64_t)INT64_MAX;
    return (uint64_t)(int64_t)d;
}

static slot sat_i64_u(double d)
{
    if (d != d || d <= 0)
        return 0;
    if (d >= 18446744073709551616.0)
        return 0xffffffffffffffffull;
    return (uint64_t)d;
}

void yozora_wasm_instance_retain(yozora_wasm_instance *i)
{
    if (i)
        ++i->refs;
}

static void instance_destroy(yozora_wasm_instance *i)
{
    uint32_t k;
    const yozora_wasm_module *m = i->module;
    if (m) {
        if (i->funcs) {
            for (k = 0; k < m->imported_funcs; ++k)
                yozora_wasm_func_release(i->funcs[k]);
        }
        if (i->tables) {
            for (k = 0; k < m->table_count; ++k)
                yozora_wasm_table_release(i->tables[k]);
        }
        if (i->memories) {
            for (k = 0; k < m->memory_count; ++k)
                yozora_wasm_memory_release(i->memories[k]);
        }
        if (i->globals) {
            for (k = 0; k < m->global_count; ++k)
                yozora_wasm_global_release(i->globals[k]);
        }
    }
    free(i->funcs);
    free(i->own);
    free(i->tables);
    free(i->memories);
    free(i->globals);
    free(i->dropped_data);
    free(i->dropped_elements);
    yozora_wasm_module_release(i->module);
    free(i);
}

void yozora_wasm_instance_release(yozora_wasm_instance *i)
{
    if (!i || --i->refs)
        return;
    instance_destroy(i);
}

static slot eval_const(const yozora_wasm_instance *inst, const yw_const *c)
{
    switch (c->op) {
    case YW_CONST_GLOBAL:
        return inst->globals[c->bits]->value;
    case YW_CONST_REF_NULL:
        return 0;
    case YW_CONST_REF_FUNC:
        return (slot)(uintptr_t)inst->funcs[c->bits];
    default:
        return c->bits;
    }
}

static void write_error(char *error, size_t capacity, const char *format, ...) YW_PRINTF(3, 4);
static void write_error(char *error, size_t capacity, const char *format, ...)
{
    va_list arguments;
    if (!error || !capacity)
        return;
    va_start(arguments, format);
    vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
}

static yozora_wasm_status call_function(yozora_wasm_context *c, yozora_wasm_func *f, const slot *arguments, slot *results);

static int import_matches(const yw_import *im, const yozora_wasm_module *m, const yozora_wasm_extern *e, char *error, size_t capacity)
{
    switch (im->kind) {
    case YOZORA_WASM_KIND_FUNCTION:
        if (!e->object.function || !same_type(e->object.function->type, &m->types[im->type_index])) {
            write_error(error, capacity, "import %s.%s: the function has another type", im->module, im->field);
            return 0;
        }
        return 1;
    case YOZORA_WASM_KIND_TABLE: {
        const yozora_wasm_table *t = e->object.table;
        if (!t || t->type != im->table.element_type || t->size < im->table.minimum || (im->table.has_maximum && (!t->has_maximum || t->maximum > im->table.maximum))) {
            write_error(error, capacity, "import %s.%s: the table does not fit", im->module, im->field);
            return 0;
        }
        return 1;
    }
    case YOZORA_WASM_KIND_MEMORY: {
        const yozora_wasm_memory *mem = e->object.memory;
        if (!mem || mem->pages < im->memory.minimum || (im->memory.has_maximum && (!mem->has_maximum || mem->maximum > im->memory.maximum))) {
            write_error(error, capacity, "import %s.%s: the memory does not fit", im->module, im->field);
            return 0;
        }
        return 1;
    }
    default: {
        const yozora_wasm_global *g = e->object.global;
        if (!g || g->type != im->global.type || g->is_mutable != im->global.is_mutable) {
            write_error(error, capacity, "import %s.%s: the global has another type or mutability", im->module, im->field);
            return 0;
        }
        return 1;
    }
    }
}

yozora_wasm_status yozora_wasm_instantiate(yozora_wasm_context *context, yozora_wasm_module *module, const yozora_wasm_extern *imports, uint32_t import_count,
    yozora_wasm_instance **out, char *error, size_t error_capacity)
{
    yozora_wasm_instance *inst;
    uint32_t i, fi = 0, ti = 0, mi = 0, gi = 0, defined = module->func_count - module->imported_funcs;
    yozora_wasm_status status = YOZORA_WASM_OK;
    *out = NULL;
    if (module->has_start && !context) {
        write_error(error, error_capacity, "a module with a start function needs a context");
        return YOZORA_WASM_LINK_ERROR;
    }
    if (import_count != module->import_count) {
        write_error(error, error_capacity, "the module has %u imports, %u were given", module->import_count, import_count);
        return YOZORA_WASM_LINK_ERROR;
    }
    for (i = 0; i < import_count; ++i) {
        if (imports[i].kind != module->imports[i].kind) {
            write_error(error, error_capacity, "import %s.%s: the kind is not the one the module imports", module->imports[i].module, module->imports[i].field);
            return YOZORA_WASM_LINK_ERROR;
        }
        if (!import_matches(&module->imports[i], module, &imports[i], error, error_capacity))
            return YOZORA_WASM_LINK_ERROR;
    }
    inst = (yozora_wasm_instance *)calloc(1, sizeof(*inst));
    if (!inst) {
        write_error(error, error_capacity, "out of memory");
        return YOZORA_WASM_NO_MEMORY;
    }
    inst->refs = 1;
    inst->module = module;
    yozora_wasm_module_retain(module);
    inst->funcs = (yozora_wasm_func **)calloc(module->func_count ? module->func_count : 1, sizeof(void *));
    inst->own = (yozora_wasm_func *)calloc(defined ? defined : 1, sizeof(yozora_wasm_func));
    inst->tables = (yozora_wasm_table **)calloc(module->table_count ? module->table_count : 1, sizeof(void *));
    inst->memories = (yozora_wasm_memory **)calloc(module->memory_count ? module->memory_count : 1, sizeof(void *));
    inst->globals = (yozora_wasm_global **)calloc(module->global_count ? module->global_count : 1, sizeof(void *));
    inst->dropped_data = (uint8_t *)calloc(module->data_count ? module->data_count : 1, 1);
    inst->dropped_elements = (uint8_t *)calloc(module->elem_count ? module->elem_count : 1, 1);
    if (!inst->funcs || !inst->own || !inst->tables || !inst->memories || !inst->globals || !inst->dropped_data || !inst->dropped_elements) {
        write_error(error, error_capacity, "out of memory");
        /* the arrays are not filled yet: the destroy walks only what was set, and the import loops of it read null */
        inst->module = NULL;
        free(inst->funcs);
        free(inst->own);
        free(inst->tables);
        free(inst->memories);
        free(inst->globals);
        free(inst->dropped_data);
        free(inst->dropped_elements);
        free(inst);
        yozora_wasm_module_release(module);
        return YOZORA_WASM_NO_MEMORY;
    }
    /* the imports are taken first, in their order */
    for (i = 0; i < import_count; ++i) {
        switch (imports[i].kind) {
        case YOZORA_WASM_KIND_FUNCTION:
            yozora_wasm_func_retain(imports[i].object.function);
            inst->funcs[fi++] = imports[i].object.function;
            break;
        case YOZORA_WASM_KIND_TABLE:
            yozora_wasm_table_retain(imports[i].object.table);
            inst->tables[ti++] = imports[i].object.table;
            break;
        case YOZORA_WASM_KIND_MEMORY:
            yozora_wasm_memory_retain(imports[i].object.memory);
            inst->memories[mi++] = imports[i].object.memory;
            break;
        default:
            yozora_wasm_global_retain(imports[i].object.global);
            inst->globals[gi++] = imports[i].object.global;
            break;
        }
    }
    for (i = 0; i < defined; ++i) {
        yozora_wasm_func *f = &inst->own[i];
        f->instance = inst;
        f->index = module->imported_funcs + i;
        f->definition = &module->defined[i];
        f->type = &module->types[module->func_types[f->index]];
        inst->funcs[f->index] = f;
    }
    for (i = module->imported_tables; i < module->table_count; ++i) {
        const yw_tabletype *t = &module->tables[i];
        inst->tables[i] = yozora_wasm_table_create(t->element_type, t->minimum, t->maximum, t->has_maximum, 0);
        if (!inst->tables[i]) {
            write_error(error, error_capacity, "a table is too large");
            status = YOZORA_WASM_NO_MEMORY;
            goto fail;
        }
    }
    for (i = module->imported_memories; i < module->memory_count; ++i) {
        const yw_memtype *t = &module->memories[i];
        inst->memories[i] = yozora_wasm_memory_create(t->minimum, t->maximum, t->has_maximum);
        if (!inst->memories[i]) {
            write_error(error, error_capacity, "a memory of %u pages cannot be made, the system has no room for it", t->minimum);
            status = YOZORA_WASM_NO_MEMORY;
            goto fail;
        }
    }
    for (i = module->imported_globals; i < module->global_count; ++i) {
        inst->globals[i] = yozora_wasm_global_create(module->globals[i].type, module->globals[i].is_mutable, eval_const(inst, &module->global_inits[i]));
        if (!inst->globals[i]) {
            write_error(error, error_capacity, "out of memory");
            status = YOZORA_WASM_NO_MEMORY;
            goto fail;
        }
    }
    /* active element segments fill their tables, then active data segments their memory, in order. a segment
       that does not fit traps and leaves what came before */
    for (i = 0; i < module->elem_count; ++i) {
        const yw_elemseg *e = &module->elems[i];
        if (e->mode == YW_SEGMENT_ACTIVE) {
            uint32_t offset = (uint32_t)eval_const(inst, &e->offset), k;
            yozora_wasm_table *t = inst->tables[e->table];
            if ((uint64_t)offset + e->count > t->size) {
                write_error(error, error_capacity, "%s", trap_table);
                status = YOZORA_WASM_TRAP;
                goto fail;
            }
            for (k = 0; k < e->count; ++k)
                table_store(t, offset + k, eval_const(inst, &e->items[k]));
            inst->dropped_elements[i] = 1;
        } else if (e->mode == YW_SEGMENT_DECLARATIVE)
            inst->dropped_elements[i] = 1;
    }
    for (i = 0; i < module->data_count; ++i) {
        const yw_dataseg *d = &module->datas[i];
        if (d->mode == YW_SEGMENT_ACTIVE) {
            uint32_t offset = (uint32_t)eval_const(inst, &d->offset);
            yozora_wasm_memory *mem = inst->memories[0];
            if ((uint64_t)offset + d->size > (uint64_t)mem->pages * YOZORA_WASM_PAGE_SIZE) {
                write_error(error, error_capacity, "%s", trap_memory);
                status = YOZORA_WASM_TRAP;
                goto fail;
            }
            if (d->size)
                memcpy(mem->data + offset, d->bytes, d->size);
            inst->dropped_data[i] = 1;
        }
    }
    if (module->has_start) {
        status = call_function(context, inst->funcs[module->start], NULL, NULL);
        if (status != YOZORA_WASM_OK) {
            write_error(error, error_capacity, "%s", context->trap ? context->trap : "the start function failed");
            goto fail;
        }
    }
    *out = inst;
    return YOZORA_WASM_OK;
fail:
    yozora_wasm_instance_release(inst);
    return status;
}

int yozora_wasm_instance_export(yozora_wasm_instance *inst, uint32_t index, yozora_wasm_extern *out)
{
    const yw_export *e;
    if (index >= inst->module->export_count)
        return 0;
    e = &inst->module->exports[index];
    out->kind = e->kind;
    switch (e->kind) {
    case YOZORA_WASM_KIND_FUNCTION:
        out->object.function = inst->funcs[e->index];
        yozora_wasm_func_retain(out->object.function);
        break;
    case YOZORA_WASM_KIND_TABLE:
        out->object.table = inst->tables[e->index];
        yozora_wasm_table_retain(out->object.table);
        break;
    case YOZORA_WASM_KIND_MEMORY:
        out->object.memory = inst->memories[e->index];
        yozora_wasm_memory_retain(out->object.memory);
        break;
    default:
        out->object.global = inst->globals[e->index];
        yozora_wasm_global_retain(out->object.global);
        break;
    }
    return 1;
}

#define MEMORY_RELOAD() \
    do { \
        if (instance->module->memory_count) { \
            memory = instance->memories[0]; \
            mem = memory->data; \
            memory_size = (uint64_t)memory->pages * YOZORA_WASM_PAGE_SIZE; \
        } \
    } while (0)

#define TRAP(message) \
    do { \
        context->trap = (message); \
        status = YOZORA_WASM_TRAP; \
        goto finish; \
    } while (0)

/* a branch carries the values the target takes down to the height it expects, then jumps */
#define POLL_INTERRUPT() \
    do { \
        if (!(++ticks & 0xffffu) && context->interrupt && context->interrupt(context->interrupt_user)) { \
            context->trap = "execution was terminated"; \
            status = YOZORA_WASM_INTERRUPTED; \
            goto finish; \
        } \
    } while (0)

#define TAKE_BRANCH(words) \
    do { \
        POLL_INTERRUPT(); \
        uint32_t target = (words)[0], height = (words)[1], arity = (words)[2]; \
        if (arity && sp - arity != fp + height) \
            memmove(fp + height, sp - arity, (size_t)arity * sizeof(slot)); \
        sp = fp + height + arity; \
        pc = code + target; \
    } while (0)

#define BINARY32(op, expression) \
    case op: { \
        uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2]; \
        --sp; \
        sp[-1] = (uint32_t)(expression); \
        break; \
    }

#define BINARY64(op, expression) \
    case op: { \
        uint64_t b = sp[-1], a = sp[-2]; \
        --sp; \
        sp[-1] = (uint64_t)(expression); \
        break; \
    }

#define COMPARE64(op, expression) \
    case op: { \
        uint64_t b = sp[-1], a = sp[-2]; \
        --sp; \
        sp[-1] = (uint32_t)(expression); \
        break; \
    }

#define FLOAT32_BINARY(op, expression) \
    case op: { \
        float b = f32_of(sp[-1]), a = f32_of(sp[-2]); \
        --sp; \
        sp[-1] = slot_f32(expression); \
        break; \
    }

#define FLOAT32_COMPARE(op, expression) \
    case op: { \
        float b = f32_of(sp[-1]), a = f32_of(sp[-2]); \
        --sp; \
        sp[-1] = (uint32_t)(expression); \
        break; \
    }

#define FLOAT64_BINARY(op, expression) \
    case op: { \
        double b = f64_of(sp[-1]), a = f64_of(sp[-2]); \
        --sp; \
        sp[-1] = slot_f64(expression); \
        break; \
    }

#define FLOAT64_COMPARE(op, expression) \
    case op: { \
        double b = f64_of(sp[-1]), a = f64_of(sp[-2]); \
        --sp; \
        sp[-1] = (uint32_t)(expression); \
        break; \
    }

#define LOAD(op, size, expression) \
    case op: { \
        uint64_t address = (uint64_t)(uint32_t)sp[-1] + *pc++; \
        uint8_t raw[8]; \
        if (address + (size) > memory_size) \
            TRAP(trap_memory); \
        memcpy(raw, mem + address, (size)); \
        sp[-1] = (expression); \
        break; \
    }

#define STORE(op, size, bytes) \
    case op: { \
        uint64_t address = (uint64_t)(uint32_t)sp[-2] + *pc++; \
        if (address + (size) > memory_size) \
            TRAP(trap_memory); \
        { \
            bytes; \
        } \
        sp -= 2; \
        break; \
    }

static uint32_t read32(const uint8_t *raw)
{
    uint32_t v;
    memcpy(&v, raw, 4);
    return v;
}

static uint64_t read64(const uint8_t *raw)
{
    uint64_t v;
    memcpy(&v, raw, 8);
    return v;
}

static uint16_t read16(const uint8_t *raw)
{
    uint16_t v;
    memcpy(&v, raw, 2);
    return v;
}

/* runs the function whose frame starts at fp with its parameters already there, returns when it returns,
   results at fp */
static yozora_wasm_status run(yozora_wasm_context *context, yozora_wasm_func *entry, slot *fp)
{
    yozora_wasm_status status = YOZORA_WASM_OK;
    const yw_funcdef *definition = entry->definition;
    yozora_wasm_instance *instance = entry->instance;
    const uint32_t *code = definition->code;
    const uint32_t *pc = code;
    slot *sp;
    slot *limit = context->stack + context->stack_slots;
    frame *frames = context->frames;
    uint32_t depth = context->depth;
    const uint32_t entry_depth = depth;
    yozora_wasm_memory *memory = NULL;
    uint8_t *mem = NULL;
    uint64_t memory_size = 0;
    uint32_t i;
    uint32_t ticks = 0;

    if (fp + definition->max_stack > limit)
        TRAP(trap_stack);
    for (i = definition->nparams; i < definition->locals; ++i)
        fp[i] = 0;
    sp = fp + definition->locals;
    MEMORY_RELOAD();

    for (;;) {
        uint32_t op = *pc++;
        switch (op) {
        case YW_OP_UNREACHABLE:
            TRAP(trap_unreachable);
        case YW_OP_JMP:
            pc = code + pc[0];
            break;
        case YW_OP_IF: {
            uint32_t condition = (uint32_t)*--sp;
            if (condition)
                ++pc;
            else
                pc = code + pc[0];
            break;
        }
        case YW_OP_BR_IF: {
            uint32_t condition = (uint32_t)*--sp;
            if (condition)
                TAKE_BRANCH(pc);
            else
                pc += 3;
            break;
        }
        case YW_OP_BR:
            TAKE_BRANCH(pc);
            break;
        case YW_OP_BR_TABLE: {
            uint32_t count = pc[0];
            uint32_t selector = (uint32_t)*--sp;
            const uint32_t *words = pc + 1 + 3 * (selector < count ? selector : count);
            TAKE_BRANCH(words);
            break;
        }
        case YW_OP_RETURN: {
            uint32_t n = definition->nresults;
            if (n && sp - n != fp)
                memmove(fp, sp - n, (size_t)n * sizeof(slot));
            sp = fp + n;
            if (depth == entry_depth)
                goto finish;
            --depth;
            pc = frames[depth].pc;
            fp = frames[depth].fp;
            definition = frames[depth].definition;
            instance = frames[depth].instance;
            code = definition->code;
            MEMORY_RELOAD();
            break;
        }
        case YW_OP_CALL:
        case YW_OP_CALL_INDIRECT: {
            yozora_wasm_func *callee;
            POLL_INTERRUPT();
            if (op == YW_OP_CALL)
                callee = instance->funcs[*pc++];
            else {
                uint32_t type_index = pc[0], table_index = pc[1];
                uint32_t element = (uint32_t)*--sp;
                const yozora_wasm_table *table = instance->tables[table_index];
                pc += 2;
                if (element >= table->size)
                    TRAP(trap_table);
                callee = (yozora_wasm_func *)(uintptr_t)table->elements[element];
                if (!callee)
                    TRAP(trap_null);
                if (!same_type(callee->type, &instance->module->types[type_index]))
                    TRAP(trap_signature);
            }
            if (callee->is_host) {
                slot results[YOZORA_WASM_MAX_RESULTS];
                uint32_t np = callee->type->nparams, nr = callee->type->nresults;
                yozora_wasm_status host_status;
                context->sp = sp;
                context->depth = depth;
                host_status = callee->host(callee->user, context, sp - np, results);
                if (host_status != YOZORA_WASM_OK) {
                    status = host_status;
                    goto finish;
                }
                sp -= np;
                for (i = 0; i < nr; ++i)
                    sp[i] = results[i];
                sp += nr;
                MEMORY_RELOAD();
            } else {
                const yw_funcdef *callee_definition = callee->definition;
                slot *callee_fp = sp - callee_definition->nparams;
                if (depth + 1 >= context->max_depth)
                    TRAP(trap_stack);
                if (callee_fp + callee_definition->max_stack > limit)
                    TRAP(trap_stack);
                frames[depth].pc = pc;
                frames[depth].fp = fp;
                frames[depth].definition = definition;
                frames[depth].instance = instance;
                ++depth;
                definition = callee_definition;
                instance = callee->instance;
                code = definition->code;
                pc = code;
                fp = callee_fp;
                for (i = definition->nparams; i < definition->locals; ++i)
                    fp[i] = 0;
                sp = fp + definition->locals;
                MEMORY_RELOAD();
            }
            break;
        }
        case YW_OP_DROP:
            --sp;
            break;
        case YW_OP_SELECT: {
            uint32_t condition = (uint32_t)*--sp;
            --sp;
            if (!condition)
                sp[-1] = sp[0];
            break;
        }
        case YW_OP_LOCAL_GET:
            *sp++ = fp[*pc++];
            break;
        case YW_OP_LOCAL_SET:
            fp[*pc++] = *--sp;
            break;
        case YW_OP_LOCAL_TEE:
            fp[*pc++] = sp[-1];
            break;
        case YW_OP_GLOBAL_GET:
            *sp++ = instance->globals[*pc++]->value;
            break;
        case YW_OP_GLOBAL_SET:
            global_store(instance->globals[*pc++], *--sp);
            break;
        case YW_OP_TABLE_GET: {
            yozora_wasm_table *table = instance->tables[*pc++];
            uint32_t index = (uint32_t)sp[-1];
            if (index >= table->size)
                TRAP(trap_table);
            sp[-1] = table->elements[index];
            break;
        }
        case YW_OP_TABLE_SET: {
            yozora_wasm_table *table = instance->tables[*pc++];
            uint32_t index = (uint32_t)sp[-2];
            if (index >= table->size)
                TRAP(trap_table);
            table_store(table, index, sp[-1]);
            sp -= 2;
            break;
        }
        LOAD(0x28, 4, read32(raw))
        LOAD(0x29, 8, read64(raw))
        LOAD(0x2a, 4, read32(raw))
        LOAD(0x2b, 8, read64(raw))
        LOAD(0x2c, 1, (uint32_t)(int32_t)(int8_t)raw[0])
        LOAD(0x2d, 1, raw[0])
        LOAD(0x2e, 2, (uint32_t)(int32_t)(int16_t)read16(raw))
        LOAD(0x2f, 2, read16(raw))
        LOAD(0x30, 1, (uint64_t)(int64_t)(int8_t)raw[0])
        LOAD(0x31, 1, raw[0])
        LOAD(0x32, 2, (uint64_t)(int64_t)(int16_t)read16(raw))
        LOAD(0x33, 2, read16(raw))
        LOAD(0x34, 4, (uint64_t)(int64_t)(int32_t)read32(raw))
        LOAD(0x35, 4, read32(raw))
        STORE(0x36, 4, { uint32_t v = (uint32_t)sp[-1]; memcpy(mem + address, &v, 4); })
        STORE(0x37, 8, { uint64_t v = sp[-1]; memcpy(mem + address, &v, 8); })
        STORE(0x38, 4, { uint32_t v = (uint32_t)sp[-1]; memcpy(mem + address, &v, 4); })
        STORE(0x39, 8, { uint64_t v = sp[-1]; memcpy(mem + address, &v, 8); })
        STORE(0x3a, 1, { mem[address] = (uint8_t)sp[-1]; })
        STORE(0x3b, 2, { uint16_t v = (uint16_t)sp[-1]; memcpy(mem + address, &v, 2); })
        STORE(0x3c, 1, { mem[address] = (uint8_t)sp[-1]; })
        STORE(0x3d, 2, { uint16_t v = (uint16_t)sp[-1]; memcpy(mem + address, &v, 2); })
        STORE(0x3e, 4, { uint32_t v = (uint32_t)sp[-1]; memcpy(mem + address, &v, 4); })
        case YW_OP_MEMORY_SIZE:
            *sp++ = memory->pages;
            break;
        case YW_OP_MEMORY_GROW: {
            int64_t old = yozora_wasm_memory_grow(memory, (uint32_t)sp[-1]);
            sp[-1] = (uint32_t)(old < 0 ? -1 : old);
            MEMORY_RELOAD();
            break;
        }
        case YW_OP_I32_CONST:
            *sp++ = *pc++;
            break;
        case YW_OP_I64_CONST:
        case YW_OP_F64_CONST:
            *sp++ = (uint64_t)pc[0] | ((uint64_t)pc[1] << 32);
            pc += 2;
            break;
        case YW_OP_F32_CONST:
            *sp++ = *pc++;
            break;
        case YW_OP_REF_NULL:
            *sp++ = 0;
            break;
        case YW_OP_REF_IS_NULL:
            sp[-1] = sp[-1] == 0;
            break;
        case YW_OP_REF_FUNC:
            *sp++ = (slot)(uintptr_t)instance->funcs[*pc++];
            break;

        /* i32 */
        case 0x45: sp[-1] = (uint32_t)sp[-1] == 0; break;
        BINARY32(0x46, a == b)
        BINARY32(0x47, a != b)
        BINARY32(0x48, (int32_t)a < (int32_t)b)
        BINARY32(0x49, a < b)
        BINARY32(0x4a, (int32_t)a > (int32_t)b)
        BINARY32(0x4b, a > b)
        BINARY32(0x4c, (int32_t)a <= (int32_t)b)
        BINARY32(0x4d, a <= b)
        BINARY32(0x4e, (int32_t)a >= (int32_t)b)
        BINARY32(0x4f, a >= b)
        /* i64 */
        case 0x50: sp[-1] = sp[-1] == 0; break;
        COMPARE64(0x51, a == b)
        COMPARE64(0x52, a != b)
        COMPARE64(0x53, (int64_t)a < (int64_t)b)
        COMPARE64(0x54, a < b)
        COMPARE64(0x55, (int64_t)a > (int64_t)b)
        COMPARE64(0x56, a > b)
        COMPARE64(0x57, (int64_t)a <= (int64_t)b)
        COMPARE64(0x58, a <= b)
        COMPARE64(0x59, (int64_t)a >= (int64_t)b)
        COMPARE64(0x5a, a >= b)
        /* f32 and f64 comparisons */
        FLOAT32_COMPARE(0x5b, a == b)
        FLOAT32_COMPARE(0x5c, a != b)
        FLOAT32_COMPARE(0x5d, a < b)
        FLOAT32_COMPARE(0x5e, a > b)
        FLOAT32_COMPARE(0x5f, a <= b)
        FLOAT32_COMPARE(0x60, a >= b)
        FLOAT64_COMPARE(0x61, a == b)
        FLOAT64_COMPARE(0x62, a != b)
        FLOAT64_COMPARE(0x63, a < b)
        FLOAT64_COMPARE(0x64, a > b)
        FLOAT64_COMPARE(0x65, a <= b)
        FLOAT64_COMPARE(0x66, a >= b)
        case 0x67: sp[-1] = clz32((uint32_t)sp[-1]); break;
        case 0x68: sp[-1] = ctz32((uint32_t)sp[-1]); break;
        case 0x69: sp[-1] = popcnt32((uint32_t)sp[-1]); break;
        BINARY32(0x6a, a + b)
        BINARY32(0x6b, a - b)
        BINARY32(0x6c, a * b)
        case 0x6d: {
            uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2];
            if (b == 0)
                TRAP(trap_divide);
            if ((int32_t)a == INT32_MIN && (int32_t)b == -1)
                TRAP(trap_overflow);
            --sp;
            sp[-1] = (uint32_t)((int32_t)a / (int32_t)b);
            break;
        }
        case 0x6e: {
            uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2];
            if (b == 0)
                TRAP(trap_divide);
            --sp;
            sp[-1] = a / b;
            break;
        }
        case 0x6f: {
            uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2];
            if (b == 0)
                TRAP(trap_remainder);
            --sp;
            sp[-1] = (int32_t)b == -1 ? 0 : (uint32_t)((int32_t)a % (int32_t)b);
            break;
        }
        case 0x70: {
            uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2];
            if (b == 0)
                TRAP(trap_remainder);
            --sp;
            sp[-1] = a % b;
            break;
        }
        BINARY32(0x71, a & b)
        BINARY32(0x72, a | b)
        BINARY32(0x73, a ^ b)
        BINARY32(0x74, a << (b & 31))
        BINARY32(0x75, sar32(a, b))
        BINARY32(0x76, a >> (b & 31))
        BINARY32(0x77, (a << (b & 31)) | (a >> ((32 - (b & 31)) & 31)))
        BINARY32(0x78, (a >> (b & 31)) | (a << ((32 - (b & 31)) & 31)))
        /* i64 arithmetic */
        case 0x79: sp[-1] = clz64(sp[-1]); break;
        case 0x7a: sp[-1] = ctz64(sp[-1]); break;
        case 0x7b: sp[-1] = popcnt64(sp[-1]); break;
        BINARY64(0x7c, a + b)
        BINARY64(0x7d, a - b)
        BINARY64(0x7e, a * b)
        case 0x7f: {
            uint64_t b = sp[-1], a = sp[-2];
            if (b == 0)
                TRAP(trap_divide);
            if ((int64_t)a == INT64_MIN && (int64_t)b == -1)
                TRAP(trap_overflow);
            --sp;
            sp[-1] = (uint64_t)((int64_t)a / (int64_t)b);
            break;
        }
        case 0x80: {
            uint64_t b = sp[-1], a = sp[-2];
            if (b == 0)
                TRAP(trap_divide);
            --sp;
            sp[-1] = a / b;
            break;
        }
        case 0x81: {
            uint64_t b = sp[-1], a = sp[-2];
            if (b == 0)
                TRAP(trap_remainder);
            --sp;
            sp[-1] = (int64_t)b == -1 ? 0 : (uint64_t)((int64_t)a % (int64_t)b);
            break;
        }
        case 0x82: {
            uint64_t b = sp[-1], a = sp[-2];
            if (b == 0)
                TRAP(trap_remainder);
            --sp;
            sp[-1] = a % b;
            break;
        }
        BINARY64(0x83, a & b)
        BINARY64(0x84, a | b)
        BINARY64(0x85, a ^ b)
        BINARY64(0x86, a << (b & 63))
        BINARY64(0x87, sar64(a, b))
        BINARY64(0x88, a >> (b & 63))
        BINARY64(0x89, (a << (b & 63)) | (a >> ((64 - (b & 63)) & 63)))
        BINARY64(0x8a, (a >> (b & 63)) | (a << ((64 - (b & 63)) & 63)))
        /* f32 */
        case 0x8b: sp[-1] = (uint32_t)sp[-1] & 0x7fffffffu; break;
        case 0x8c: sp[-1] = (uint32_t)sp[-1] ^ 0x80000000u; break;
        case 0x8d: sp[-1] = slot_f32(ceilf(f32_of(sp[-1]))); break;
        case 0x8e: sp[-1] = slot_f32(floorf(f32_of(sp[-1]))); break;
        case 0x8f: sp[-1] = slot_f32(truncf(f32_of(sp[-1]))); break;
        case 0x90: sp[-1] = slot_f32(nearbyintf(f32_of(sp[-1]))); break;
        case 0x91: sp[-1] = slot_f32(sqrtf(f32_of(sp[-1]))); break;
        FLOAT32_BINARY(0x92, a + b)
        FLOAT32_BINARY(0x93, a - b)
        FLOAT32_BINARY(0x94, a * b)
        FLOAT32_BINARY(0x95, a / b)
        FLOAT32_BINARY(0x96, min32(a, b))
        FLOAT32_BINARY(0x97, max32(a, b))
        case 0x98: {
            uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2];
            --sp;
            sp[-1] = (a & 0x7fffffffu) | (b & 0x80000000u);
            break;
        }
        /* f64 */
        case 0x99: sp[-1] = sp[-1] & 0x7fffffffffffffffull; break;
        case 0x9a: sp[-1] = sp[-1] ^ 0x8000000000000000ull; break;
        case 0x9b: sp[-1] = slot_f64(ceil(f64_of(sp[-1]))); break;
        case 0x9c: sp[-1] = slot_f64(floor(f64_of(sp[-1]))); break;
        case 0x9d: sp[-1] = slot_f64(trunc(f64_of(sp[-1]))); break;
        case 0x9e: sp[-1] = slot_f64(nearbyint(f64_of(sp[-1]))); break;
        case 0x9f: sp[-1] = slot_f64(sqrt(f64_of(sp[-1]))); break;
        FLOAT64_BINARY(0xa0, a + b)
        FLOAT64_BINARY(0xa1, a - b)
        FLOAT64_BINARY(0xa2, a * b)
        FLOAT64_BINARY(0xa3, a / b)
        FLOAT64_BINARY(0xa4, min64(a, b))
        FLOAT64_BINARY(0xa5, max64(a, b))
        case 0xa6: {
            uint64_t b = sp[-1], a = sp[-2];
            --sp;
            sp[-1] = (a & 0x7fffffffffffffffull) | (b & 0x8000000000000000ull);
            break;
        }
        /* conversions */
        case 0xa7: sp[-1] = (uint32_t)sp[-1]; break;
        case 0xa8: {
            float f = f32_of(sp[-1]);
            if (f != f)
                TRAP(trap_conversion);
            if (!(f >= -2147483648.0f && f < 2147483648.0f))
                TRAP(trap_conversion);
            sp[-1] = (uint32_t)(int32_t)f;
            break;
        }
        case 0xa9: {
            float f = f32_of(sp[-1]);
            if (f != f || !(f > -1.0f && f < 4294967296.0f))
                TRAP(trap_conversion);
            sp[-1] = (uint32_t)f;
            break;
        }
        case 0xaa: {
            double d = f64_of(sp[-1]);
            if (d != d || !(d > -2147483649.0 && d < 2147483648.0))
                TRAP(trap_conversion);
            sp[-1] = (uint32_t)(int32_t)d;
            break;
        }
        case 0xab: {
            double d = f64_of(sp[-1]);
            if (d != d || !(d > -1.0 && d < 4294967296.0))
                TRAP(trap_conversion);
            sp[-1] = (uint32_t)d;
            break;
        }
        case 0xac: sp[-1] = (uint64_t)(int64_t)(int32_t)(uint32_t)sp[-1]; break;
        case 0xad: sp[-1] = (uint32_t)sp[-1]; break;
        case 0xae: {
            float f = f32_of(sp[-1]);
            if (f != f || !(f >= -9223372036854775808.0f && f < 9223372036854775808.0f))
                TRAP(trap_conversion);
            sp[-1] = (uint64_t)(int64_t)f;
            break;
        }
        case 0xaf: {
            float f = f32_of(sp[-1]);
            if (f != f || !(f > -1.0f && f < 18446744073709551616.0f))
                TRAP(trap_conversion);
            sp[-1] = (uint64_t)f;
            break;
        }
        case 0xb0: {
            double d = f64_of(sp[-1]);
            if (d != d || !(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
                TRAP(trap_conversion);
            sp[-1] = (uint64_t)(int64_t)d;
            break;
        }
        case 0xb1: {
            double d = f64_of(sp[-1]);
            if (d != d || !(d > -1.0 && d < 18446744073709551616.0))
                TRAP(trap_conversion);
            sp[-1] = (uint64_t)d;
            break;
        }
        case 0xb2: sp[-1] = slot_f32((float)(int32_t)(uint32_t)sp[-1]); break;
        case 0xb3: sp[-1] = slot_f32((float)(uint32_t)sp[-1]); break;
        case 0xb4: sp[-1] = slot_f32((float)(int64_t)sp[-1]); break;
        case 0xb5: sp[-1] = slot_f32((float)sp[-1]); break;
        case 0xb6: sp[-1] = slot_f32((float)f64_of(sp[-1])); break;
        case 0xb7: sp[-1] = slot_f64((double)(int32_t)(uint32_t)sp[-1]); break;
        case 0xb8: sp[-1] = slot_f64((double)(uint32_t)sp[-1]); break;
        case 0xb9: sp[-1] = slot_f64((double)(int64_t)sp[-1]); break;
        case 0xba: sp[-1] = slot_f64((double)sp[-1]); break;
        case 0xbb: sp[-1] = slot_f64((double)f32_of(sp[-1])); break;
        case 0xbc: sp[-1] = (uint32_t)sp[-1]; break;
        case 0xbd:
        case 0xbf:
            break;
        case 0xbe: sp[-1] = (uint32_t)sp[-1]; break;
        case 0xc0: sp[-1] = (uint32_t)(int32_t)(int8_t)(uint8_t)sp[-1]; break;
        case 0xc1: sp[-1] = (uint32_t)(int32_t)(int16_t)(uint16_t)sp[-1]; break;
        case 0xc2: sp[-1] = (uint64_t)(int64_t)(int8_t)(uint8_t)sp[-1]; break;
        case 0xc3: sp[-1] = (uint64_t)(int64_t)(int16_t)(uint16_t)sp[-1]; break;
        case 0xc4: sp[-1] = (uint64_t)(int64_t)(int32_t)(uint32_t)sp[-1]; break;

        /* the instructions behind the 0xfc prefix */
        case YW_OP_PREFIX + 0: sp[-1] = sat_i32_s((double)f32_of(sp[-1])); break;
        case YW_OP_PREFIX + 1: sp[-1] = sat_i32_u((double)f32_of(sp[-1])); break;
        case YW_OP_PREFIX + 2: sp[-1] = sat_i32_s(f64_of(sp[-1])); break;
        case YW_OP_PREFIX + 3: sp[-1] = sat_i32_u(f64_of(sp[-1])); break;
        case YW_OP_PREFIX + 4: sp[-1] = sat_i64_s((double)f32_of(sp[-1])); break;
        case YW_OP_PREFIX + 5: sp[-1] = sat_i64_u((double)f32_of(sp[-1])); break;
        case YW_OP_PREFIX + 6: sp[-1] = sat_i64_s(f64_of(sp[-1])); break;
        case YW_OP_PREFIX + 7: sp[-1] = sat_i64_u(f64_of(sp[-1])); break;
        case YW_OP_PREFIX + 8: { /* memory.init */
            uint32_t segment = *pc++;
            uint32_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            uint64_t available = instance->dropped_data[segment] ? 0 : instance->module->datas[segment].size;
            if ((uint64_t)s + n > available || (uint64_t)d + n > memory_size)
                TRAP(trap_memory);
            if (n)
                memcpy(mem + d, instance->module->datas[segment].bytes + s, n);
            sp -= 3;
            break;
        }
        case YW_OP_PREFIX + 9:
            instance->dropped_data[*pc++] = 1;
            break;
        case YW_OP_PREFIX + 10: { /* memory.copy */
            uint32_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            if ((uint64_t)s + n > memory_size || (uint64_t)d + n > memory_size)
                TRAP(trap_memory);
            if (n)
                memmove(mem + d, mem + s, n);
            sp -= 3;
            break;
        }
        case YW_OP_PREFIX + 11: { /* memory.fill */
            uint32_t n = (uint32_t)sp[-1], v = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            if ((uint64_t)d + n > memory_size)
                TRAP(trap_memory);
            if (n)
                memset(mem + d, (int)(v & 0xff), n);
            sp -= 3;
            break;
        }
        case YW_OP_PREFIX + 12: { /* table.init */
            uint32_t segment = pc[0], table_index = pc[1], k;
            uint32_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            const yw_elemseg *seg = &instance->module->elems[segment];
            yozora_wasm_table *table = instance->tables[table_index];
            uint64_t available = instance->dropped_elements[segment] ? 0 : seg->count;
            pc += 2;
            if ((uint64_t)s + n > available || (uint64_t)d + n > table->size)
                TRAP(trap_table);
            for (k = 0; k < n; ++k)
                table_store(table, d + k, eval_const(instance, &seg->items[s + k]));
            sp -= 3;
            break;
        }
        case YW_OP_PREFIX + 13:
            instance->dropped_elements[*pc++] = 1;
            break;
        case YW_OP_PREFIX + 14: { /* table.copy */
            yozora_wasm_table *dst = instance->tables[pc[0]], *src = instance->tables[pc[1]];
            uint32_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3], k;
            pc += 2;
            if ((uint64_t)s + n > src->size || (uint64_t)d + n > dst->size)
                TRAP(trap_table);
            if (d <= s) {
                for (k = 0; k < n; ++k)
                    table_store(dst, d + k, src->elements[s + k]);
            } else {
                for (k = n; k > 0; --k)
                    table_store(dst, d + k - 1, src->elements[s + k - 1]);
            }
            sp -= 3;
            break;
        }
        case YW_OP_PREFIX + 15: { /* table.grow */
            yozora_wasm_table *table = instance->tables[*pc++];
            uint32_t n = (uint32_t)sp[-1];
            slot initial = sp[-2];
            int64_t old = yozora_wasm_table_grow(table, n, initial);
            --sp;
            sp[-1] = (uint32_t)(old < 0 ? -1 : old);
            break;
        }
        case YW_OP_PREFIX + 16:
            *sp++ = instance->tables[*pc++]->size;
            break;
        case YW_OP_PREFIX + 17: { /* table.fill */
            yozora_wasm_table *table = instance->tables[*pc++];
            uint32_t n = (uint32_t)sp[-1], k;
            slot value = sp[-2];
            uint32_t d = (uint32_t)sp[-3];
            if ((uint64_t)d + n > table->size)
                TRAP(trap_table);
            for (k = 0; k < n; ++k)
                table_store(table, d + k, value);
            sp -= 3;
            break;
        }
        default:
            TRAP("internal error: an instruction the compiler never emits");
        }
    }
finish:
    context->depth = entry_depth;
    (void)trap_segment;
    return status;
}

static yozora_wasm_status call_function(yozora_wasm_context *context, yozora_wasm_func *f, const slot *arguments, slot *results)
{
    yozora_wasm_status status;
    uint32_t np = f->type->nparams, nr = f->type->nresults, i;
    slot *base;
    context->trap = NULL;
    if (f->is_host) {
        slot local[YOZORA_WASM_MAX_RESULTS];
        slot *out = results ? results : local;
        return f->host(f->user, context, arguments, out);
    }
    if (!context_allocate(context)) {
        context->trap = "out of memory";
        return YOZORA_WASM_NO_MEMORY;
    }
    base = context->sp;
    if (base + f->definition->max_stack + np > context->stack + context->stack_slots) {
        context->trap = trap_stack;
        return YOZORA_WASM_TRAP;
    }
    for (i = 0; i < np; ++i)
        base[i] = arguments[i];
    context->sp = base + np;
    status = run(context, f, base);
    context->sp = base;
    if (status == YOZORA_WASM_OK && results) {
        for (i = 0; i < nr; ++i)
            results[i] = base[i];
    }
    return status;
}

yozora_wasm_status yozora_wasm_call(yozora_wasm_context *context, yozora_wasm_func *function, const slot *arguments, slot *results, char *error, size_t error_capacity)
{
    yozora_wasm_status status = call_function(context, function, arguments, results);
    if (status != YOZORA_WASM_OK && error && error_capacity)
        write_error(error, error_capacity, "%s", context->trap ? context->trap : "the host function failed");
    return status;
}
