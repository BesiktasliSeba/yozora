#pragma once

// prints where a fatal signal hit as raw addresses for yozora_crash.py and stops the process with the same
// signal. the ios crash reporter writes nothing for a tool started from a shell on the phones this runs on
#if defined(__APPLE__) && (defined(__arm__) || defined(__aarch64__))

#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#include <mach-o/dyld.h>
#include <sys/ucontext.h>

namespace YozoraCrash {

static void report(int signal_number, siginfo_t* info, void* context)
{
    auto* machine = static_cast<ucontext_t*>(context)->uc_mcontext;
#if defined(__arm__)
    uintptr_t pc = machine->__ss.__pc, lr = machine->__ss.__lr, sp = machine->__ss.__sp, fp = machine->__ss.__r[7];
#else
    uintptr_t pc = machine->__ss.__pc, lr = machine->__ss.__lr, sp = machine->__ss.__sp, fp = machine->__ss.__fp;
#endif
    uintptr_t fp0 = fp;
    char line[256];
    int length = std::snprintf(line, sizeof(line), "yozora-render: crash signal=%d code=%d address=%lx slide=%lx pc=%lx lr=%lx sp=%lx\nyozora-render: crash frames", signal_number, info->si_code,
                               static_cast<unsigned long>(reinterpret_cast<uintptr_t>(info->si_addr)), static_cast<unsigned long>(_dyld_get_image_vmaddr_slide(0)),
                               static_cast<unsigned long>(pc), static_cast<unsigned long>(lr), static_cast<unsigned long>(sp));
    write(2, line, length);
    pthread_t self = pthread_self();
    uintptr_t top = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(self));
    length = std::snprintf(line, sizeof(line), "yozora-render: crash stack top %lx size %lx\n", static_cast<unsigned long>(top), static_cast<unsigned long>(pthread_get_stacksize_np(self)));
    write(2, line, length);
    for (int depth = 0; depth < 40 && fp >= sp && fp + 8 <= top && !(fp & 3); ++depth) {
        const uintptr_t* frame = reinterpret_cast<const uintptr_t*>(fp);
        length = std::snprintf(line, sizeof(line), " %lx", static_cast<unsigned long>(frame[1]));
        write(2, line, length);
        if (frame[0] <= fp)
            break;
        fp = frame[0];
    }
    write(2, "\n", 1);
    // the general registers: the instruction at pc names the one that held the bad pointer
    write(2, "yozora-render: crash registers", 30);
#if defined(__arm__)
    for (int index = 0; index < 13; ++index) {
        length = std::snprintf(line, sizeof(line), " %lx", static_cast<unsigned long>(machine->__ss.__r[index]));
        write(2, line, length);
    }
#else
    for (int index = 0; index < 29; ++index) {
        length = std::snprintf(line, sizeof(line), " %lx", static_cast<unsigned long>(machine->__ss.__x[index]));
        write(2, line, length);
    }
#endif
    write(2, "\n", 1);
    // the instructions at pc, to compare with what the binary holds
    write(2, "yozora-render: crash code", 25);
    for (int offset = -12; offset <= 4; offset += 2) {
        length = std::snprintf(line, sizeof(line), " %04x", *reinterpret_cast<const uint16_t*>((pc & ~static_cast<uintptr_t>(1)) + offset));
        write(2, line, length);
    }
    write(2, "\n", 1);
    // the words around the frame pointer, where a call frame and the record of a vm entry live
    write(2, "yozora-render: crash frame words", 32);
    {
        uintptr_t base = (fp0 - 0xa0) & ~static_cast<uintptr_t>(3);
        for (int index = 0; index < 64 && base + index * 4 + 4 <= top && base + index * 4 >= sp; ++index) {
            length = std::snprintf(line, sizeof(line), " %lx", static_cast<unsigned long>(reinterpret_cast<const uintptr_t*>(base)[index]));
            write(2, line, length);
        }
        length = std::snprintf(line, sizeof(line), " from %lx\n", static_cast<unsigned long>(base));
        write(2, line, length);
    }
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

static void install()
{
    struct sigaction action = { };
    action.sa_sigaction = report;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for (int signal_number : { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP })
        sigaction(signal_number, &action, nullptr);
}

}

#else

namespace YozoraCrash {

static void install() { }

}

#endif
