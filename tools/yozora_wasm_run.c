/* runs the exports of a wasm module from stdin lines, for tests that compare the engine with another one. a
   line is "name arg arg ..." with arguments i32:N, i64:N, f32:0xBITS or f64:0xBITS, the answer is one line of
   the same shapes, "trap" or "error text". imports follow what the module asks for: a function returns zeros,
   except an i32 result after an i32 parameter is twice it plus one */

#include "../core/wasm/yozora_wasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static yozora_wasm_status stub(void *user, yozora_wasm_context *context, const yozora_wasm_slot *arguments, yozora_wasm_slot *results)
{
    const yozora_wasm_functype *type = (const yozora_wasm_functype *)user;
    uint32_t i;
    (void)context;
    for (i = 0; i < type->nresults; ++i)
        results[i] = 0;
    if (type->nresults && type->results[0] == YOZORA_WASM_I32 && type->nparams && type->params[0] == YOZORA_WASM_I32)
        results[0] = (uint32_t)((uint32_t)arguments[0] * 2u + 1u);
    return YOZORA_WASM_OK;
}

static int parse_value(const char *text, uint8_t type, yozora_wasm_slot *out)
{
    const char *colon = strchr(text, ':');
    char *end;
    unsigned long long v;
    if (!colon)
        return 0;
    v = strtoull(colon + 1, &end, 0);
    if (*colon == 0 || (*end && *end != '\n'))
        return 0;
    if (colon[1] == '-') {
        long long signed_value = strtoll(colon + 1, &end, 0);
        v = (unsigned long long)signed_value;
    }
    *out = type == YOZORA_WASM_I32 || type == YOZORA_WASM_F32 ? (uint32_t)v : (uint64_t)v;
    return 1;
}

int main(int argc, char **argv)
{
    FILE *file;
    uint8_t *bytes;
    long size;
    yozora_wasm_module *module;
    yozora_wasm_instance *instance;
    yozora_wasm_context *context;
    yozora_wasm_extern *imports;
    uint32_t i, count;
    char error[256], line[4096];
    yozora_wasm_status status;

    if (argc != 2) {
        fputs("usage: yozora-wasm-run module.wasm < cases\n", stderr);
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 2;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    bytes = (uint8_t *)malloc((size_t)size + 1);
    if (!bytes || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        fputs("cannot read the module\n", stderr);
        return 2;
    }
    fclose(file);
    status = yozora_wasm_module_create(bytes, (size_t)size, &module, error, sizeof(error));
    if (status != YOZORA_WASM_OK) {
        printf("invalid: %s\n", error);
        return 1;
    }
    context = yozora_wasm_context_create();
    count = yozora_wasm_module_import_count(module);
    imports = (yozora_wasm_extern *)calloc(count ? count : 1, sizeof(*imports));
    for (i = 0; i < count; ++i) {
        const char *mod, *field;
        yozora_wasm_externtype t;
        yozora_wasm_module_import(module, i, &mod, &field, &t);
        imports[i].kind = t.kind;
        switch (t.kind) {
        case YOZORA_WASM_KIND_FUNCTION: {
            /* the type outlives the run: the module keeps it */
            imports[i].object.function = yozora_wasm_func_create_host(t.function.nparams, t.function.params, t.function.nresults, t.function.results, stub,
                (void *)&t.function, NULL);
            {
                static yozora_wasm_functype keep[64];
                static unsigned used;
                if (used < 64) {
                    keep[used] = t.function;
                    yozora_wasm_func_release(imports[i].object.function);
                    imports[i].object.function = yozora_wasm_func_create_host(t.function.nparams, t.function.params, t.function.nresults, t.function.results, stub,
                        &keep[used], NULL);
                    ++used;
                }
            }
            break;
        }
        case YOZORA_WASM_KIND_TABLE:
            imports[i].object.table = yozora_wasm_table_create(t.value_type, t.minimum, t.maximum, t.has_maximum, 0);
            break;
        case YOZORA_WASM_KIND_MEMORY:
            imports[i].object.memory = yozora_wasm_memory_create(t.minimum, t.maximum, t.has_maximum);
            break;
        default:
            imports[i].object.global = yozora_wasm_global_create(t.value_type, t.mutable_global, 0);
            break;
        }
    }
    status = yozora_wasm_instantiate(context, module, imports, count, &instance, error, sizeof(error));
    if (status != YOZORA_WASM_OK) {
        printf("%s: %s\n", status == YOZORA_WASM_TRAP ? "trap" : "link", error);
        return 1;
    }
    while (fgets(line, sizeof(line), stdin)) {
        char *name = strtok(line, " \n");
        char *token;
        yozora_wasm_slot arguments[YOZORA_WASM_MAX_PARAMS];
        yozora_wasm_slot results[YOZORA_WASM_MAX_RESULTS];
        yozora_wasm_func *function = NULL;
        uint32_t k, given = 0;
        if (!name)
            continue;
        for (k = 0; k < yozora_wasm_module_export_count(module); ++k) {
            const char *export_name;
            yozora_wasm_externtype t;
            yozora_wasm_module_export(module, k, &export_name, &t);
            if (t.kind == YOZORA_WASM_KIND_FUNCTION && strcmp(export_name, name) == 0) {
                yozora_wasm_extern e;
                yozora_wasm_instance_export(instance, k, &e);
                function = e.object.function;
                break;
            }
        }
        if (!function) {
            puts("error: no such export");
            continue;
        }
        {
            yozora_wasm_functype type = yozora_wasm_func_type(function);
            int bad = 0;
            while ((token = strtok(NULL, " \n")) != NULL && given < type.nparams) {
                if (!parse_value(token, type.params[given], &arguments[given])) {
                    bad = 1;
                    break;
                }
                ++given;
            }
            if (bad || given != type.nparams) {
                puts("error: bad arguments");
                yozora_wasm_func_release(function);
                continue;
            }
            status = yozora_wasm_call(context, function, arguments, results, error, sizeof(error));
            if (status != YOZORA_WASM_OK) {
                printf("trap %s\n", error);
            } else {
                for (k = 0; k < type.nresults; ++k) {
                    switch (type.results[k]) {
                    case YOZORA_WASM_I32: printf("%si32:0x%x", k ? " " : "", (unsigned)results[k]); break;
                    case YOZORA_WASM_I64: printf("%si64:0x%llx", k ? " " : "", (unsigned long long)results[k]); break;
                    case YOZORA_WASM_F32: printf("%sf32:0x%x", k ? " " : "", (unsigned)results[k]); break;
                    case YOZORA_WASM_F64: printf("%sf64:0x%llx", k ? " " : "", (unsigned long long)results[k]); break;
                    default: printf("%sref:%s", k ? " " : "", results[k] ? "set" : "null"); break;
                    }
                }
                putchar('\n');
            }
            yozora_wasm_func_release(function);
        }
    }
    yozora_wasm_instance_release(instance);
    yozora_wasm_module_release(module);
    yozora_wasm_context_destroy(context);
    free(imports);
    free(bytes);
    return 0;
}
