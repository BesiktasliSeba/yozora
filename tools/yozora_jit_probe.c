/* asks the kernel for executable memory the ways javascriptcore does and runs a few bytes in it. every
   strategy runs in its own child: a kernel that enforces code signing kills the process on the first
   instruction fetch and the parent reports the signal instead of dying */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#endif

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
#if defined(__APPLE__) && !defined(MAP_JIT)
/* the ios 5 and 6 sdks predate the constant, the kernel may still know the bit */
#define MAP_JIT 0x800
#endif

#define ANSWER 42

#if defined(__arm__)
/* thumb-2 as javascriptcore emits it: movs r0, #42; bx lr */
static const uint8_t stub[] = { 0x2a, 0x20, 0x70, 0x47 };
#define STUB_ENTRY_BIT 1u
#elif defined(__aarch64__)
/* movz w0, #42; ret */
static const uint8_t stub[] = { 0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6 };
#define STUB_ENTRY_BIT 0u
#elif defined(__x86_64__)
/* mov eax, 42; ret */
static const uint8_t stub[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };
#define STUB_ENTRY_BIT 0u
#else
#error unsupported architecture
#endif

typedef int (*stub_function)(void);

/* the exit status of a child that finished its strategy */
enum { CHILD_OK = 0, CHILD_MEMORY_DENIED = 10, CHILD_PROTECT_DENIED = 11, CHILD_WRONG_RESULT = 12 };

static void flush_code(void *memory, size_t length)
{
#if defined(__APPLE__)
    sys_icache_invalidate(memory, length);
#else
    __builtin___clear_cache((char *)memory, (char *)memory + length);
#endif
}

static int run_stub(void *memory, size_t page)
{
    memcpy(memory, stub, sizeof(stub));
    flush_code(memory, page);
    stub_function function = (stub_function)((uintptr_t)memory | STUB_ENTRY_BIT);
    return function() == ANSWER ? CHILD_OK : CHILD_WRONG_RESULT;
}

static int child_error(const char *step)
{
    fprintf(stderr, "    %s: %s (errno %d)\n", step, strerror(errno), errno);
    return CHILD_MEMORY_DENIED;
}

/* one anonymous mapping that is writable and executable from the start */
static int strategy_rwx(size_t page, int extra_flags)
{
    void *memory = mmap(NULL, page, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | extra_flags, -1, 0);
    if (memory == MAP_FAILED)
        return child_error("mmap");
    return run_stub(memory, page);
}

static int strategy_rwx_plain(size_t page) { return strategy_rwx(page, 0); }

static int strategy_map_jit(size_t page)
{
#if defined(MAP_JIT)
    return strategy_rwx(page, MAP_JIT);
#else
    (void)page;
    fprintf(stderr, "    MAP_JIT does not exist on this system\n");
    return CHILD_MEMORY_DENIED;
#endif
}

/* reserve first and commit later, which is what the javascriptcore allocator does for its executable pool */
static int strategy_reserve_commit(size_t page)
{
    void *memory = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED)
        return child_error("mmap");
    if (mprotect(memory, page, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        fprintf(stderr, "    mprotect rwx: %s (errno %d)\n", strerror(errno), errno);
        return CHILD_PROTECT_DENIED;
    }
    return run_stub(memory, page);
}

/* write first, then flip the page to executable, and back to writable for the next patch */
static int strategy_write_then_exec(size_t page)
{
    void *memory = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED)
        return child_error("mmap");
    memcpy(memory, stub, sizeof(stub));
    flush_code(memory, page);
    if (mprotect(memory, page, PROT_READ | PROT_EXEC)) {
        fprintf(stderr, "    mprotect rx: %s (errno %d)\n", strerror(errno), errno);
        return CHILD_PROTECT_DENIED;
    }
    stub_function function = (stub_function)((uintptr_t)memory | STUB_ENTRY_BIT);
    if (function() != ANSWER)
        return CHILD_WRONG_RESULT;
    if (mprotect(memory, page, PROT_READ | PROT_WRITE)) {
        fprintf(stderr, "    mprotect back to rw: %s (errno %d)\n", strerror(errno), errno);
        return CHILD_PROTECT_DENIED;
    }
    return CHILD_OK;
}

#if defined(__APPLE__)
/* the mach interface, which some kernels judge differently from mmap */
static int strategy_vm_protect(size_t page)
{
    vm_address_t address = 0;
    kern_return_t status = vm_allocate(mach_task_self(), &address, page, VM_FLAGS_ANYWHERE);
    if (status != KERN_SUCCESS) {
        fprintf(stderr, "    vm_allocate: kern_return %d\n", (int)status);
        return CHILD_MEMORY_DENIED;
    }
    status = vm_protect(mach_task_self(), address, page, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    if (status != KERN_SUCCESS) {
        fprintf(stderr, "    vm_protect rwx: kern_return %d\n", (int)status);
        return CHILD_PROTECT_DENIED;
    }
    return run_stub((void *)address, page);
}
#endif

typedef struct {
    const char *name;
    int (*run)(size_t page);
    /* javascriptcore needs a page that is writable and executable together */
    int writable_and_executable;
} strategy;

static const strategy strategies[] = {
    { "rwx-anon", strategy_rwx_plain, 1 },
    { "map-jit", strategy_map_jit, 1 },
    { "reserve-commit", strategy_reserve_commit, 1 },
    { "write-then-exec", strategy_write_then_exec, 0 },
#if defined(__APPLE__)
    { "vm-protect", strategy_vm_protect, 1 },
#endif
};

static const char *describe(int status, char *buffer, size_t capacity)
{
    if (WIFSIGNALED(status)) {
        snprintf(buffer, capacity, "killed by signal %d (%s)", WTERMSIG(status), strsignal(WTERMSIG(status)));
        return buffer;
    }
    switch (WEXITSTATUS(status)) {
    case CHILD_OK: return "runs code";
    case CHILD_MEMORY_DENIED: return "memory request refused";
    case CHILD_PROTECT_DENIED: return "permission change refused";
    case CHILD_WRONG_RESULT: return "code ran and returned the wrong value";
    }
    snprintf(buffer, capacity, "child exit status %d", WEXITSTATUS(status));
    return buffer;
}

int main(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 4096 || page_size > 65536) {
        fprintf(stderr, "yozora-jit-probe: unexpected page size %ld\n", page_size);
        return 2;
    }
    size_t page = (size_t)page_size;
    const char *usable = NULL;
    int usable_for_jit = 0;
    for (size_t i = 0; i < sizeof(strategies) / sizeof(strategies[0]); ++i) {
        printf("%s\n", strategies[i].name);
        fflush(NULL);
        pid_t child = fork();
        if (child < 0) {
            fprintf(stderr, "yozora-jit-probe: fork: %s\n", strerror(errno));
            return 2;
        }
        if (!child)
            _exit(strategies[i].run(page));
        int status = 0;
        pid_t waited;
        do
            waited = waitpid(child, &status, 0);
        while (waited < 0 && errno == EINTR);
        if (waited != child) {
            fprintf(stderr, "yozora-jit-probe: waitpid: %s\n", strerror(errno));
            return 2;
        }
        char text[64];
        printf("    %s\n", describe(status, text, sizeof(text)));
        if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK) {
            if (!usable)
                usable = strategies[i].name;
            if (strategies[i].writable_and_executable)
                usable_for_jit = 1;
        }
    }
    if (usable_for_jit)
        puts("yozora-jit-probe: javascript jit can use executable memory");
    else if (usable)
        puts("yozora-jit-probe: only write-then-execute memory works, the jit needs the rwx mapping");
    else
        puts("yozora-jit-probe: no executable memory, the interpreter is the only option");
    return usable_for_jit ? 0 : 1;
}
