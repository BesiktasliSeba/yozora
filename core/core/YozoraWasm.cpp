#include "config.h"
#include "YozoraWasm.h"
#include "yozora_wasm.h"
#include "darwin/yozora_tls_slot.h"
#include "JSDOMConvertBufferSource.h"
#include "JSDOMGlobalObject.h"
#include <JavaScriptCore/APICast.h>
#include <JavaScriptCore/ArrayBuffer.h>
#include <JavaScriptCore/JSGlobalObject.h>
#include <JavaScriptCore/JSLock.h>
#include <JavaScriptCore/JavaScript.h>
#include <JavaScriptCore/JSTypedArray.h>
#include <JavaScriptCore/Protect.h>
#include <JavaScriptCore/VM.h>
#include <JavaScriptCore/VMTraps.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

// the page side of WebAssembly: the API objects in core/core/yozora_wasm_api.js call these. what crosses is
// flat: an i64 is two numbers (low then high 32 bits), a function reference is a handle or null, an external
// reference is the value itself
static const char wasm_api_source[] =
#include "yozora_wasm_api_script.inc"
;

namespace {

enum HandleKind : uint8_t { KindModule, KindInstance, KindMemory, KindTable, KindGlobal, KindFunc };

struct Handle {
    uint8_t kind;
    void* object;
};

// a memory shows pages as an ArrayBuffer and the engine moves the bytes when it grows, so the view drops the
// buffer first and a script never reads freed bytes
struct MemoryView {
    JSC::ArrayBuffer* buffer { nullptr };
    void* bytes { nullptr };
    JSC::VM* vm { nullptr };
};

struct Host {
    JSC::JSValue function;
    std::vector<uint8_t> params;
    std::vector<uint8_t> results;
};

constexpr uint32_t max_extern_references = 1u << 20;

// everything that belongs to the script thread: the engine stacks, the call in progress, what a host function
// threw
struct ThreadState {
    yozora_wasm_context* context { nullptr };
    JSContextRef current { nullptr };
    JSC::JSValue pending;
    bool has_pending { false };
    std::vector<JSC::JSValue> externs;
    std::unordered_map<JSC::JSCell*, uint32_t> extern_ids;
    std::vector<JSC::JSValue> to_unprotect;

    ~ThreadState()
    {
        if (context)
            yozora_wasm_context_destroy(context);
    }
};

// ios 5 has no C++ thread-local storage, the slot is a pthread key that deletes the state when the thread ends
ThreadState& thread_state()
{
    static YozoraThreadLocalSlot slot([](void* state) { delete static_cast<ThreadState*>(state); });
    auto* state = static_cast<ThreadState*>(slot.get());
    if (!state) {
        state = new ThreadState;
        slot.set(state);
    }
    return *state;
}

// a handle's finalizer runs inside the collector, where a value may not be unprotected, so the list is
// drained at the next call from a script
void drain_unprotect(ThreadState& state)
{
    for (auto value : state.to_unprotect)
        JSC::gcUnprotect(value);
    state.to_unprotect.clear();
}

void finalize_handle(JSObjectRef object)
{
    auto* handle = static_cast<Handle*>(JSObjectGetPrivate(object));
    if (!handle)
        return;
    switch (handle->kind) {
    case KindModule:
        yozora_wasm_module_release(static_cast<yozora_wasm_module*>(handle->object));
        break;
    case KindInstance:
        yozora_wasm_instance_release(static_cast<yozora_wasm_instance*>(handle->object));
        break;
    case KindMemory:
        yozora_wasm_memory_release(static_cast<yozora_wasm_memory*>(handle->object));
        break;
    case KindTable:
        yozora_wasm_table_release(static_cast<yozora_wasm_table*>(handle->object));
        break;
    case KindGlobal:
        yozora_wasm_global_release(static_cast<yozora_wasm_global*>(handle->object));
        break;
    default:
        yozora_wasm_func_release(static_cast<yozora_wasm_func*>(handle->object));
        break;
    }
    delete handle;
}

JSClassRef handle_class()
{
    static JSClassRef cls = [] {
        JSClassDefinition definition = kJSClassDefinitionEmpty;
        definition.className = "WebAssemblyHandle";
        definition.finalize = finalize_handle;
        return JSClassCreate(&definition);
    }();
    return cls;
}

// an engine failure thrown as an object of its own so the API code can tell it from anything a page throws
// and turn it into the named error class
JSClassRef status_class()
{
    static JSClassRef cls = [] {
        JSClassDefinition definition = kJSClassDefinitionEmpty;
        definition.className = "WebAssemblyStatus";
        return JSClassCreate(&definition);
    }();
    return cls;
}

JSValueRef make_string(JSContextRef ctx, const char* text)
{
    JSStringRef string = JSStringCreateWithUTF8CString(text);
    JSValueRef value = JSValueMakeString(ctx, string);
    JSStringRelease(string);
    return value;
}

void set_property(JSContextRef ctx, JSObjectRef object, const char* name, JSValueRef value)
{
    JSStringRef key = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(ctx, object, key, value, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(key);
}

JSValueRef raise_status(JSContextRef ctx, JSValueRef* exception, const char* kind, const char* message)
{
    JSObjectRef status = JSObjectMake(ctx, status_class(), nullptr);
    set_property(ctx, status, "kind", make_string(ctx, kind));
    set_property(ctx, status, "message", make_string(ctx, message));
    *exception = status;
    return nullptr;
}

JSValueRef raise_engine(JSContextRef ctx, JSValueRef* exception, yozora_wasm_status status, const char* message)
{
    const char* kind = "runtime";
    switch (status) {
    case YOZORA_WASM_MALFORMED:
    case YOZORA_WASM_INVALID:
    case YOZORA_WASM_UNSUPPORTED:
    case YOZORA_WASM_TOO_LARGE:
        kind = "compile";
        break;
    case YOZORA_WASM_LINK_ERROR:
        kind = "link";
        break;
    case YOZORA_WASM_NO_MEMORY:
        kind = "range";
        break;
    case YOZORA_WASM_TRAP:
        // a script sees the stack limit as a RangeError, the way JavaScriptCore reports its own
        kind = std::strcmp(message, "Maximum call stack size exceeded") ? "runtime" : "range";
        break;
    default:
        break;
    }
    return raise_status(ctx, exception, kind, message);
}

JSObjectRef make_handle(JSContextRef ctx, HandleKind kind, void* object)
{
    return JSObjectMake(ctx, handle_class(), new Handle { kind, object });
}

Handle* handle_of(JSContextRef ctx, JSValueRef value, HandleKind kind)
{
    if (!value || !JSValueIsObjectOfClass(ctx, value, handle_class()))
        return nullptr;
    auto* handle = static_cast<Handle*>(JSObjectGetPrivate(JSValueToObject(ctx, value, nullptr)));
    return handle && handle->kind == kind ? handle : nullptr;
}

// a function reference crossing out keeps the function alive for the handle
JSValueRef function_handle(JSContextRef ctx, yozora_wasm_func* function)
{
    if (!function)
        return JSValueMakeNull(ctx);
    yozora_wasm_func_retain(function);
    return make_handle(ctx, KindFunc, function);
}

char type_letter(uint8_t type)
{
    switch (type) {
    case YOZORA_WASM_I32: return 'i';
    case YOZORA_WASM_I64: return 'l';
    case YOZORA_WASM_F32: return 'f';
    case YOZORA_WASM_F64: return 'd';
    case YOZORA_WASM_FUNCREF: return 'F';
    default: return 'E';
    }
}

uint8_t letter_type(char letter)
{
    switch (letter) {
    case 'i': return YOZORA_WASM_I32;
    case 'l': return YOZORA_WASM_I64;
    case 'f': return YOZORA_WASM_F32;
    case 'd': return YOZORA_WASM_F64;
    case 'F': return YOZORA_WASM_FUNCREF;
    default: return YOZORA_WASM_EXTERNREF;
    }
}

std::string letters(const uint8_t* types, uint32_t count)
{
    std::string text;
    for (uint32_t i = 0; i < count; ++i)
        text.push_back(type_letter(types[i]));
    return text;
}

// an array that grows as values are put in, so each value is reachable from the array before the next one is made
struct Output {
    JSContextRef ctx;
    JSObjectRef array;
    unsigned length { 0 };

    explicit Output(JSContextRef context)
        : ctx(context)
        , array(JSObjectMakeArray(context, 0, nullptr, nullptr))
    {
    }
    void push(JSValueRef value) { JSObjectSetPropertyAtIndex(ctx, array, length++, value, nullptr); }
    void push_number(double value) { push(JSValueMakeNumber(ctx, value)); }
    void push_string(const std::string& text) { push(make_string(ctx, text.c_str())); }
};

uint32_t intern_extern(JSContextRef ctx, JSValueRef value, JSValueRef* exception)
{
    if (JSValueIsNull(ctx, value))
        return 0;
    auto& state = thread_state();
    JSC::JSGlobalObject* global = ::toJS(ctx);
    JSC::JSValue js = ::toJS(global, value);
    JSC::JSCell* cell = js.isCell() ? js.asCell() : nullptr;
    if (cell && cell->isObject()) {
        auto found = state.extern_ids.find(cell);
        if (found != state.extern_ids.end())
            return found->second;
    }
    if (state.externs.size() >= max_extern_references) {
        raise_status(ctx, exception, "range", "too many external references");
        return 0;
    }
    // the engine counts no references of this kind, so a value that went in stays alive for the thread,
    // bounded by the cap, objects once each
    JSC::gcProtect(js);
    state.externs.push_back(js);
    uint32_t id = static_cast<uint32_t>(state.externs.size());
    if (cell && cell->isObject())
        state.extern_ids[cell] = id;
    return id;
}

JSValueRef extern_value(JSContextRef ctx, uint32_t id)
{
    auto& state = thread_state();
    if (!id || id > state.externs.size())
        return JSValueMakeNull(ctx);
    return ::toRef(::toJS(ctx), state.externs[id - 1]);
}

void put_slot(JSContextRef ctx, Output& out, uint8_t type, yozora_wasm_slot slot)
{
    switch (type) {
    case YOZORA_WASM_I32:
        out.push_number(static_cast<int32_t>(slot));
        break;
    case YOZORA_WASM_I64:
        out.push_number(static_cast<uint32_t>(slot));
        out.push_number(static_cast<uint32_t>(slot >> 32));
        break;
    case YOZORA_WASM_F32: {
        uint32_t bits = static_cast<uint32_t>(slot);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        out.push_number(value);
        break;
    }
    case YOZORA_WASM_F64: {
        double value;
        std::memcpy(&value, &slot, sizeof(value));
        out.push_number(value);
        break;
    }
    case YOZORA_WASM_FUNCREF:
        out.push(function_handle(ctx, reinterpret_cast<yozora_wasm_func*>(static_cast<uintptr_t>(slot))));
        break;
    default:
        out.push(slot ? extern_value(ctx, static_cast<uint32_t>(slot)) : JSValueMakeNull(ctx));
        break;
    }
}

struct Cursor {
    JSContextRef ctx;
    JSObjectRef array;
    unsigned index { 0 };
    unsigned length;

    Cursor(JSContextRef context, JSObjectRef source)
        : ctx(context)
        , array(source)
        , length(0)
    {
        JSStringRef key = JSStringCreateWithUTF8CString("length");
        double count = JSValueToNumber(ctx, JSObjectGetProperty(ctx, source, key, nullptr), nullptr);
        JSStringRelease(key);
        length = count > 0 && count < 4294967295.0 ? static_cast<unsigned>(count) : 0;
    }
    bool has(unsigned count) const { return index + count <= length; }
    JSValueRef next() { return JSObjectGetPropertyAtIndex(ctx, array, index++, nullptr); }
};

bool get_slot(Cursor& in, uint8_t type, yozora_wasm_slot& slot, JSValueRef* exception)
{
    JSContextRef ctx = in.ctx;
    if (!in.has(type == YOZORA_WASM_I64 ? 2 : 1)) {
        raise_status(ctx, exception, "type", "too few values");
        return false;
    }
    switch (type) {
    case YOZORA_WASM_I32:
        slot = static_cast<uint32_t>(static_cast<int64_t>(JSValueToNumber(ctx, in.next(), nullptr)));
        return true;
    case YOZORA_WASM_I64: {
        uint64_t low = static_cast<uint32_t>(static_cast<int64_t>(JSValueToNumber(ctx, in.next(), nullptr)));
        uint64_t high = static_cast<uint32_t>(static_cast<int64_t>(JSValueToNumber(ctx, in.next(), nullptr)));
        slot = low | (high << 32);
        return true;
    }
    case YOZORA_WASM_F32: {
        float value = static_cast<float>(JSValueToNumber(ctx, in.next(), nullptr));
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        slot = bits;
        return true;
    }
    case YOZORA_WASM_F64: {
        double value = JSValueToNumber(ctx, in.next(), nullptr);
        std::memcpy(&slot, &value, sizeof(value));
        return true;
    }
    case YOZORA_WASM_FUNCREF: {
        JSValueRef value = in.next();
        if (JSValueIsNull(ctx, value)) {
            slot = 0;
            return true;
        }
        Handle* handle = handle_of(ctx, value, KindFunc);
        if (!handle) {
            raise_status(ctx, exception, "type", "a function reference is null or an exported WebAssembly function");
            return false;
        }
        slot = static_cast<yozora_wasm_slot>(reinterpret_cast<uintptr_t>(handle->object));
        return true;
    }
    default: {
        JSValueRef error = nullptr;
        uint32_t id = intern_extern(ctx, in.next(), &error);
        if (error) {
            *exception = error;
            return false;
        }
        slot = id;
        return true;
    }
    }
}

int interrupt_hook(void* user)
{
    auto* vm = static_cast<JSC::VM*>(user);
    return vm->hasTerminationRequest() || vm->traps().needHandling(JSC::VMTraps::NeedTermination);
}

// every script call into the engine goes through one of these: it makes the thread context, notes which
// context host functions call back into and hooks the interrupt to the VM, so a script the core stopped
// (yozora_stop_scripts) stops a loop inside WebAssembly too
struct Entry {
    ThreadState& state;
    JSContextRef previous;
    bool ok;

    explicit Entry(JSContextRef ctx)
        : state(thread_state())
        , previous(state.current)
    {
        drain_unprotect(state);
        if (!state.context)
            state.context = yozora_wasm_context_create();
        ok = state.context != nullptr;
        if (!ok)
            return;
        state.current = ctx;
        yozora_wasm_context_set_interrupt(state.context, interrupt_hook, &::toJS(ctx)->vm());
    }
    ~Entry() { state.current = previous; }
};

void keep_pending(JSContextRef ctx, JSValueRef exception)
{
    auto& state = thread_state();
    JSC::JSValue value = ::toJS(::toJS(ctx), exception);
    JSC::gcProtect(value);
    if (state.has_pending)
        JSC::gcUnprotect(state.pending);
    state.pending = value;
    state.has_pending = true;
}

JSValueRef release_pending(JSContextRef ctx, JSValueRef* exception)
{
    auto& state = thread_state();
    if (!state.has_pending)
        return raise_status(ctx, exception, "runtime", "a host function failed");
    *exception = ::toRef(::toJS(ctx), state.pending);
    JSC::gcUnprotect(state.pending);
    state.has_pending = false;
    return nullptr;
}

yozora_wasm_status host_call(void* user, yozora_wasm_context*, const yozora_wasm_slot* arguments, yozora_wasm_slot* results)
{
    auto* host = static_cast<Host*>(user);
    JSContextRef ctx = thread_state().current;
    if (!ctx)
        return YOZORA_WASM_HOST_ERROR;
    JSValueRef exception = nullptr;
    Output out(ctx);
    for (size_t i = 0; i < host->params.size(); ++i)
        put_slot(ctx, out, host->params[i], arguments[i]);
    JSValueRef argument = out.array;
    JSObjectRef function = JSValueToObject(ctx, ::toRef(::toJS(ctx), host->function), nullptr);
    JSValueRef answer = JSObjectCallAsFunction(ctx, function, nullptr, 1, &argument, &exception);
    if (!exception) {
        JSObjectRef array = answer ? JSValueToObject(ctx, answer, nullptr) : nullptr;
        if (!array && !host->results.empty()) {
            raise_status(ctx, &exception, "type", "a host function answers with an array");
        } else if (array) {
            Cursor cursor(ctx, array);
            size_t slots = 0;
            for (uint8_t type : host->results) {
                if (!get_slot(cursor, type, results[slots], &exception))
                    break;
                ++slots;
            }
        }
    }
    if (exception) {
        keep_pending(ctx, exception);
        return YOZORA_WASM_HOST_ERROR;
    }
    return YOZORA_WASM_OK;
}

void host_finalize(void* user)
{
    auto* host = static_cast<Host*>(user);
    thread_state().to_unprotect.push_back(host->function);
    delete host;
}

// the answer of a call: the results as flat values, or the exception to throw
JSValueRef finish_call(JSContextRef ctx, JSValueRef* exception, yozora_wasm_status status, const char* error, const uint8_t* types, uint32_t count,
    const yozora_wasm_slot* results)
{
    if (status == YOZORA_WASM_HOST_ERROR)
        return release_pending(ctx, exception);
    if (status != YOZORA_WASM_OK)
        return raise_engine(ctx, exception, status, error);
    Output out(ctx);
    for (uint32_t i = 0; i < count; ++i)
        put_slot(ctx, out, types[i], results[i]);
    return out.array;
}

bool source_bytes(JSContextRef ctx, JSValueRef value, const uint8_t*& bytes, size_t& size)
{
    if (!value || !JSValueIsObject(ctx, value))
        return false;
    JSObjectRef object = JSValueToObject(ctx, value, nullptr);
    switch (JSValueGetTypedArrayType(ctx, value, nullptr)) {
    case kJSTypedArrayTypeArrayBuffer:
        bytes = static_cast<const uint8_t*>(JSObjectGetArrayBufferBytesPtr(ctx, object, nullptr));
        size = JSObjectGetArrayBufferByteLength(ctx, object, nullptr);
        return bytes || !size;
    case kJSTypedArrayTypeNone:
        return false;
    default:
        bytes = static_cast<const uint8_t*>(JSObjectGetTypedArrayBytesPtr(ctx, object, nullptr));
        size = JSObjectGetTypedArrayByteLength(ctx, object, nullptr);
        return bytes || !size;
    }
}

double number_argument(JSContextRef ctx, size_t count, const JSValueRef arguments[], size_t index)
{
    return index < count ? JSValueToNumber(ctx, arguments[index], nullptr) : 0;
}

#define NATIVE(name) JSValueRef name(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef* exception)

NATIVE(native_is_status)
{
    return JSValueMakeBoolean(ctx, argc && JSValueIsObjectOfClass(ctx, argv[0], status_class()));
}

NATIVE(native_validate)
{
    const uint8_t* bytes;
    size_t size;
    char error[256];
    if (argc < 1 || !source_bytes(ctx, argv[0], bytes, size))
        return raise_status(ctx, exception, "type", "the argument must be an ArrayBuffer or a view of one");
    return JSValueMakeBoolean(ctx, yozora_wasm_validate(bytes, size, error, sizeof(error)) == YOZORA_WASM_OK);
}

NATIVE(native_module_create)
{
    const uint8_t* bytes;
    size_t size;
    char error[256] = "";
    yozora_wasm_module* module = nullptr;
    if (argc < 1 || !source_bytes(ctx, argv[0], bytes, size))
        return raise_status(ctx, exception, "type", "the argument must be an ArrayBuffer or a view of one");
    auto status = yozora_wasm_module_create(bytes, size, &module, error, sizeof(error));
    if (status != YOZORA_WASM_OK)
        return raise_engine(ctx, exception, status, error);
    return make_handle(ctx, KindModule, module);
}

void put_type(Output& out, const yozora_wasm_externtype& type)
{
    switch (type.kind) {
    case YOZORA_WASM_KIND_FUNCTION:
        out.push_string(letters(type.function.params, type.function.nparams));
        out.push_string(letters(type.function.results, type.function.nresults));
        out.push_number(0);
        out.push(JSValueMakeBoolean(out.ctx, false));
        break;
    case YOZORA_WASM_KIND_TABLE:
        out.push_string(std::string(1, type_letter(type.value_type)));
        out.push_number(type.minimum);
        out.push_number(type.has_maximum ? type.maximum : -1.0);
        out.push(JSValueMakeBoolean(out.ctx, false));
        break;
    case YOZORA_WASM_KIND_MEMORY:
        out.push_string("");
        out.push_number(type.minimum);
        out.push_number(type.has_maximum ? type.maximum : -1.0);
        out.push(JSValueMakeBoolean(out.ctx, false));
        break;
    default:
        out.push_string(std::string(1, type_letter(type.value_type)));
        out.push_number(0);
        out.push_number(0);
        out.push(JSValueMakeBoolean(out.ctx, type.mutable_global != 0));
        break;
    }
}

// each entry is a flat array: module (imports only), name, kind, then the four fields put_type writes
NATIVE(native_module_imports)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindModule) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a module");
    auto* module = static_cast<yozora_wasm_module*>(handle->object);
    Output list(ctx);
    for (uint32_t i = 0; i < yozora_wasm_module_import_count(module); ++i) {
        const char* module_name;
        const char* field;
        yozora_wasm_externtype type;
        if (!yozora_wasm_module_import(module, i, &module_name, &field, &type))
            continue;
        Output entry(ctx);
        entry.push(make_string(ctx, module_name));
        entry.push(make_string(ctx, field));
        entry.push_number(type.kind);
        put_type(entry, type);
        list.push(entry.array);
    }
    return list.array;
}

NATIVE(native_module_exports)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindModule) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a module");
    auto* module = static_cast<yozora_wasm_module*>(handle->object);
    Output list(ctx);
    for (uint32_t i = 0; i < yozora_wasm_module_export_count(module); ++i) {
        const char* name;
        yozora_wasm_externtype type;
        if (!yozora_wasm_module_export(module, i, &name, &type))
            continue;
        Output entry(ctx);
        entry.push(make_string(ctx, name));
        entry.push_number(type.kind);
        put_type(entry, type);
        list.push(entry.array);
    }
    return list.array;
}

NATIVE(native_custom_sections)
{
    Handle* handle = argc >= 2 ? handle_of(ctx, argv[0], KindModule) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a module");
    JSStringRef wanted = JSValueToStringCopy(ctx, argv[1], exception);
    if (!wanted)
        return nullptr;
    std::vector<char> name(JSStringGetMaximumUTF8CStringSize(wanted));
    JSStringGetUTF8CString(wanted, name.data(), name.size());
    JSStringRelease(wanted);
    auto* module = static_cast<yozora_wasm_module*>(handle->object);
    Output list(ctx);
    const uint8_t* section;
    size_t size;
    for (uint32_t i = 0; yozora_wasm_module_custom_section(module, name.data(), i, &section, &size); ++i) {
        JSObjectRef buffer = JSObjectMakeTypedArray(ctx, kJSTypedArrayTypeUint8Array, size, exception);
        if (!buffer)
            return nullptr;
        if (size)
            std::memcpy(JSObjectGetTypedArrayBytesPtr(ctx, buffer, nullptr), section, size);
        list.push(JSObjectGetTypedArrayBuffer(ctx, buffer, nullptr));
    }
    return list.array;
}

NATIVE(native_instantiate)
{
    Handle* module_handle = argc >= 2 ? handle_of(ctx, argv[0], KindModule) : nullptr;
    JSObjectRef list = argc >= 2 ? JSValueToObject(ctx, argv[1], nullptr) : nullptr;
    if (!module_handle || !list)
        return raise_status(ctx, exception, "type", "a module and its imports are needed");
    auto* module = static_cast<yozora_wasm_module*>(module_handle->object);
    uint32_t count = yozora_wasm_module_import_count(module);
    Cursor cursor(ctx, list);
    if (cursor.length != count)
        return raise_status(ctx, exception, "link", "the imports do not match the module");
    std::vector<yozora_wasm_extern> externs(count);
    // functions made here for host callables: the instance takes its own reference, this one goes after the call
    std::vector<yozora_wasm_func*> made;
    auto drop_made = [&] {
        for (auto* function : made)
            yozora_wasm_func_release(function);
    };
    for (uint32_t i = 0; i < count; ++i) {
        const char* module_name;
        const char* field;
        yozora_wasm_externtype type;
        yozora_wasm_module_import(module, i, &module_name, &field, &type);
        JSValueRef value = cursor.next();
        externs[i].kind = type.kind;
        Handle* handle = nullptr;
        switch (type.kind) {
        case YOZORA_WASM_KIND_FUNCTION:
            if ((handle = handle_of(ctx, value, KindFunc))) {
                externs[i].object.function = static_cast<yozora_wasm_func*>(handle->object);
                break;
            }
            if (!JSValueIsObject(ctx, value) || !JSObjectIsFunction(ctx, JSValueToObject(ctx, value, nullptr))) {
                drop_made();
                return raise_status(ctx, exception, "link", "a function import needs a function");
            } else {
                auto* host = new Host;
                host->function = ::toJS(::toJS(ctx), value);
                host->params.assign(type.function.params, type.function.params + type.function.nparams);
                host->results.assign(type.function.results, type.function.results + type.function.nresults);
                auto* function = yozora_wasm_func_create_host(type.function.nparams, host->params.data(), type.function.nresults, host->results.data(), host_call, host,
                    host_finalize);
                if (!function) {
                    delete host;
                    drop_made();
                    return raise_status(ctx, exception, "range", "out of memory");
                }
                // the function the page gave stays alive as long as the engine function does
                JSC::gcProtect(host->function);
                made.push_back(function);
                externs[i].object.function = function;
            }
            break;
        case YOZORA_WASM_KIND_TABLE:
            handle = handle_of(ctx, value, KindTable);
            if (handle)
                externs[i].object.table = static_cast<yozora_wasm_table*>(handle->object);
            break;
        case YOZORA_WASM_KIND_MEMORY:
            handle = handle_of(ctx, value, KindMemory);
            if (handle)
                externs[i].object.memory = static_cast<yozora_wasm_memory*>(handle->object);
            break;
        default:
            handle = handle_of(ctx, value, KindGlobal);
            if (handle)
                externs[i].object.global = static_cast<yozora_wasm_global*>(handle->object);
            break;
        }
        if (!handle && type.kind != YOZORA_WASM_KIND_FUNCTION) {
            drop_made();
            return raise_status(ctx, exception, "link", "an import needs the object of its kind");
        }
    }
    Entry entry(ctx);
    if (!entry.ok) {
        drop_made();
        return raise_status(ctx, exception, "range", "out of memory");
    }
    char error[256] = "";
    yozora_wasm_instance* instance = nullptr;
    auto status = yozora_wasm_instantiate(entry.state.context, module, externs.data(), count, &instance, error, sizeof(error));
    drop_made();
    if (status == YOZORA_WASM_HOST_ERROR)
        return release_pending(ctx, exception);
    if (status != YOZORA_WASM_OK)
        return raise_engine(ctx, exception, status, error);
    return make_handle(ctx, KindInstance, instance);
}

// each export: name, kind, handle. the module says what the instance exports and in which order
NATIVE(native_instance_exports)
{
    Handle* instance_handle = argc >= 2 ? handle_of(ctx, argv[0], KindInstance) : nullptr;
    Handle* module_handle = argc >= 2 ? handle_of(ctx, argv[1], KindModule) : nullptr;
    if (!instance_handle || !module_handle)
        return raise_status(ctx, exception, "type", "an instance and its module are needed");
    auto* instance = static_cast<yozora_wasm_instance*>(instance_handle->object);
    auto* module = static_cast<yozora_wasm_module*>(module_handle->object);
    Output list(ctx);
    for (uint32_t i = 0; i < yozora_wasm_module_export_count(module); ++i) {
        const char* name;
        yozora_wasm_externtype type;
        yozora_wasm_extern object;
        if (!yozora_wasm_module_export(module, i, &name, &type) || !yozora_wasm_instance_export(instance, i, &object))
            continue;
        // the export comes with a reference of its own, the handle takes it over
        HandleKind kind = KindFunc;
        void* pointer = object.object.function;
        switch (object.kind) {
        case YOZORA_WASM_KIND_TABLE: kind = KindTable; pointer = object.object.table; break;
        case YOZORA_WASM_KIND_MEMORY: kind = KindMemory; pointer = object.object.memory; break;
        case YOZORA_WASM_KIND_GLOBAL: kind = KindGlobal; pointer = object.object.global; break;
        default: break;
        }
        Output entry(ctx);
        entry.push(make_string(ctx, name));
        entry.push_number(object.kind);
        entry.push(make_handle(ctx, kind, pointer));
        list.push(entry.array);
    }
    return list.array;
}

// [parameters, results, index in the function space of its module]
NATIVE(native_func_info)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindFunc) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a function");
    auto* function = static_cast<yozora_wasm_func*>(handle->object);
    auto type = yozora_wasm_func_type(function);
    Output out(ctx);
    out.push_string(letters(type.params, type.nparams));
    out.push_string(letters(type.results, type.nresults));
    out.push_number(yozora_wasm_func_index(function));
    return out.array;
}

// the identity of the engine object behind a handle, so the API code gives one wrapper to one object
NATIVE(native_identity)
{
    Handle* handle = nullptr;
    for (auto kind : { KindFunc, KindMemory, KindTable, KindGlobal, KindInstance, KindModule }) {
        if (argc && (handle = handle_of(ctx, argv[0], kind)))
            break;
    }
    if (!handle)
        return raise_status(ctx, exception, "type", "not a handle");
    return JSValueMakeNumber(ctx, static_cast<double>(reinterpret_cast<uintptr_t>(handle->object)));
}

NATIVE(native_call)
{
    Handle* handle = argc >= 2 ? handle_of(ctx, argv[0], KindFunc) : nullptr;
    JSObjectRef list = argc >= 2 ? JSValueToObject(ctx, argv[1], nullptr) : nullptr;
    if (!handle || !list)
        return raise_status(ctx, exception, "type", "a function and its arguments are needed");
    auto* function = static_cast<yozora_wasm_func*>(handle->object);
    auto type = yozora_wasm_func_type(function);
    std::vector<yozora_wasm_slot> arguments(type.nparams ? type.nparams : 1);
    Cursor cursor(ctx, list);
    for (uint32_t i = 0; i < type.nparams; ++i) {
        if (!get_slot(cursor, type.params[i], arguments[i], exception))
            return nullptr;
    }
    Entry entry(ctx);
    if (!entry.ok)
        return raise_status(ctx, exception, "range", "out of memory");
    yozora_wasm_slot results[YOZORA_WASM_MAX_RESULTS] = {};
    char error[256] = "";
    auto status = yozora_wasm_call(entry.state.context, function, arguments.data(), results, error, sizeof(error));
    return finish_call(ctx, exception, status, error, type.results, type.nresults, results);
}

void view_before_grow(void* user)
{
    auto* view = static_cast<MemoryView*>(user);
    JSC::ArrayBuffer* buffer = view->buffer;
    if (!buffer)
        return;
    view->buffer = nullptr;
    view->bytes = nullptr;
    buffer->detach(*view->vm);
}

void view_destroy(void* user) { delete static_cast<MemoryView*>(user); }

MemoryView* view_of(JSContextRef ctx, yozora_wasm_memory* memory)
{
    if (auto* view = static_cast<MemoryView*>(yozora_wasm_memory_observer_user(memory)))
        return view;
    auto* view = new MemoryView;
    view->vm = &::toJS(ctx)->vm();
    yozora_wasm_memory_set_observer(memory, view_before_grow, view_destroy, view);
    return view;
}

NATIVE(native_memory_create)
{
    double initial = number_argument(ctx, argc, argv, 0), maximum = number_argument(ctx, argc, argv, 1);
    bool has_maximum = maximum >= 0;
    if (!(initial >= 0) || initial > YOZORA_WASM_MAX_PAGES || (has_maximum && maximum > 4294967295.0))
        return raise_status(ctx, exception, "range", "WebAssembly.Memory(): the size is out of range");
    auto* memory = yozora_wasm_memory_create(static_cast<uint32_t>(initial), has_maximum ? static_cast<uint32_t>(maximum) : 0, has_maximum);
    if (!memory)
        return raise_status(ctx, exception, "range", "WebAssembly.Memory(): could not allocate memory");
    return make_handle(ctx, KindMemory, memory);
}

NATIVE(native_memory_grow)
{
    Handle* handle = argc >= 2 ? handle_of(ctx, argv[0], KindMemory) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a memory");
    double delta = number_argument(ctx, argc, argv, 1);
    if (!(delta >= 0) || delta > 4294967295.0)
        return JSValueMakeNumber(ctx, -1);
    auto* memory = static_cast<yozora_wasm_memory*>(handle->object);
    // the view exists before the growth so that the observer drops a buffer a script holds
    view_of(ctx, memory);
    return JSValueMakeNumber(ctx, static_cast<double>(yozora_wasm_memory_grow(memory, static_cast<uint32_t>(delta))));
}

NATIVE(native_memory_buffer)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindMemory) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a memory");
    auto* memory = static_cast<yozora_wasm_memory*>(handle->object);
    MemoryView* view = view_of(ctx, memory);
    JSC::JSGlobalObject* global = ::toJS(ctx);
    auto* dom = dynamicDowncast<WebCore::JSDOMGlobalObject>(global);
    if (!dom)
        return raise_status(ctx, exception, "type", "this global object has no buffers");
    if (!view->buffer) {
        uint8_t* data = yozora_wasm_memory_data(memory);
        size_t size = yozora_wasm_memory_size(memory);
        yozora_wasm_memory_retain(memory);
        auto buffer = JSC::ArrayBuffer::createFromBytes({ data, size }, createSharedTask<void(void*)>([memory](void* bytes) {
            // the buffer let go: forget it, and give back the reference that kept the bytes alive for it
            if (auto* held = static_cast<MemoryView*>(yozora_wasm_memory_observer_user(memory))) {
                if (held->bytes == bytes) {
                    held->buffer = nullptr;
                    held->bytes = nullptr;
                }
            }
            yozora_wasm_memory_release(memory);
        }));
        // a script cannot transfer or detach it: the engine does, when the memory grows
        buffer->makeWasmMemory();
        view->buffer = buffer.ptr();
        view->bytes = data;
        return ::toRef(global, WebCore::toJS(global, dom, buffer.get()));
    }
    return ::toRef(global, WebCore::toJS(global, dom, *view->buffer));
}

NATIVE(native_memory_pages)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindMemory) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a memory");
    return JSValueMakeNumber(ctx, yozora_wasm_memory_pages(static_cast<yozora_wasm_memory*>(handle->object)));
}

NATIVE(native_table_create)
{
    if (argc < 4 || !JSValueIsString(ctx, argv[0]))
        return raise_status(ctx, exception, "type", "a table needs a type, sizes and a value");
    double initial = number_argument(ctx, argc, argv, 1), maximum = number_argument(ctx, argc, argv, 2);
    bool has_maximum = maximum >= 0;
    char letter = 'F';
    {
        JSStringRef text = JSValueToStringCopy(ctx, argv[0], nullptr);
        char buffer[4] = "F";
        JSStringGetUTF8CString(text, buffer, sizeof(buffer));
        JSStringRelease(text);
        letter = buffer[0];
    }
    if (!(initial >= 0) || initial > YOZORA_WASM_MAX_TABLE_ELEMENTS || (has_maximum && maximum > 4294967295.0))
        return raise_status(ctx, exception, "range", "WebAssembly.Table(): the size is out of range");
    JSObjectRef holder = JSObjectMakeArray(ctx, 1, &argv[3], nullptr);
    Cursor cursor(ctx, holder);
    yozora_wasm_slot fill = 0;
    if (!get_slot(cursor, letter_type(letter), fill, exception))
        return nullptr;
    auto* table = yozora_wasm_table_create(letter_type(letter), static_cast<uint32_t>(initial), has_maximum ? static_cast<uint32_t>(maximum) : 0, has_maximum, fill);
    if (!table)
        return raise_status(ctx, exception, "range", "WebAssembly.Table(): could not allocate the table");
    return make_handle(ctx, KindTable, table);
}

NATIVE(native_table_get)
{
    Handle* handle = argc >= 2 ? handle_of(ctx, argv[0], KindTable) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a table");
    auto* table = static_cast<yozora_wasm_table*>(handle->object);
    double index = number_argument(ctx, argc, argv, 1);
    yozora_wasm_slot slot = 0;
    if (!(index >= 0) || index > 4294967295.0 || !yozora_wasm_table_get(table, static_cast<uint32_t>(index), &slot))
        return raise_status(ctx, exception, "range", "WebAssembly.Table.get(): invalid address");
    Output out(ctx);
    put_slot(ctx, out, yozora_wasm_table_type(table), slot);
    return JSObjectGetPropertyAtIndex(ctx, out.array, 0, nullptr);
}

NATIVE(native_table_set)
{
    Handle* handle = argc >= 3 ? handle_of(ctx, argv[0], KindTable) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a table");
    auto* table = static_cast<yozora_wasm_table*>(handle->object);
    double index = number_argument(ctx, argc, argv, 1);
    JSObjectRef holder = JSObjectMakeArray(ctx, 1, &argv[2], nullptr);
    Cursor cursor(ctx, holder);
    yozora_wasm_slot slot = 0;
    if (!get_slot(cursor, yozora_wasm_table_type(table), slot, exception))
        return nullptr;
    if (!(index >= 0) || index > 4294967295.0 || !yozora_wasm_table_set(table, static_cast<uint32_t>(index), slot))
        return raise_status(ctx, exception, "range", "WebAssembly.Table.set(): invalid address");
    return JSValueMakeUndefined(ctx);
}

NATIVE(native_table_grow)
{
    Handle* handle = argc >= 3 ? handle_of(ctx, argv[0], KindTable) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a table");
    auto* table = static_cast<yozora_wasm_table*>(handle->object);
    double delta = number_argument(ctx, argc, argv, 1);
    JSObjectRef holder = JSObjectMakeArray(ctx, 1, &argv[2], nullptr);
    Cursor cursor(ctx, holder);
    yozora_wasm_slot fill = 0;
    if (!get_slot(cursor, yozora_wasm_table_type(table), fill, exception))
        return nullptr;
    if (!(delta >= 0) || delta > 4294967295.0)
        return JSValueMakeNumber(ctx, -1);
    return JSValueMakeNumber(ctx, static_cast<double>(yozora_wasm_table_grow(table, static_cast<uint32_t>(delta), fill)));
}

NATIVE(native_table_size)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindTable) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a table");
    return JSValueMakeNumber(ctx, yozora_wasm_table_size(static_cast<yozora_wasm_table*>(handle->object)));
}

NATIVE(native_global_create)
{
    if (argc < 3 || !JSValueIsString(ctx, argv[0]))
        return raise_status(ctx, exception, "type", "a global needs a type, a mutability and a value");
    char buffer[4] = "i";
    JSStringRef text = JSValueToStringCopy(ctx, argv[0], nullptr);
    JSStringGetUTF8CString(text, buffer, sizeof(buffer));
    JSStringRelease(text);
    uint8_t type = letter_type(buffer[0]);
    JSObjectRef values = JSValueToObject(ctx, argv[2], nullptr);
    if (!values)
        return raise_status(ctx, exception, "type", "a global needs a value");
    Cursor cursor(ctx, values);
    yozora_wasm_slot slot = 0;
    if (!get_slot(cursor, type, slot, exception))
        return nullptr;
    auto* global = yozora_wasm_global_create(type, JSValueToBoolean(ctx, argv[1]) ? 1 : 0, slot);
    if (!global)
        return raise_status(ctx, exception, "range", "out of memory");
    return make_handle(ctx, KindGlobal, global);
}

NATIVE(native_global_get)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindGlobal) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a global");
    auto* global = static_cast<yozora_wasm_global*>(handle->object);
    Output out(ctx);
    put_slot(ctx, out, yozora_wasm_global_type(global), yozora_wasm_global_get(global));
    return out.array;
}

NATIVE(native_global_set)
{
    Handle* handle = argc >= 2 ? handle_of(ctx, argv[0], KindGlobal) : nullptr;
    JSObjectRef values = argc >= 2 ? JSValueToObject(ctx, argv[1], nullptr) : nullptr;
    if (!handle || !values)
        return raise_status(ctx, exception, "type", "a global and a value are needed");
    auto* global = static_cast<yozora_wasm_global*>(handle->object);
    Cursor cursor(ctx, values);
    yozora_wasm_slot slot = 0;
    if (!get_slot(cursor, yozora_wasm_global_type(global), slot, exception))
        return nullptr;
    return JSValueMakeBoolean(ctx, yozora_wasm_global_set(global, slot) != 0);
}

// the type of a global, as the letter the API code uses
NATIVE(native_global_type)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindGlobal) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a global");
    auto* global = static_cast<yozora_wasm_global*>(handle->object);
    Output out(ctx);
    out.push_string(std::string(1, type_letter(yozora_wasm_global_type(global))));
    out.push(JSValueMakeBoolean(ctx, yozora_wasm_global_mutable(global) != 0));
    return out.array;
}

NATIVE(native_table_type)
{
    Handle* handle = argc ? handle_of(ctx, argv[0], KindTable) : nullptr;
    if (!handle)
        return raise_status(ctx, exception, "type", "not a table");
    return make_string(ctx, std::string(1, type_letter(yozora_wasm_table_type(static_cast<yozora_wasm_table*>(handle->object)))).c_str());
}

struct NativeFunction {
    const char* name;
    JSObjectCallAsFunctionCallback callback;
};

const NativeFunction natives[] = {
    { "isStatus", native_is_status },
    { "validate", native_validate },
    { "moduleCreate", native_module_create },
    { "moduleImports", native_module_imports },
    { "moduleExports", native_module_exports },
    { "customSections", native_custom_sections },
    { "instantiate", native_instantiate },
    { "instanceExports", native_instance_exports },
    { "funcInfo", native_func_info },
    { "identity", native_identity },
    { "call", native_call },
    { "memoryCreate", native_memory_create },
    { "memoryGrow", native_memory_grow },
    { "memoryBuffer", native_memory_buffer },
    { "memoryPages", native_memory_pages },
    { "tableCreate", native_table_create },
    { "tableGet", native_table_get },
    { "tableSet", native_table_set },
    { "tableGrow", native_table_grow },
    { "tableSize", native_table_size },
    { "tableType", native_table_type },
    { "globalCreate", native_global_create },
    { "globalGet", native_global_get },
    { "globalSet", native_global_set },
    { "globalType", native_global_type },
};

// the first read of WebAssembly: evaluates the API code, hands it the natives, returns what it makes
NATIVE(native_build)
{
    JSStringRef source = JSStringCreateWithUTF8CString(wasm_api_source);
    JSValueRef factory = JSEvaluateScript(ctx, source, nullptr, nullptr, 1, exception);
    JSStringRelease(source);
    if (!factory)
        return nullptr;
    JSObjectRef natives_object = JSObjectMake(ctx, nullptr, nullptr);
    for (const auto& native : natives) {
        JSStringRef name = JSStringCreateWithUTF8CString(native.name);
        JSObjectRef function = JSObjectMakeFunctionWithCallback(ctx, name, native.callback);
        JSObjectSetProperty(ctx, natives_object, name, function, kJSPropertyAttributeNone, nullptr);
        JSStringRelease(name);
    }
    JSObjectRef factory_function = JSValueToObject(ctx, factory, exception);
    if (!factory_function)
        return nullptr;
    JSValueRef argument = natives_object;
    return JSObjectCallAsFunction(ctx, factory_function, nullptr, 1, &argument, exception);
}

// WebAssembly is a lazy accessor on the global object: reading it builds the API and puts it in place as the
// plain writable property the specification describes
const char install_source[] =
    "(function (build) {"
    "var define = function (value) { Object.defineProperty(globalThis, 'WebAssembly', { value: value, writable: true, configurable: true, enumerable: false }); };"
    "Object.defineProperty(globalThis, 'WebAssembly', { configurable: true, enumerable: false,"
    "get: function () { var api = build(); define(api); return api; },"
    "set: function (value) { define(value); } });"
    "})";

} // namespace

extern "C" int yozora_wasm_js_install(JSContextRef ctx)
{
    if (!ctx)
        return 0;
    JSStringRef source = JSStringCreateWithUTF8CString(install_source);
    JSValueRef installer = JSEvaluateScript(ctx, source, nullptr, nullptr, 1, nullptr);
    JSStringRelease(source);
    if (!installer)
        return 0;
    JSStringRef name = JSStringCreateWithUTF8CString("build");
    JSObjectRef build = JSObjectMakeFunctionWithCallback(ctx, name, native_build);
    JSStringRelease(name);
    JSObjectRef function = JSValueToObject(ctx, installer, nullptr);
    JSValueRef argument = build;
    JSValueRef exception = nullptr;
    JSObjectCallAsFunction(ctx, function, nullptr, 1, &argument, &exception);
    return exception ? 0 : 1;
}
