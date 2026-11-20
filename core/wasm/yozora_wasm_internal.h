#ifndef YOZORA_WASM_INTERNAL_H
#define YOZORA_WASM_INTERNAL_H

#include "yozora_wasm.h"

/* the message writers take printf formats, the compiler checks the arguments */
#if defined(__GNUC__)
#define YW_PRINTF(format_index, first_argument) __attribute__((format(printf, format_index, first_argument)))
#else
#define YW_PRINTF(format_index, first_argument)
#endif

typedef struct {
    uint32_t nparams, nresults;
    uint8_t *params;
    uint8_t *results;
} yw_functype;

/* a constant expression of a global, an offset or an element: one instruction, the extended constants are not supported */
enum { YW_CONST_I32, YW_CONST_I64, YW_CONST_F32, YW_CONST_F64, YW_CONST_GLOBAL, YW_CONST_REF_NULL, YW_CONST_REF_FUNC };
typedef struct {
    uint8_t op;
    uint8_t type;
    uint64_t bits;
} yw_const;

typedef struct {
    uint8_t element_type;
    uint32_t minimum, maximum;
    uint8_t has_maximum;
} yw_tabletype;

typedef struct {
    uint32_t minimum, maximum;
    uint8_t has_maximum;
} yw_memtype;

typedef struct {
    uint8_t type;
    uint8_t is_mutable;
} yw_globaltype;

typedef struct {
    char *module;
    char *field;
    uint8_t kind;
    /* function: the type index; table, memory and global: the index among the imports of that kind */
    uint32_t type_index;
    yw_tabletype table;
    yw_memtype memory;
    yw_globaltype global;
} yw_import;

typedef struct {
    char *name;
    uint8_t kind;
    uint32_t index;
} yw_export;

enum { YW_SEGMENT_ACTIVE, YW_SEGMENT_PASSIVE, YW_SEGMENT_DECLARATIVE };

typedef struct {
    uint8_t mode;
    uint32_t table;
    yw_const offset;
    uint8_t element_type;
    uint32_t count;
    yw_const *items;
} yw_elemseg;

typedef struct {
    uint8_t mode;
    yw_const offset;
    const uint8_t *bytes;
    uint32_t size;
} yw_dataseg;

typedef struct {
    uint32_t *code;
    uint32_t code_length;
    /* parameters and locals together */
    uint32_t locals;
    uint32_t nparams, nresults;
    /* the most slots the function uses above its frame base, so a call checks the stack once */
    uint32_t max_stack;
} yw_funcdef;

typedef struct {
    char *name;
    const uint8_t *bytes;
    uint32_t size;
} yw_custom;

struct yozora_wasm_module {
    uint32_t refs;
    uint8_t *image;
    uint32_t image_size;

    yw_functype *types;
    uint32_t type_count;

    /* the function index space: imported functions first, then the defined ones */
    uint32_t *func_types;
    uint32_t func_count;
    uint32_t imported_funcs;
    yw_funcdef *defined;

    yw_tabletype *tables;
    uint32_t table_count;
    uint32_t imported_tables;

    yw_memtype *memories;
    uint32_t memory_count;
    uint32_t imported_memories;

    yw_globaltype *globals;
    yw_const *global_inits;
    uint32_t global_count;
    uint32_t imported_globals;

    yw_import *imports;
    uint32_t import_count;
    yw_export *exports;
    uint32_t export_count;

    int has_start;
    uint32_t start;

    yw_elemseg *elems;
    uint32_t elem_count;
    yw_dataseg *datas;
    uint32_t data_count;
    int has_data_count;

    yw_custom *customs;
    uint32_t custom_count;
};

/* the instructions of the compiled code, the numeric ones keep the opcode of the binary format */
enum {
    YW_OP_UNREACHABLE = 0x00,
    YW_OP_IF = 0x04,
    YW_OP_BR = 0x0c,
    YW_OP_BR_IF = 0x0d,
    YW_OP_BR_TABLE = 0x0e,
    YW_OP_RETURN = 0x0f,
    YW_OP_CALL = 0x10,
    YW_OP_CALL_INDIRECT = 0x11,
    YW_OP_DROP = 0x1a,
    YW_OP_SELECT = 0x1b,
    YW_OP_LOCAL_GET = 0x20,
    YW_OP_LOCAL_SET = 0x21,
    YW_OP_LOCAL_TEE = 0x22,
    YW_OP_GLOBAL_GET = 0x23,
    YW_OP_GLOBAL_SET = 0x24,
    YW_OP_TABLE_GET = 0x25,
    YW_OP_TABLE_SET = 0x26,
    YW_OP_MEMORY_SIZE = 0x3f,
    YW_OP_MEMORY_GROW = 0x40,
    YW_OP_I32_CONST = 0x41,
    YW_OP_I64_CONST = 0x42,
    YW_OP_F32_CONST = 0x43,
    YW_OP_F64_CONST = 0x44,
    YW_OP_REF_NULL = 0xd0,
    YW_OP_REF_IS_NULL = 0xd1,
    YW_OP_REF_FUNC = 0xd2,
    /* the instructions behind the 0xfc prefix are 0x100 and the number */
    YW_OP_PREFIX = 0x100,
    YW_OP_JMP = 0x200
};

void yw_functype_free(yw_functype *);

#endif
