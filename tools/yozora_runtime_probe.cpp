#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <unicode/ucal.h>
#include <unicode/uidna.h>
#include <unicode/unorm2.h>
#include <unicode/uversion.h>

#if defined(__APPLE__)
#include "yozora_darwin.h"
#endif

static void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static void check_icu(void)
{
    UVersionInfo version;
    u_getVersion(version);
    require(version[0] == 78 && version[1] == 3, "unexpected ICU revision");
    UErrorCode error = U_ZERO_ERROR;
    const UNormalizer2 *normalizer = unorm2_getNFCInstance(&error);
    require(U_SUCCESS(error) && normalizer, "NFC initialization failed");
    UChar source[] = { 'e', 0x301 };
    UChar result[8] = { 0 };
    int32_t length = unorm2_normalize(normalizer, source, 2, result, 8, &error);
    require(U_SUCCESS(error) && length == 1 && result[0] == 0xe9, "NFC data failed");

    UIDNA *idna = uidna_openUTS46(UIDNA_NONTRANSITIONAL_TO_ASCII | UIDNA_CHECK_BIDI | UIDNA_CHECK_CONTEXTJ, &error);
    require(U_SUCCESS(error) && idna, "IDNA initialization failed");
    char ascii[128] = { 0 };
    const char domain[] = "b\xc3\xbc" "cher.example";
    UIDNAInfo info = UIDNA_INFO_INITIALIZER;
    length = uidna_nameToASCII_UTF8(idna, domain, sizeof(domain) - 1, ascii, sizeof(ascii), &info, &error);
    uidna_close(idna);
    require(U_SUCCESS(error) && !info.errors && length == 21 && !std::strcmp(ascii, "xn--bcher-kva.example"), "IDNA data failed");

    const UChar zone[] = { 'A', 's', 'i', 'a', '/', 'T', 'o', 'k', 'y', 'o', 0 };
    UCalendar *calendar = ucal_open(zone, -1, "en_US", UCAL_GREGORIAN, &error);
    require(U_SUCCESS(error) && calendar, "calendar initialization failed");
    ucal_setMillis(calendar, 0, &error);
    int32_t offset = ucal_get(calendar, UCAL_ZONE_OFFSET, &error);
    ucal_close(calendar);
    require(U_SUCCESS(error) && offset == 9 * 60 * 60 * 1000, "timezone data failed");
}

static void check_runtime(void)
{
    std::expected<int, std::string> value = 42;
    require(value.transform([](int number) { return number + 1; }).value() == 43, "expected failed");
    int sum = 0;
    for (int number : std::views::iota(0, 10) | std::views::filter([](int number) { return number % 2 == 0; }))
        sum += number;
    require(sum == 20, "ranges failed");
    bool caught = false;
    try {
        throw std::runtime_error("yozora exception");
    } catch (const std::runtime_error& error) {
        caught = !std::strcmp(error.what(), "yozora exception");
    }
    require(caught, "exception unwind failed");
    double parsed = 0;
    const char text[] = "1.25";
    auto converted = std::from_chars(text, text + sizeof(text) - 1, parsed);
    require(converted.ec == std::errc() && parsed == 1.25, "floating from_chars failed");
    void *aligned = ::operator new(17, std::align_val_t(128));
    bool alignment_ok = (reinterpret_cast<uintptr_t>(aligned) & 127) == 0;
    ::operator delete(aligned, std::align_val_t(128));
    require(alignment_ok, "aligned allocation failed");
    std::random_device random;
    (void)random();

    std::mutex mutex;
    std::condition_variable changed;
    bool finished = false;
    auto start = std::chrono::steady_clock::now();
    std::thread worker([&] {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
        changed.notify_one();
    });
    bool completed;
    {
        std::unique_lock<std::mutex> lock(mutex);
        completed = changed.wait_for(lock, std::chrono::seconds(2), [&] { return finished; });
    }
    worker.join();
    require(completed && std::chrono::steady_clock::now() >= start, "thread, condition variable or clock failed");
    std::barrier rendezvous(2);
    std::atomic<uint64_t> phase = 0;
    std::thread waiter([&] {
        rendezvous.arrive_and_wait();
        phase.wait(0);
    });
    rendezvous.arrive_and_wait();
    phase.store(1);
    phase.notify_all();
    waiter.join();
    require(std::chrono::system_clock::now().time_since_epoch().count() > 0, "calendar clock failed");
#if defined(__APPLE__)
    uint64_t uptime = 0, continuous = 0, cpu = 0;
    size_t memory = 0, page_size = 0;
    require(yozora_darwin_uptime_ns(&uptime) == YOZORA_CLOCK_OK && uptime, "Darwin uptime failed");
    require(yozora_darwin_continuous_ns(&continuous) == YOZORA_CLOCK_OK && continuous, "Darwin continuous clock failed");
    require(yozora_darwin_thread_cpu_us(&cpu) == YOZORA_CLOCK_OK, "Darwin CPU clock failed");
    require(yozora_darwin_memory_bytes(&memory) == YOZORA_CLOCK_OK && memory, "Darwin memory query failed");
    require(yozora_darwin_kernel_page_size(&page_size) == YOZORA_CLOCK_OK && page_size, "Darwin page size query failed");
    unsigned char bytes[32];
    require(yozora_darwin_random_bytes(bytes, sizeof(bytes)) == 0, "Darwin random bytes failed");
    require(yozora_darwin_random_bytes(nullptr, 1) == EINVAL, "Darwin random buffer validation failed");
    void *allocation = yozora_darwin_aligned_alloc(128, 17);
    bool allocation_ok = allocation && !(reinterpret_cast<uintptr_t>(allocation) & 127);
    std::free(allocation);
    require(allocation_ok, "Darwin aligned allocation failed");
    errno = 0;
    require(!yozora_darwin_aligned_alloc(3, 17) && errno == EINVAL, "Darwin alignment validation failed");
    uint8_t uuid[16] = { 0 };
    void (*function)(bool, const char *) = require;
    void *address = nullptr;
    static_assert(sizeof(address) == sizeof(function));
    std::memcpy(&address, &function, sizeof(address));
    require(yozora_darwin_image_uuid(address, uuid, sizeof(uuid)) == 0, "Darwin image UUID failed");
#endif
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: yozora-runtime-probe SCRATCH_DIRECTORY\n");
        return 2;
    }
    char scratch[4096];
    int length = std::snprintf(scratch, sizeof(scratch), "%s/yozora-runtime-XXXXXX", argv[1]);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(scratch) || !mkdtemp(scratch)) {
        std::perror("yozora: cannot create private scratch directory");
        return 1;
    }
    std::filesystem::path file = std::filesystem::path(scratch) / "runtime-probe.txt";
    int status = 0;
    try {
        require(!std::filesystem::exists(file), "scratch file already exists");
        check_runtime();
        check_icu();
        {
            std::ofstream output(file);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << "yozora\n";
            output.close();
        }
        require(std::filesystem::file_size(file) == 6, "filesystem write failed");
        require(std::filesystem::last_write_time(file) <= std::filesystem::file_time_type::clock::now(), "filesystem clock failed");
        std::ifstream input(file);
        std::string text;
        std::getline(input, text);
        require(text == "yozora", "filesystem read failed");
        input.close();
        require(std::filesystem::remove(file), "scratch cleanup failed");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "yozora: %s\n", error.what());
        status = 1;
    }
    std::error_code error;
    std::filesystem::remove(file, error);
    if (error) {
        std::fprintf(stderr, "yozora: scratch file cleanup failed: %s\n", error.message().c_str());
        status = 1;
    }
    std::filesystem::remove(scratch, error);
    if (error) {
        std::fprintf(stderr, "yozora: scratch directory cleanup failed: %s\n", error.message().c_str());
        status = 1;
    }
    if (!status)
        std::puts("runtime: C++23, exceptions, aligned allocation, threads, clocks, filesystem and ICU 78.3 passed");
    return status;
}
