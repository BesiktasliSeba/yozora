#define _POSIX_C_SOURCE 200809L

#include "yozora_types.h"
#include <JavaScriptCore/JavaScript.h>
#include <JavaScriptCore/JSContextRefPrivate.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOURCE_LIMIT (1024 * 1024)
#define OUTPUT_LIMIT (64 * 1024)

typedef struct {
    int timed_out;
    JSValueRef rejection;
} execution_state;

static bool terminate_script(JSContextRef context, void *data) {
    execution_state *state = data;
    (void)context;
    state->timed_out = 1;
    return true;
}

static JSValueRef rejected_promise(JSContextRef context, JSObjectRef function,
                                  JSObjectRef this_object, size_t count,
                                  const JSValueRef arguments[], JSValueRef *exception) {
    execution_state *state = JSObjectGetPrivate(JSContextGetGlobalObject(context));
    (void)function;
    (void)this_object;
    (void)exception;
    if (!state || count != 2) {
        fputs("yozora-js: invalid rejection callback\n", stderr);
        abort();
    }
    if (!state->rejection) {
        state->rejection = arguments[1];
        JSValueProtect(context, state->rejection);
    }
    return JSValueMakeUndefined(context);
}

static char *read_source(const char *path) {
    struct stat attributes;
    char *source = NULL;
    FILE *stream = NULL;
    size_t length;
    int descriptor = open(path, O_RDONLY | O_NONBLOCK);
    if (descriptor < 0) {
        fprintf(stderr, "yozora-js: open: %s\n", strerror(errno));
        return NULL;
    }
    if (fstat(descriptor, &attributes) != 0 || !S_ISREG(attributes.st_mode)) {
        fputs("yozora-js: source must be a regular file\n", stderr);
        close(descriptor);
        return NULL;
    }
    stream = fdopen(descriptor, "rb");
    if (!stream) {
        fprintf(stderr, "yozora-js: fdopen: %s\n", strerror(errno));
        close(descriptor);
        return NULL;
    }
    source = malloc(SOURCE_LIMIT + 1);
    if (!source) {
        fputs("yozora-js: cannot allocate source buffer\n", stderr);
        fclose(stream);
        return NULL;
    }
    length = fread(source, 1, SOURCE_LIMIT + 1, stream);
    if (ferror(stream)) {
        fputs("yozora-js: source read failed\n", stderr);
        free(source);
        source = NULL;
    } else if (length > SOURCE_LIMIT) {
        fputs("yozora-js: source exceeds 1 MiB\n", stderr);
        free(source);
        source = NULL;
    } else if (memchr(source, '\0', length)) {
        fputs("yozora-js: source contains a NUL byte\n", stderr);
        free(source);
        source = NULL;
    } else {
        source[length] = '\0';
        if (yozora_text_check(source, length + 1, 1) != YOZORA_OK) {
            fputs("yozora-js: source is not valid UTF-8\n", stderr);
            free(source);
            source = NULL;
        }
    }
    if (fclose(stream) != 0) {
        fputs("yozora-js: source close failed\n", stderr);
        free(source);
        source = NULL;
    }
    return source;
}

static int write_value(JSContextRef context, JSValueRef value, FILE *stream) {
    JSValueRef exception = NULL;
    JSStringRef string = JSValueToStringCopy(context, value, &exception);
    size_t capacity, length;
    char *bytes;
    int result = 0;
    if (!string || exception) {
        if (string) JSStringRelease(string);
        fputs("yozora-js: result conversion threw an exception\n", stderr);
        return 0;
    }
    capacity = JSStringGetMaximumUTF8CStringSize(string);
    if (capacity > OUTPUT_LIMIT) {
        JSStringRelease(string);
        fputs("yozora-js: result exceeds the 64 KiB encoding buffer\n", stderr);
        return 0;
    }
    bytes = malloc(capacity);
    if (!bytes) {
        JSStringRelease(string);
        fputs("yozora-js: cannot allocate result buffer\n", stderr);
        return 0;
    }
    length = JSStringGetUTF8CString(string, bytes, capacity);
    if (length && fwrite(bytes, 1, length - 1, stream) == length - 1 &&
        fputc('\n', stream) != EOF && fflush(stream) == 0) {
        result = 1;
    } else {
        fputs("yozora-js: result write failed\n", stderr);
    }
    free(bytes);
    JSStringRelease(string);
    return result;
}

int main(int argc, char **argv) {
    execution_state state = {0, NULL};
    JSClassRef global_class;
    JSContextGroupRef group;
    JSGlobalContextRef context;
    JSStringRef script;
    JSObjectRef rejection_callback;
    JSValueRef exception = NULL, result = NULL;
    char *source;
    int exit_status = 1;
    if (argc != 2) {
        fputs("usage: yozora-js FILE\n", stderr);
        return 2;
    }
    source = read_source(argv[1]);
    if (!source) return 2;
    global_class = JSClassCreate(&kJSClassDefinitionEmpty);
    group = JSContextGroupCreate();
    context = JSGlobalContextCreateInGroup(group, global_class);
    int private_set = JSObjectSetPrivate(JSContextGetGlobalObject(context), &state);
    JSClassRelease(global_class);
    if (!private_set) {
        fputs("yozora-js: global context rejected its callback state\n", stderr);
        free(source);
        JSGlobalContextRelease(context);
        JSContextGroupRelease(group);
        return 1;
    }
    script = JSStringCreateWithUTF8CString(source);
    free(source);
    rejection_callback = JSObjectMakeFunctionWithCallback(context, NULL, rejected_promise);
    JSGlobalContextSetUnhandledRejectionCallback(context, rejection_callback, &exception);
    if (exception) {
        JSValueProtect(context, exception);
        fputs("yozora-js: rejection callback setup failed\n", stderr);
    } else {
        JSContextGroupSetExecutionTimeLimit(group, 2.0, terminate_script, &state);
        result = JSEvaluateScript(context, script, NULL, NULL, 1, &exception);
        if (result) JSValueProtect(context, result);
        if (exception) JSValueProtect(context, exception);
        if (state.timed_out) {
            fputs("yozora-js: execution exceeded 2 seconds of CPU time\n", stderr);
            exit_status = 124;
        } else if (exception) {
            fputs("yozora-js: uncaught exception: ", stderr);
            write_value(context, exception, stderr);
        } else if (state.rejection) {
            fputs("yozora-js: unhandled promise rejection: ", stderr);
            write_value(context, state.rejection, stderr);
        } else if (result) {
            exit_status = write_value(context, result, stdout) ? 0 : 1;
            if (state.rejection) {
                fputs("yozora-js: unhandled promise rejection during result conversion: ", stderr);
                write_value(context, state.rejection, stderr);
                exit_status = 1;
            }
        } else {
            fputs("yozora-js: evaluation returned no value or exception\n", stderr);
        }
        if (state.timed_out && exit_status != 124) {
            fputs("yozora-js: result conversion exceeded its CPU time limit\n", stderr);
            exit_status = 124;
        }
    }
    JSContextGroupClearExecutionTimeLimit(group);
    if (exception) JSValueUnprotect(context, exception);
    if (result) JSValueUnprotect(context, result);
    if (state.rejection) JSValueUnprotect(context, state.rejection);
    JSStringRelease(script);
    JSGlobalContextRelease(context);
    JSContextGroupRelease(group);
    return exit_status;
}
