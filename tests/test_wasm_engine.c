/* what the JavaScript API leans on in the core/wasm engine: the interrupt hook that ends a loop with no
   calls, the observer that hears of a memory growth before the bytes move (and of a growth by nothing), and
   its destroy when the memory goes */
#include "../core/wasm/yozora_wasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

/* (func (export "spin") (loop (br 0))) */
static const uint8_t spin_module[] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x07, 0x08, 0x01,
    0x04, 's', 'p', 'i', 'n', 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b };

/* (memory (export "mem") 1 3) (func (export "grow") (param i32) (result i32) (memory.grow (local.get 0))) */
static const uint8_t grow_module[] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x05,
    0x04, 0x01, 0x01, 0x01, 0x03, 0x07, 0x0e, 0x02, 0x04, 'g', 'r', 'o', 'w', 0x00, 0x00, 0x03, 'm', 'e', 'm', 0x02, 0x00, 0x0a, 0x08, 0x01, 0x06, 0x00, 0x20, 0x00,
    0x40, 0x00, 0x0b };

static int polls;

static int interrupt_after_three(void *user)
{
    (void)user;
    return ++polls >= 3;
}

static int grows, destroyed;

static void before_grow(void *user)
{
    CHECK(user == &grows);
    ++grows;
}

static void destroy(void *user)
{
    CHECK(user == &grows);
    ++destroyed;
}

static yozora_wasm_func *export_function(yozora_wasm_module *module, yozora_wasm_instance *instance, const char *wanted)
{
    uint32_t i;
    for (i = 0; i < yozora_wasm_module_export_count(module); ++i) {
        const char *name;
        yozora_wasm_externtype type;
        yozora_wasm_extern object;
        CHECK(yozora_wasm_module_export(module, i, &name, &type));
        if (type.kind == YOZORA_WASM_KIND_FUNCTION && !strcmp(name, wanted)) {
            CHECK(yozora_wasm_instance_export(instance, i, &object));
            return object.object.function;
        }
    }
    return NULL;
}

static void test_interrupt(void)
{
    yozora_wasm_module *module;
    yozora_wasm_instance *instance;
    yozora_wasm_context *context = yozora_wasm_context_create();
    yozora_wasm_func *spin;
    char error[128];
    yozora_wasm_status status;
    CHECK(yozora_wasm_module_create(spin_module, sizeof(spin_module), &module, error, sizeof(error)) == YOZORA_WASM_OK);
    CHECK(yozora_wasm_instantiate(context, module, NULL, 0, &instance, error, sizeof(error)) == YOZORA_WASM_OK);
    spin = export_function(module, instance, "spin");
    CHECK(spin);
    yozora_wasm_context_set_interrupt(context, interrupt_after_three, NULL);
    status = yozora_wasm_call(context, spin, NULL, NULL, error, sizeof(error));
    CHECK(status == YOZORA_WASM_INTERRUPTED);
    CHECK(polls == 3);
    CHECK(strstr(error, "terminated"));
    /* the hook asks every 65536 branches, so three polls took at least 196608 of them */
    yozora_wasm_context_set_interrupt(context, NULL, NULL);
    yozora_wasm_func_release(spin);
    yozora_wasm_instance_release(instance);
    yozora_wasm_module_release(module);
    yozora_wasm_context_destroy(context);
}

static void test_memory_observer(void)
{
    yozora_wasm_module *module;
    yozora_wasm_instance *instance;
    yozora_wasm_context *context = yozora_wasm_context_create();
    yozora_wasm_func *grow;
    yozora_wasm_extern memory_export;
    yozora_wasm_slot argument, result;
    char error[128];
    uint32_t i;
    CHECK(yozora_wasm_module_create(grow_module, sizeof(grow_module), &module, error, sizeof(error)) == YOZORA_WASM_OK);
    CHECK(yozora_wasm_instantiate(context, module, NULL, 0, &instance, error, sizeof(error)) == YOZORA_WASM_OK);
    grow = export_function(module, instance, "grow");
    CHECK(grow);
    for (i = 0; i < yozora_wasm_module_export_count(module); ++i) {
        const char *name;
        yozora_wasm_externtype type;
        yozora_wasm_module_export(module, i, &name, &type);
        if (type.kind == YOZORA_WASM_KIND_MEMORY)
            CHECK(yozora_wasm_instance_export(instance, i, &memory_export));
    }
    CHECK(memory_export.kind == YOZORA_WASM_KIND_MEMORY);
    yozora_wasm_memory_set_observer(memory_export.object.memory, before_grow, destroy, &grows);
    CHECK(yozora_wasm_memory_observer_user(memory_export.object.memory) == &grows);

    argument = 1;
    CHECK(yozora_wasm_call(context, grow, &argument, &result, error, sizeof(error)) == YOZORA_WASM_OK);
    CHECK((uint32_t)result == 1 && grows == 1);
    CHECK(yozora_wasm_memory_pages(memory_export.object.memory) == 2);
    /* a growth by nothing still tells the observer: a page sees a new buffer for it */
    argument = 0;
    CHECK(yozora_wasm_call(context, grow, &argument, &result, error, sizeof(error)) == YOZORA_WASM_OK);
    CHECK((uint32_t)result == 2 && grows == 2);
    /* a growth past the maximum fails before the bytes would move, so the observer is not asked */
    argument = 5;
    CHECK(yozora_wasm_call(context, grow, &argument, &result, error, sizeof(error)) == YOZORA_WASM_OK);
    CHECK((uint32_t)result == 0xffffffffu && grows == 2);
    CHECK(yozora_wasm_memory_grow(memory_export.object.memory, 1) == 2 && grows == 3);

    CHECK(destroyed == 0);
    yozora_wasm_func_release(grow);
    yozora_wasm_instance_release(instance);
    CHECK(destroyed == 0);
    yozora_wasm_memory_release(memory_export.object.memory);
    CHECK(destroyed == 1);
    yozora_wasm_module_release(module);
    yozora_wasm_context_destroy(context);
}

/* the engine keeps no cap of its own below the 4 GiB of the specification: a growth past the old 128 MB works when the system has the room */
static void test_memory_size(void)
{
    yozora_wasm_memory *memory = yozora_wasm_memory_create(1, 0, 0);
    yozora_wasm_memory *bounded = yozora_wasm_memory_create(1, 4, 1);
    CHECK(memory != NULL && bounded != NULL);
    if (!memory || !bounded)
        return;
    CHECK(yozora_wasm_memory_grow(memory, 2100) == 1);
    CHECK(yozora_wasm_memory_pages(memory) == 2101);
    CHECK(yozora_wasm_memory_data(memory)[(size_t)2100 * 65536] == 0);
    /* the page count of the specification is the one limit, a count that would wrap a 32 bit size is refused as well */
    CHECK(yozora_wasm_memory_grow(memory, 65536) == -1);
    CHECK(yozora_wasm_memory_pages(memory) == 2101);
    CHECK(yozora_wasm_memory_grow(bounded, 4) == -1);
    CHECK(yozora_wasm_memory_grow(bounded, 3) == 1);
    CHECK(yozora_wasm_memory_create(YOZORA_WASM_MAX_PAGES + 1, 0, 0) == NULL);
    yozora_wasm_memory_release(memory);
    yozora_wasm_memory_release(bounded);
}

int main(void)
{
    test_interrupt();
    test_memory_observer();
    test_memory_size();
    puts("wasm engine: interrupt hook, memory observer and memory size pass");
    return 0;
}
