#include <JavaScriptCore/JavaScript.h>

#include <assert.h>
#include <stdio.h>

#ifdef __APPLE__
#include "yozora_darwin.h"
#endif

/* JSGarbageCollect only reports abandoned objects, force full cycles in this test */
extern void JSSynchronousGarbageCollectForDebugging(JSContextRef context);

static JSValueRef evaluate(JSContextRef context, const char *source)
{
    JSStringRef script = JSStringCreateWithUTF8CString(source);
    JSValueRef exception = NULL;
    JSValueRef result = JSEvaluateScript(context, script, NULL, NULL, 1, &exception);
    JSStringRelease(script);
    assert(result && !exception);
    return result;
}

int main(void)
{
    unsigned iteration;
#ifdef __APPLE__
    size_t peak_resident = 0, final_resident = 0;
#endif
    for (iteration = 0; iteration < 20; ++iteration) {
        JSContextGroupRef group = JSContextGroupCreate();
        JSGlobalContextRef context = JSGlobalContextCreateInGroup(group, NULL);
        JSValueRef exception = NULL;
        JSValueRef value = evaluate(context,
            "(() => {"
            " const buffers = Array.from({length: 256}, (_, index) => {"
            "   const buffer = new Uint8Array(4096); buffer[0] = index; return buffer;"
            " });"
            " return {buffers, sum: buffers.reduce((sum, buffer) => sum + buffer[0], 0)};"
            "})()");
        unsigned round;
        JSValueProtect(context, value);
        for (round = 0; round < 4; ++round) {
            evaluate(context,
                "(() => {"
                " const temporary = Array.from({length: 512}, () => new Uint8Array(1024));"
                " return temporary.length;"
                "})()");
            JSSynchronousGarbageCollectForDebugging(context);
        }
        assert(JSValueIsObject(context, value));
        JSObjectRef object = JSValueToObject(context, value, &exception);
        assert(object && !exception);
        JSStringRef property = JSStringCreateWithUTF8CString("sum");
        JSValueRef sum = JSObjectGetProperty(context, object, property, &exception);
        JSStringRelease(property);
        assert(sum && !exception);
        assert(JSValueToNumber(context, sum, &exception) == 32640 && !exception);
        property = JSStringCreateWithUTF8CString("buffers");
        JSValueRef buffers = JSObjectGetProperty(context, object, property, &exception);
        JSStringRelease(property);
        assert(buffers && !exception);
        JSObjectRef array = JSValueToObject(context, buffers, &exception);
        assert(array && !exception);
        JSValueRef last = JSObjectGetPropertyAtIndex(context, array, 255, &exception);
        assert(last && !exception);
        JSObjectRef buffer = JSValueToObject(context, last, &exception);
        assert(buffer && !exception);
        JSValueRef byte = JSObjectGetPropertyAtIndex(context, buffer, 0, &exception);
        assert(byte && !exception);
        assert(JSValueToNumber(context, byte, &exception) == 255 && !exception);
        JSValueUnprotect(context, value);
        JSGlobalContextRelease(context);
        JSContextGroupRelease(group);
#ifdef __APPLE__
        assert(yozora_darwin_memory_bytes(&final_resident) == YOZORA_CLOCK_OK);
        if (final_resident > peak_resident)
            peak_resident = final_resident;
#endif
    }
    puts("javascriptcore lifecycle: 20 context groups, 80 full GC cycles, protected values and teardown passed");
#ifdef __APPLE__
    printf("resident bytes after teardown: max=%zu final=%zu\n", peak_resident, final_resident);
#endif
    return 0;
}
