#ifndef YOZORA_WASM_H
#define YOZORA_WASM_H

/* a WebAssembly engine for slices where JavaScriptCore has none (armv7): a decoder and validator that compile
   each function to a compact code array, and an interpreter over it. portable C99, no dependency, the
   embedder brings the JavaScript API. supported: the core of specification 2.0 without vectors, bulk memory,
   reference types, multiple results, mutable globals, several tables. refused with YOZORA_WASM_UNSUPPORTED:
   simd, threads and atomics, exceptions, gc types, tail calls, memory64, multiple memories, extended constant
   expressions */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOZORA_WASM_OK = 0,
    /* the bytes are not a module: bad magic, truncated, a number that does not fit */
    YOZORA_WASM_MALFORMED,
    /* a module that does not type check */
    YOZORA_WASM_INVALID,
    YOZORA_WASM_UNSUPPORTED,
    YOZORA_WASM_TOO_LARGE,
    YOZORA_WASM_NO_MEMORY,
    /* an import that is missing or of another type */
    YOZORA_WASM_LINK_ERROR,
    /* the program trapped, the message of the call says how */
    YOZORA_WASM_TRAP,
    /* a host function failed and the embedder keeps the reason: the call unwinds with this status */
    YOZORA_WASM_HOST_ERROR,
    /* the interrupt hook of the context asked to stop: the call unwinds and the embedder decides what the program sees */
    YOZORA_WASM_INTERRUPTED
} yozora_wasm_status;

/* the values of a slot: i32 and f32 in the low 32 bits, i64 and f64 in all 64, a reference as a handle (0 is null) */
typedef uint64_t yozora_wasm_slot;

enum {
    YOZORA_WASM_I32 = 0x7f,
    YOZORA_WASM_I64 = 0x7e,
    YOZORA_WASM_F32 = 0x7d,
    YOZORA_WASM_F64 = 0x7c,
    YOZORA_WASM_FUNCREF = 0x70,
    YOZORA_WASM_EXTERNREF = 0x6f
};

enum { YOZORA_WASM_KIND_FUNCTION = 0, YOZORA_WASM_KIND_TABLE = 1, YOZORA_WASM_KIND_MEMORY = 2, YOZORA_WASM_KIND_GLOBAL = 3 };

/* hard limits, all of them bounded so a hostile module cannot make the engine allocate without end */
#define YOZORA_WASM_MAX_MODULE_BYTES (64u * 1024u * 1024u)
#define YOZORA_WASM_MAX_PAGES 65536u
#define YOZORA_WASM_MAX_TABLE_ELEMENTS 10000000u
#define YOZORA_WASM_PAGE_SIZE 65536u
#define YOZORA_WASM_MAX_RESULTS 16u
#define YOZORA_WASM_MAX_PARAMS 1000u

typedef struct yozora_wasm_module yozora_wasm_module;
typedef struct yozora_wasm_instance yozora_wasm_instance;
typedef struct yozora_wasm_memory yozora_wasm_memory;
typedef struct yozora_wasm_table yozora_wasm_table;
typedef struct yozora_wasm_global yozora_wasm_global;
typedef struct yozora_wasm_func yozora_wasm_func;
typedef struct yozora_wasm_context yozora_wasm_context;

typedef struct {
    uint32_t nparams, nresults;
    const uint8_t *params;
    const uint8_t *results;
} yozora_wasm_functype;

typedef struct {
    uint8_t kind;
    /* a function import: the type; a global: value type and mutable; a table: element type; limits for tables and memories */
    yozora_wasm_functype function;
    uint8_t value_type;
    uint8_t mutable_global;
    uint32_t minimum;
    uint32_t maximum;
    uint8_t has_maximum;
} yozora_wasm_externtype;

/* a module is immutable: decoded, validated and compiled once. objects are reference counted, release frees
   at zero */
yozora_wasm_status yozora_wasm_module_create(const uint8_t *bytes, size_t size, yozora_wasm_module **out, char *error, size_t error_capacity);
/* the same decoding and validation without the module */
yozora_wasm_status yozora_wasm_validate(const uint8_t *bytes, size_t size, char *error, size_t error_capacity);
void yozora_wasm_module_retain(yozora_wasm_module *);
void yozora_wasm_module_release(yozora_wasm_module *);

uint32_t yozora_wasm_module_import_count(const yozora_wasm_module *);
/* the names are valid as long as the module is */
int yozora_wasm_module_import(const yozora_wasm_module *, uint32_t index, const char **module_name, const char **field_name, yozora_wasm_externtype *type);
uint32_t yozora_wasm_module_export_count(const yozora_wasm_module *);
int yozora_wasm_module_export(const yozora_wasm_module *, uint32_t index, const char **name, yozora_wasm_externtype *type);
/* the contents of the custom sections with this name, in order, 0 when there are no more */
int yozora_wasm_module_custom_section(const yozora_wasm_module *, const char *name, uint32_t index, const uint8_t **bytes, size_t *size);

/* the objects an instance shares with its embedder and with other instances */
yozora_wasm_memory *yozora_wasm_memory_create(uint32_t minimum, uint32_t maximum, int has_maximum);
void yozora_wasm_memory_retain(yozora_wasm_memory *);
void yozora_wasm_memory_release(yozora_wasm_memory *);
uint8_t *yozora_wasm_memory_data(yozora_wasm_memory *);
size_t yozora_wasm_memory_size(const yozora_wasm_memory *);
uint32_t yozora_wasm_memory_pages(const yozora_wasm_memory *);
/* counts the growths: a buffer the embedder made over the memory is stale when this changed */
uint32_t yozora_wasm_memory_generation(const yozora_wasm_memory *);
/* called just before a growth moves the bytes, from wasm or the embedder: drop views of the old bytes here,
   they are freed right after */
typedef void (*yozora_wasm_memory_observer)(void *user);
/* one observer for a memory: destroy runs when the memory is freed, on whatever thread drops the last reference */
void yozora_wasm_memory_set_observer(yozora_wasm_memory *, yozora_wasm_memory_observer before_grow, void (*destroy)(void *user), void *user);
void *yozora_wasm_memory_observer_user(const yozora_wasm_memory *);
/* returns the old size in pages, or -1 when the memory cannot grow */
int64_t yozora_wasm_memory_grow(yozora_wasm_memory *, uint32_t delta);

yozora_wasm_table *yozora_wasm_table_create(uint8_t element_type, uint32_t minimum, uint32_t maximum, int has_maximum, yozora_wasm_slot initial);
void yozora_wasm_table_retain(yozora_wasm_table *);
void yozora_wasm_table_release(yozora_wasm_table *);
uint32_t yozora_wasm_table_size(const yozora_wasm_table *);
uint8_t yozora_wasm_table_type(const yozora_wasm_table *);
/* a funcref is a retained function handle in the table, an externref the handle of the embedder; 0 is null */
int yozora_wasm_table_get(const yozora_wasm_table *, uint32_t index, yozora_wasm_slot *out);
int yozora_wasm_table_set(yozora_wasm_table *, uint32_t index, yozora_wasm_slot value);
int64_t yozora_wasm_table_grow(yozora_wasm_table *, uint32_t delta, yozora_wasm_slot initial);

yozora_wasm_global *yozora_wasm_global_create(uint8_t type, int is_mutable, yozora_wasm_slot value);
void yozora_wasm_global_retain(yozora_wasm_global *);
void yozora_wasm_global_release(yozora_wasm_global *);
yozora_wasm_slot yozora_wasm_global_get(const yozora_wasm_global *);
int yozora_wasm_global_set(yozora_wasm_global *, yozora_wasm_slot value);
uint8_t yozora_wasm_global_type(const yozora_wasm_global *);
int yozora_wasm_global_mutable(const yozora_wasm_global *);

/* a host function reads its arguments and writes its results as slots, a status other than OK unwinds the
   whole call and is returned by it */
typedef yozora_wasm_status (*yozora_wasm_host_function)(void *user, yozora_wasm_context *context, const yozora_wasm_slot *arguments, yozora_wasm_slot *results);
yozora_wasm_func *yozora_wasm_func_create_host(uint32_t nparams, const uint8_t *params, uint32_t nresults, const uint8_t *results, yozora_wasm_host_function function,
    void *user, void (*finalize)(void *user));
void yozora_wasm_func_retain(yozora_wasm_func *);
void yozora_wasm_func_release(yozora_wasm_func *);
yozora_wasm_functype yozora_wasm_func_type(const yozora_wasm_func *);
/* the instance a function of a module belongs to, null for a function of the host. and its index in the function space */
yozora_wasm_instance *yozora_wasm_func_instance(const yozora_wasm_func *);
uint32_t yozora_wasm_func_index(const yozora_wasm_func *);
void *yozora_wasm_func_user(const yozora_wasm_func *);

typedef struct {
    uint8_t kind;
    union {
        yozora_wasm_func *function;
        yozora_wasm_table *table;
        yozora_wasm_memory *memory;
        yozora_wasm_global *global;
    } object;
} yozora_wasm_extern;

/* an instance takes a reference to each import. the start function runs here and a trap ends in
   YOZORA_WASM_TRAP or the status of the host */
yozora_wasm_status yozora_wasm_instantiate(yozora_wasm_context *context, yozora_wasm_module *module, const yozora_wasm_extern *imports, uint32_t import_count,
    yozora_wasm_instance **out, char *error, size_t error_capacity);
void yozora_wasm_instance_retain(yozora_wasm_instance *);
void yozora_wasm_instance_release(yozora_wasm_instance *);
/* the exports as objects, each returned with a reference of its own for the caller */
int yozora_wasm_instance_export(yozora_wasm_instance *, uint32_t index, yozora_wasm_extern *out);

/* a context owns the stacks the interpreter runs on, one per thread that runs wasm. a host function may call
   back into wasm with the same context */
yozora_wasm_context *yozora_wasm_context_create(void);
void yozora_wasm_context_destroy(yozora_wasm_context *);
/* the call depth and value stack of a context, set before the first call */
void yozora_wasm_context_set_limits(yozora_wasm_context *, uint32_t max_depth, uint32_t stack_slots);
/* the interpreter asks every 65536 branches and calls, a program that never calls out would hold the thread
   for good. a nonzero answer ends the call with YOZORA_WASM_INTERRUPTED. the hook runs on the calling thread */
typedef int (*yozora_wasm_interrupt)(void *user);
void yozora_wasm_context_set_interrupt(yozora_wasm_context *, yozora_wasm_interrupt hook, void *user);

/* calls a function: arguments and results are slots in the order of its type. a trap leaves its reason in error */
yozora_wasm_status yozora_wasm_call(yozora_wasm_context *, yozora_wasm_func *function, const yozora_wasm_slot *arguments, yozora_wasm_slot *results, char *error,
    size_t error_capacity);

#ifdef __cplusplus
}
#endif
#endif
