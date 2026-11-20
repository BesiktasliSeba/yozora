#include "yozora_core.h"
#include "yozora_crash.h"
#include "yozora_sampler.h"
#include <skia/core/SkBitmap.h>
#include <skia/core/SkData.h>
#include <skia/core/SkPixmap.h>
#include <skia/encode/SkPngEncoder.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <memory>
#include <vector>
#include <sys/resource.h>
#if defined(__APPLE__)
#include "darwin/yozora_darwin.h"
#endif

// the rows of the page the tiles hold, in points
static void tile_span(const yozora_frame& frame, int& top, int& bottom)
{
    top = 0;
    bottom = 0;
    for (unsigned i = 0; i < frame.count; ++i) {
        int tile_top = frame.tiles[i].document_y;
        int tile_bottom = tile_top + static_cast<int>(std::lround(frame.tiles[i].height / frame.tiles[i].scale));
        top = i ? std::min(top, tile_top) : tile_top;
        bottom = i ? std::max(bottom, tile_bottom) : tile_bottom;
    }
}

// the page from document row top for rows points at the given scale, white where no tile has been painted. a
// tile painted at a lower scale is stretched by repeating its pixels
static SkBitmap compose(const yozora_frame& frame, float scale, int top, int rows)
{
    SkBitmap bitmap;
    unsigned width = frame.count ? static_cast<unsigned>(std::lround(frame.tiles[0].width / frame.tiles[0].scale * scale)) : 1;
    bitmap.allocPixels(SkImageInfo::Make(width, std::max(1, static_cast<int>(rows * scale)), kBGRA_8888_SkColorType, kPremul_SkAlphaType));
    bitmap.eraseColor(SK_ColorWHITE);
    for (unsigned i = 0; i < frame.count; ++i) {
        auto& tile = frame.tiles[i];
        int tile_top = static_cast<int>(std::lround(tile.document_y * scale));
        int tile_rows = static_cast<int>(std::lround(tile.height / tile.scale * scale));
        for (int row = 0; row < tile_rows; ++row) {
            int target = tile_top + row - static_cast<int>(std::lround(top * scale));
            if (target < 0 || target >= bitmap.height())
                continue;
            unsigned source_row = std::min<unsigned>(tile.height - 1, static_cast<unsigned>(row * tile.scale / scale));
            auto* from = reinterpret_cast<const uint32_t*>(tile.pixels + source_row * tile.row_bytes);
            auto* to = static_cast<uint32_t*>(bitmap.getAddr(0, target));
            for (unsigned x = 0; x < width; ++x)
                to[x] = from[std::min<unsigned>(tile.width - 1, static_cast<unsigned>(x * tile.scale / scale))];
        }
    }
    return bitmap;
}

// the rows of [top, top + rows) that no tile holds, what the user sees as a white strip while scrolling
static int uncovered_rows(const yozora_frame& frame, int top, int rows)
{
    int cursor = top;
    int bottom = top + rows;
    std::vector<std::pair<int, int>> spans;
    for (unsigned i = 0; i < frame.count; ++i)
        spans.emplace_back(frame.tiles[i].document_y, frame.tiles[i].document_y + static_cast<int>(std::lround(frame.tiles[i].height / frame.tiles[i].scale)));
    std::sort(spans.begin(), spans.end());
    int missing = 0;
    for (auto& span : spans) {
        if (span.first > cursor)
            missing += std::min(span.first, bottom) - cursor;
        cursor = std::max(cursor, span.second);
        if (cursor >= bottom)
            return missing;
    }
    return missing + bottom - cursor;
}

// rows of [top, top + rows) that a tile holds but nothing was painted into: a row white at three places
// across the width. a page with no white of its own shows this way what layer recording did not reach, which
// tile coverage cannot see because the snapshot fills the strip
static int blank_rows(const yozora_frame& frame, int top, int rows)
{
    int blank = 0;
    for (int y = top; y < top + rows; ++y) {
        bool found = false, white = false;
        for (unsigned i = 0; i < frame.count && !found; ++i) {
            const auto& tile = frame.tiles[i];
            int from = tile.document_y, to = from + static_cast<int>(std::lround(tile.height / tile.scale));
            if (y < from || y >= to)
                continue;
            found = true;
            unsigned row = static_cast<unsigned>(std::lround((y - from) * tile.scale));
            if (row >= tile.height)
                continue;
            const uint32_t* pixels = reinterpret_cast<const uint32_t*>(tile.pixels + row * tile.row_bytes);
            white = pixels[tile.width / 8] == 0xffffffffu && pixels[tile.width / 2] == 0xffffffffu && pixels[tile.width * 7 / 8] == 0xffffffffu;
        }
        if (white || !found)
            ++blank;
    }
    return blank;
}

static long resident_kb()
{
    long kb = -1;
#if defined(__APPLE__)
    size_t bytes = 0;
    auto status = yozora_darwin_memory_bytes(&bytes);
    if (status == YOZORA_CLOCK_OK)
        kb = static_cast<long>(bytes / 1024);
    else
        std::fprintf(stderr, "yozora-render: resident memory query failed: %d\n", static_cast<int>(status));
#elif defined(__linux__)
    if (FILE* status = std::fopen("/proc/self/status", "r")) {
        char line[128];
        while (std::fgets(line, sizeof(line), status)) {
            if (std::sscanf(line, "VmRSS: %ld kB", &kb) == 1)
                break;
        }
        std::fclose(status);
    }
#endif
    return kb;
}

static bool bounded_environment(const char* name, long fallback, long minimum, long maximum, long& result)
{
    result = fallback;
    const char* value = std::getenv(name);
    if (!value)
        return true;
    char* end = nullptr;
    errno = 0;
    result = std::strtol(value, &end, 10);
    if (!errno && end != value && !*end && result >= minimum && result <= maximum)
        return true;
    std::fprintf(stderr, "yozora-render: %s must be between %ld and %ld\n", name, minimum, maximum);
    return false;
}

static bool write_png(const SkBitmap& picture, const char* path)
{
    auto png = SkPngEncoder::Encode(picture.pixmap(), { });
    FILE* output = png ? std::fopen(path, "wb") : nullptr;
    bool written = output && std::fwrite(png->data(), 1, png->size(), output) == png->size();
    if (output && std::fclose(output))
        written = false;
    if (!written)
        std::fprintf(stderr, "yozora-render: PNG output failed: %s\n", path);
    return written;
}

int main(int argc, char** argv)
{
    auto process_started = std::chrono::steady_clock::now();
    YozoraCrash::install();
    if (argc != 5 && argc != 7) {
        std::fputs("usage: yozora-render FONT_DIRECTORY CA_BUNDLE URL OUTPUT.png [WIDTH HEIGHT]\n", stderr);
        return 2;
    }
    unsigned width = argc == 7 ? static_cast<unsigned>(std::atoi(argv[5])) : 320;
    unsigned height = argc == 7 ? static_cast<unsigned>(std::atoi(argv[6])) : 400;
    long settle_ms, timeout_seconds, snapshot_ms;
    if (!bounded_environment("YOZORA_RENDER_SETTLE_MS", 400, 0, 180000, settle_ms)
        || !bounded_environment("YOZORA_RENDER_TIMEOUT", 35, 1, 180, timeout_seconds)
        || !bounded_environment("YOZORA_RENDER_SNAPSHOT_MS", 0, 1000, 180000, snapshot_ms))
        return 2;
    char error[256];
    if (const char* blocking = std::getenv("YOZORA_RENDER_BLOCKING"))
        yozora_set_tracker_blocking(std::atoi(blocking));
    if (const char* cache = std::getenv("YOZORA_RENDER_CACHE")) {
        if (!yozora_set_http_cache(cache, 8 * 1024 * 1024, error, sizeof(error))) {
            std::fprintf(stderr, "yozora-render: %s\n", error);
            return 1;
        }
    }
    // cookies and local storage persist in this directory between runs, it must exist with mode 0700, a
    // second run with the same one shows what the cache saves
    if (const char* bytecode = std::getenv("YOZORA_RENDER_BYTECODE")) {
        if (!yozora_set_bytecode_cache(bytecode, 48 * 1024 * 1024, error, sizeof(error))) {
            std::fprintf(stderr, "yozora-render: %s\n", error);
            return 1;
        }
    }
    if (const char* site_data = std::getenv("YOZORA_RENDER_SITE_DATA")) {
        if (!yozora_set_site_data(site_data, error, sizeof(error))) {
            std::fprintf(stderr, "yozora-render: %s\n", error);
            return 1;
        }
    }
    if (std::getenv("YOZORA_RENDER_ECONOMY"))
        yozora_set_economy_raster(1);
    {
        yozora_device device;
        yozora_device_info(&device);
        std::fprintf(stderr, "yozora-render: device cores %u memory %u MB\n", device.cores, device.memory_mb);
    }
    // "javascript=0,zoom=150,third=1,motion=0,fonts=0,dark=1,webgl=0": the settings a user can change
    if (const char* text = std::getenv("YOZORA_RENDER_OPTIONS")) {
        yozora_options options { 1, 100, 0, 0, 1, 0, 1, 0 };
        std::string list(text);
        for (size_t position = 0; position < list.size();) {
            size_t end = list.find(',', position);
            std::string item = list.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = end == std::string::npos ? list.size() : end + 1;
            int value = 0;
            if (std::sscanf(item.c_str(), "javascript=%d", &value) == 1)
                options.javascript = value;
            else if (std::sscanf(item.c_str(), "zoom=%d", &value) == 1)
                options.text_zoom_percent = value;
            else if (std::sscanf(item.c_str(), "webgl=%d", &value) == 1)
                options.webgl = value;
            else if (std::sscanf(item.c_str(), "third=%d", &value) == 1)
                options.block_third_party_cookies = value;
            else if (std::sscanf(item.c_str(), "motion=%d", &value) == 1)
                options.reduce_motion = value ? 0 : 1;
            else if (std::sscanf(item.c_str(), "fonts=%d", &value) == 1)
                options.web_fonts = value;
            else if (std::sscanf(item.c_str(), "dark=%d", &value) == 1)
                options.dark_appearance = value;
            else {
                std::fprintf(stderr, "yozora-render: bad option %s\n", item.c_str());
                return 2;
            }
        }
        yozora_set_options(&options);
    }
    // YOZORA_RENDER_DOWNLOADS=DIR: files the page hands over are saved there, and the list is printed once they are done
    const char* download_directory = std::getenv("YOZORA_RENDER_DOWNLOADS");
    if (download_directory)
        yozora_downloads_set_directory(download_directory);
    // match the app initialization before UIApplicationMain
    yozora_initialize_javascript();
    auto* core = yozora_core_create(argv[1], argv[2], error, sizeof(error));
    if (!core) {
        std::fprintf(stderr, "yozora-render: %s\n", error);
        return 1;
    }
    // what an earlier run stored is deleted before the page runs, as the clear site data setting does
    if (std::getenv("YOZORA_RENDER_CLEAR_SITE_DATA"))
        yozora_clear_site_data();
    auto initialized = std::chrono::steady_clock::now();
    auto started = initialized;
    float scale = std::getenv("YOZORA_RENDER_SCALE") ? static_cast<float>(std::atof(std::getenv("YOZORA_RENDER_SCALE"))) : 1.0f;
    // YOZORA_RENDER_PROFILE=FILE writes samples of the running threads from the request until the load, scripts and actions are done
    const char* profile_path = std::getenv("YOZORA_RENDER_PROFILE");
    if (profile_path) {
        long interval_us, depth;
        if (!bounded_environment("YOZORA_RENDER_PROFILE_US", 1000, 200, 100000, interval_us)
            || !bounded_environment("YOZORA_RENDER_PROFILE_DEPTH", 160, 1, 160, depth)
            || !YozoraSampler::start(static_cast<unsigned>(interval_us), static_cast<unsigned>(depth)))
            return 2;
    }
    if (!yozora_core_resize(core, width, height, scale) || !yozora_core_load(core, argv[3], error, sizeof(error))) {
        std::fprintf(stderr, "yozora-render: resize or load failed: %s\n", error);
        yozora_core_destroy(core);
        return 1;
    }
    yozora_frame frame { };
    long long busy_ms = 0, longest_ms = 0, ticks = 0;
    long long first_tile_ms = -1;
    bool busy_timeline = std::getenv("YOZORA_RENDER_BUSY") != nullptr;
    std::vector<int> busy_seconds;
    long long busy_last_total = 0;
    long long next_snapshot_ms = snapshot_ms;
    unsigned snapshots = 0;
    bool snapshots_written = true;
    using ProbeWindow = std::unique_ptr<yozora_core, decltype(&yozora_core_destroy)>;
    std::vector<ProbeWindow> windows;
    // YOZORA_RENDER_STOP_SCRIPTS=MS: once the core thread has been away from its passes for MS the script
    // that keeps it is stopped, as the interface does
    long stop_limit_ms = std::getenv("YOZORA_RENDER_STOP_SCRIPTS") ? std::atol(std::getenv("YOZORA_RENDER_STOP_SCRIPTS")) : 0;
    bool stop_done = false;
    // YOZORA_RENDER_AUTH=USER:PASSWORD,USER:PASSWORD,...: answers to the name and password questions in
    // order, "cancel" refuses. questions and answers are printed
    std::vector<std::string> auth_answers;
    if (const char* answers = std::getenv("YOZORA_RENDER_AUTH")) {
        std::string text(answers);
        for (size_t position = 0; position <= text.size();) {
            size_t end = text.find(',', position);
            auth_answers.push_back(text.substr(position, end == std::string::npos ? std::string::npos : end - position));
            position = end == std::string::npos ? text.size() + 1 : end + 1;
        }
    }
    size_t auth_next = 0;
    auto take_frame = [&] {
        yozora_auth_request question;
        while (yozora_core_take_auth(core, &question)) {
            std::string answer = auth_next < auth_answers.size() ? auth_answers[auth_next++] : std::string("cancel");
            std::fprintf(stderr, "yozora-render: auth question host %s port %d realm %s scheme %s secure %d failures %d answer %s\n", question.host, question.port,
                         question.realm, question.scheme, question.secure, question.failures, answer.c_str());
            size_t colon = answer.find(':');
            if (answer == "cancel" || colon == std::string::npos)
                yozora_core_auth_respond(core, question.id, nullptr, nullptr);
            else
                yozora_core_auth_respond(core, question.id, answer.substr(0, colon).c_str(), answer.substr(colon + 1).c_str());
        }
        if (stop_limit_ms > 0 && !stop_done) {
            long long stalled = yozora_core_stalled_ms();
            if (stalled >= stop_limit_ms) {
                std::fprintf(stderr, "yozora-render: core thread away for %lld ms, stop scripts returns %d\n", stalled, yozora_stop_scripts());
                stop_done = true;
            }
        }
        if (std::getenv("YOZORA_RENDER_POPUPS")) {
            auto collect = [&](yozora_core* parent) {
                if (auto* popup = yozora_core_take_popup(parent)) {
                    windows.emplace_back(popup, yozora_core_destroy);
                    yozora_core_resize(popup, width, height, scale);
                }
            };
            collect(core);
            for (size_t index = 0; index < windows.size();) {
                auto* popup = windows[index].get();
                collect(popup);
                yozora_frame discarded;
                yozora_core_tick(popup, &discarded);
                if (yozora_core_close_requested(popup)) {
                    std::fprintf(stderr, "yozora-render: popup closed %s\n", yozora_core_url(popup));
                    windows.erase(windows.begin() + index);
                } else
                    ++index;
            }
        }
        yozora_frame next;
        if (yozora_core_tick(core, &next))
            frame = next;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        // the share of each second the core thread worked, to tell a page that is still busy from one that waits for the network
        if (busy_timeline && elapsed_ms >= static_cast<long long>(busy_seconds.size() + 1) * 1000) {
            yozora_core_stats current;
            yozora_core_read_stats(core, &current);
            long long total = current.events_ms + current.rendering_ms + current.layout_ms + current.paint_ms;
            busy_seconds.push_back(static_cast<int>((total - busy_last_total) / 10));
            busy_last_total = total;
        }
        if (first_tile_ms < 0 && frame.count)
            first_tile_ms = elapsed_ms;
        if (snapshot_ms && snapshots < 180 && frame.count && elapsed_ms >= next_snapshot_ms) {
            std::string path = std::string(argv[4]) + ".at-" + std::to_string(elapsed_ms) + ".png";
            auto picture = compose(frame, scale, yozora_core_scroll_y(core), static_cast<int>(height));
            snapshots_written = write_png(picture, path.c_str()) && snapshots_written;
            std::fprintf(stderr, "yozora-render: snapshot at_ms=%lld resident_kb=%ld path=%s\n",
                         static_cast<long long>(elapsed_ms), resident_kb(), path.c_str());
            ++snapshots;
            next_snapshot_ms = elapsed_ms + snapshot_ms;
        }
    };
    auto pump = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
        while (yozora_core_state(core) == YOZORA_LOAD_LOADING && std::chrono::steady_clock::now() < deadline) {
            auto tick_started = std::chrono::steady_clock::now();
            take_frame();
            auto tick_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tick_started).count();
            busy_ms += tick_ms;
            ++ticks;
            if (tick_ms > longest_ms)
                longest_ms = tick_ms;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    pump();
    auto initial_load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    auto initial_load_state = yozora_core_state(core);
    // a second page and then back to the first, for the history load path
    if (const char* other = std::getenv("YOZORA_RENDER_BACK_VIA")) {
        if (!yozora_core_load(core, other, error, sizeof(error))) {
            std::fprintf(stderr, "yozora-render: second load failed: %s\n", error);
            yozora_core_destroy(core);
            return 1;
        }
        pump();
        if (!yozora_core_back(core)) {
            std::fputs("yozora-render: back failed\n", stderr);
            yozora_core_destroy(core);
            return 1;
        }
        pump();
    }
    // the changes that arrived last are rendered by the ticks after the load completed
    auto settle = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(settle_ms);
        // a script may navigate after the first load event, keep observing it for the same bounded settle interval
        while (std::chrono::steady_clock::now() < deadline && yozora_core_state(core) != YOZORA_LOAD_FAILED && yozora_core_state(core) != YOZORA_LOAD_IDLE) {
            take_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    settle();
    // the script that spins can start while the page still loads, so the stall is watched every frame, once
    // it is stopped the request is checked to be dropped when nothing runs
    if (stop_limit_ms > 0) {
        if (!stop_done)
            std::fputs("yozora-render: core thread never stalled\n", stderr);
        for (int wait = 0; wait < 60; ++wait) {
            take_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        std::fprintf(stderr, "yozora-render: stop scripts with nothing running returns %d, stalled %lld ms\n", yozora_stop_scripts(), yozora_core_stalled_ms());
    }
    // actions separated by semicolons, a backslash n in typed text is the return key
    if (const char* script = std::getenv("YOZORA_RENDER_ACTIONS")) {
        std::string actions(script);
        size_t position = 0;
        while (position <= actions.size()) {
            size_t end = actions.find(';', position);
            std::string action = actions.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = end == std::string::npos ? actions.size() + 1 : end + 1;
            if (action.compare(0, 4, "tap:") == 0) {
                int x = 0, y = 0;
                if (std::sscanf(action.c_str() + 4, "%d,%d", &x, &y) != 2) {
                    std::fprintf(stderr, "yozora-render: bad action %s\n", action.c_str());
                    return 2;
                }
                yozora_core_tap(core, x, y);
            } else if (action.compare(0, 5, "hold:") == 0) {
                // a finger held at the point until the long press fires, then lifted: what the long press recognizer of the interface does
                int x = 0, y = 0;
                if (std::sscanf(action.c_str() + 5, "%d,%d", &x, &y) != 2) {
                    std::fprintf(stderr, "yozora-render: bad action %s\n", action.c_str());
                    return 2;
                }
                yozora_core_long_press(core, x, y);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                yozora_core_long_press_release(core, x, y);
                yozora_hit hit;
                if (yozora_core_take_hit(core, &hit))
                    std::fprintf(stderr, "yozora-render: long press sheet link %s image %s\n", hit.link, hit.image);
            } else if (action.compare(0, 8, "geo-ask:") == 0) {
                // answers the oldest question of a page about the position: geo-ask:1 allows, geo-ask:0 refuses
                yozora_geolocation_request request;
                if (!yozora_core_take_geolocation_request(core, &request)) {
                    std::fputs("yozora-render: no geolocation question\n", stderr);
                    return 2;
                }
                std::fprintf(stderr, "yozora-render: geolocation question %u from %s\n", request.id, request.origin);
                yozora_core_geolocation_answer(core, request.id, std::atoi(action.c_str() + 8));
            } else if (action.compare(0, 8, "geo-pos:") == 0) {
                double v[3];
                if (std::sscanf(action.c_str() + 8, "%lf,%lf,%lf", &v[0], &v[1], &v[2]) != 3) {
                    std::fprintf(stderr, "yozora-render: bad action %s\n", action.c_str());
                    return 2;
                }
                int accuracy = 0;
                std::fprintf(stderr, "yozora-render: geolocation wanted %d\n", yozora_core_geolocation_wanted(core, &accuracy));
                double nothing = std::nan("");
                yozora_core_geolocation_position(core, v[0], v[1], v[2], nothing, nothing, nothing, nothing, static_cast<double>(std::time(nullptr)) * 1000.0);
            } else if (action.compare(0, 10, "geo-error:") == 0) {
                yozora_core_geolocation_error(core, std::atoi(action.c_str() + 10), "the test refused");
            } else if (action.compare(0, 5, "wait:") == 0) {
                // the page keeps running while the core thread is ticked, for what a gesture started and the next action reads
                long wait = std::atol(action.c_str() + 5);
                auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0L, std::min(wait, 60000L)));
                while (std::chrono::steady_clock::now() < until) {
                    yozora_frame next;
                    if (yozora_core_tick(core, &next))
                        frame = next;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            } else if (action.compare(0, 5, "type:") == 0) {
                std::string text = action.substr(5);
                for (size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n"))
                    text.replace(at, 2, "\n");
                if (!yozora_core_insert_text(core, text.c_str()))
                    std::fputs("yozora-render: nothing accepts text\n", stderr);
            } else if (action.compare(0, 6, "media:") == 0) {
                unsigned id = 0;
                int kind = 0;
                double value = 0, extra = 0;
                if (std::sscanf(action.c_str() + 6, "%u,%d,%lf,%lf", &id, &kind, &value, &extra) < 2) {
                    std::fprintf(stderr, "yozora-render: bad action %s\n", action.c_str());
                    return 2;
                }
                yozora_core_media_event(core, id, static_cast<yozora_media_event_kind>(kind), value, extra);
            } else if (action == "suspend" || action == "resume") {
                int accepted = yozora_core_set_suspended(core, action == "suspend");
                std::fprintf(stderr, "yozora-render: %s accepted %d title %s\n", action.c_str(), accepted, yozora_core_title(core));
            } else if (action.compare(0, 4, "tab:") == 0) {
                std::unique_ptr<yozora_core, decltype(&yozora_core_destroy)> peer(
                    yozora_core_create(argv[1], argv[2], error, sizeof(error)), yozora_core_destroy);
                if (!peer || !yozora_core_resize(peer.get(), width, height, scale)
                    || !yozora_core_load(peer.get(), action.c_str() + 4, error, sizeof(error))) {
                    std::fprintf(stderr, "yozora-render: second tab failed: %s\n", error);
                    return 1;
                }
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
                while ((yozora_core_state(peer.get()) == YOZORA_LOAD_LOADING || yozora_core_painting(peer.get()))
                    && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (yozora_core_state(peer.get()) != YOZORA_LOAD_COMPLETE || yozora_core_painting(peer.get())) {
                    std::fprintf(stderr, "yozora-render: second tab did not finish: %s\n", yozora_core_status(peer.get()));
                    return 1;
                }
                yozora_core_stats peer_stats;
                yozora_core_read_stats(peer.get(), &peer_stats);
                std::fprintf(stderr, "yozora-render: second tab title %s passes %lld\n", yozora_core_title(peer.get()), peer_stats.ticks);
                if (!yozora_core_set_suspended(peer.get(), 1)) {
                    std::fputs("yozora-render: second tab could not suspend before teardown\n", stderr);
                    return 1;
                }
            } else if (action == "economy:0" || action == "economy:1") {
                yozora_set_economy_raster(action.back() == '1');
            } else if (action == "resize") {
                if (!yozora_core_resize(core, width, height, scale))
                    return 2;
            } else if (action == "delete") {
                if (!yozora_core_delete_backward(core))
                    std::fputs("yozora-render: nothing accepts text\n", stderr);
            } else {
                std::fprintf(stderr, "yozora-render: bad action %s\n", action.c_str());
                return 2;
            }
            // a form submit or a script navigation starts from a timer, so the load is awaited after a short run of ticks
            auto quiet = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            while (std::chrono::steady_clock::now() < quiet) {
                yozora_frame next;
                if (yozora_core_tick(core, &next))
                    frame = next;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            yozora_media_request media;
            while (yozora_core_take_media(core, &media))
                std::fprintf(stderr, "yozora-render: media command %d id %u video %d value %g muted %d url %s title %s\n",
                             static_cast<int>(media.command), media.id, media.video, media.value, media.muted, media.url, media.title);
            yozora_video videos[YOZORA_MAX_VIDEOS];
            int video_count = yozora_core_videos(core, videos, YOZORA_MAX_VIDEOS);
            for (int index = 0; index < video_count; ++index)
                std::fprintf(stderr, "yozora-render: video id %u at %d,%d size %dx%d\n", videos[index].id, videos[index].x, videos[index].y, videos[index].width, videos[index].height);
            // the page thread answers asynchronously, so what the action did is read once it has had its turn
            std::fprintf(stderr, "yozora-render: input kind %d after %s\n", static_cast<int>(yozora_core_input_kind(core)), action.c_str());
            pump();
            settle();
            std::fprintf(stderr, "yozora-render: after %s title %s\n", action.c_str(), yozora_core_title(core));
        }
        std::fprintf(stderr, "yozora-render: title %s\n", yozora_core_title(core));
    }
    if (download_directory) {
        yozora_download list[8];
        int count = 0;
        auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        for (;;) {
            take_frame();
            count = yozora_downloads_list(list, 8);
            bool busy = false;
            for (int i = 0; i < count; ++i)
                busy = busy || list[i].state == YOZORA_DOWNLOAD_QUEUED || list[i].state == YOZORA_DOWNLOAD_RUNNING;
            if ((!busy && count) || std::chrono::steady_clock::now() > give_up)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        std::fprintf(stderr, "yozora-render: downloads %d\n", count);
        for (int i = 0; i < count; ++i)
            std::fprintf(stderr, "yozora-render: download state %d received %lld total %lld name %s error %s\n", list[i].state, list[i].received, list[i].total, list[i].name, list[i].error);
    }
    // "word:X,Y", "range:X,Y,X,Y", "all" and "clear" separated by semicolons: what the user selects, the text
    // and the ends are printed after each
    if (const char* steps = std::getenv("YOZORA_RENDER_SELECT")) {
        std::string text(steps);
        for (size_t position = 0; position <= text.size();) {
            size_t end = text.find(';', position);
            std::string step = text.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = end == std::string::npos ? text.size() + 1 : end + 1;
            int a = 0, b = 0, c = 0, d = 0;
            if (std::sscanf(step.c_str(), "word:%d,%d", &a, &b) == 2)
                yozora_core_select_word(core, a, b);
            else if (std::sscanf(step.c_str(), "range:%d,%d,%d,%d", &a, &b, &c, &d) == 4)
                yozora_core_select_range(core, a, b, c, d);
            else if (step == "all")
                yozora_core_select_all(core);
            else if (step == "clear")
                yozora_core_select_clear(core);
            else {
                std::fprintf(stderr, "yozora-render: bad selection step %s\n", step.c_str());
                return 2;
            }
            for (int wait = 0; wait < 12; ++wait) {
                take_frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            yozora_selection selection;
            int active = yozora_core_selection(core, &selection);
            std::string shown;
            for (const char* at = selection.text; *at; ++at)
                shown += *at == '\n' ? std::string("|") : std::string(1, *at);
            std::fprintf(stderr, "yozora-render: selection %s active %d start %d,%d,%d end %d,%d,%d text %s\n", step.c_str(), active,
                         selection.start_x, selection.start_y, selection.start_height, selection.end_x, selection.end_y, selection.end_height, shown.c_str());
        }
    }
    // "X,Y,DX,DY" drags at a point of the view, separated by semicolons: a block under the point takes what
    // it can and the page the rest
    if (const char* drags = std::getenv("YOZORA_RENDER_INNER")) {
        std::string text(drags);
        for (size_t position = 0; position <= text.size();) {
            size_t end = text.find(';', position);
            std::string drag = text.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = end == std::string::npos ? text.size() + 1 : end + 1;
            int x = 0, y = 0, dx = 0, dy = 0;
            if (std::sscanf(drag.c_str(), "%d,%d,%d,%d", &x, &y, &dx, &dy) != 4) {
                std::fprintf(stderr, "yozora-render: bad inner drag %s\n", drag.c_str());
                return 2;
            }
            int rest_x = 0, rest_y = 0;
            int taken = yozora_core_scroll_inner(core, x, y, dx, dy, &rest_x, &rest_y);
            int page_moved = rest_y ? yozora_core_scroll(core, rest_y) : 0;
            std::fprintf(stderr, "yozora-render: inner scroll %d,%d by %d,%d taken %d rest %d,%d page moved %d\n", x, y, dx, dy, taken, rest_x, rest_y, page_moved);
            for (int wait = 0; wait < 12; ++wait) {
                take_frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
    }
    // "X,Y,STEPS,DY": a finger goes down at X,Y, moves STEPS times by DY points and lifts, delivered as page
    // touch events. the page's claim (preventDefault) is printed at the end of the gesture
    if (const char* touch = std::getenv("YOZORA_RENDER_TOUCH")) {
        int x = 0, y = 0, steps = 0, dy = 0;
        if (std::sscanf(touch, "%d,%d,%d,%d", &x, &y, &steps, &dy) != 4) {
            std::fprintf(stderr, "yozora-render: bad touch %s\n", touch);
            return 2;
        }
        yozora_core_touch(core, 0, x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        for (int step = 1; step <= steps; ++step) {
            yozora_core_touch(core, 1, x, y + step * dy);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            yozora_frame next;
            if (yozora_core_tick(core, &next))
                frame = next;
        }
        std::fprintf(stderr, "yozora-render: touch claimed %d\n", yozora_core_touch_claimed(core));
        yozora_core_touch(core, 2, x, y + steps * dy);
        for (int wait = 0; wait < 20; ++wait) {
            yozora_frame next;
            if (yozora_core_tick(core, &next))
                frame = next;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    // "STEPS,DY" drags of STEPS frames of DY points each, separated by semicolons, the paint time of the exposed strips is reported
    if (const char* drags = std::getenv("YOZORA_RENDER_SCROLL")) {
        std::string text(drags);
        yozora_core_stats before, after;
        yozora_core_read_stats(core, &before);
        int total_steps = 0, gap_frames = 0, worst_gap = 0;
        std::string gap_steps;
        int scroll_frame_ms = std::getenv("YOZORA_RENDER_SCROLL_FRAME_MS") ? std::max(1, std::atoi(std::getenv("YOZORA_RENDER_SCROLL_FRAME_MS"))) : 40;
        for (size_t position = 0; position <= text.size();) {
            size_t end = text.find(';', position);
            std::string drag = text.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = end == std::string::npos ? text.size() + 1 : end + 1;
            int steps = 0, dy = 0;
            if (std::sscanf(drag.c_str(), "%d,%d", &steps, &dy) != 2) {
                std::fprintf(stderr, "yozora-render: bad drag %s\n", drag.c_str());
                return 2;
            }
            for (int step = 0; step < steps; ++step, ++total_steps) {
                yozora_core_scroll(core, dy);
                std::this_thread::sleep_for(std::chrono::milliseconds(scroll_frame_ms));
                yozora_frame next;
                if (yozora_core_tick(core, &next))
                    frame = next;
                // what the interface would show right now: the newest tiles at the position the finger asked for
                int missing = uncovered_rows(frame, yozora_core_scroll_y(core), static_cast<int>(height));
                // with YOZORA_RENDER_BLANK set the page paints no white, so a white row is one the layers did not record
                if (std::getenv("YOZORA_RENDER_BLANK"))
                    missing = std::max(missing, blank_rows(frame, yozora_core_scroll_y(core), static_cast<int>(height)));
                if (missing) {
                    ++gap_frames;
                    worst_gap = std::max(worst_gap, missing);
                    gap_steps += " " + std::to_string(total_steps) + ":" + std::to_string(missing);
                }
            }
        }
        // the last picture may still be on its way
        for (int wait = 0; wait < 10; ++wait) {
            yozora_frame next;
            if (yozora_core_tick(core, &next))
                frame = next;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        yozora_core_read_stats(core, &after);
        std::fprintf(stderr, "yozora-render: scroll %d frames: paint %lld ms in %lld paints, picture at %d\n", total_steps,
                     after.paint_ms - before.paint_ms, after.paints - before.paints, yozora_core_scroll_y(core));
        std::fprintf(stderr, "yozora-render: scroll gaps %d of %d frames showed untiled rows, worst %d points (frame:rows%s)\n", gap_frames, total_steps, worst_gap, gap_steps.c_str());
    }
    // the interface calls while the page runs a long script: none may wait for it and the scroll must still be taken
    if (std::getenv("YOZORA_RENDER_RESPONSIVE")) {
        // the page starts its long script from a tap at the top left corner
        yozora_core_tap(core, 5, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        long long longest_call_us = 0;
        int frames = 0, moved_total = 0, first_frame = -1;
        int pictures_while_busy = 0, ended_step = -1;
        bool busy_started = false, busy_ended = false;
        auto timed = [&](auto&& call) {
            auto began = std::chrono::steady_clock::now();
            call();
            auto took = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count();
            if (took > longest_call_us)
                longest_call_us = took;
        };
        for (int step = 0; step < 150; ++step) {
            char line[512];
            while (yozora_core_take_console(core, line, sizeof(line))) {
                if (std::strstr(line, "responsive-busy-start"))
                    busy_started = true;
                if (std::strstr(line, "responsive-busy-end")) {
                    busy_ended = true;
                    ended_step = step;
                }
                std::fprintf(stderr, "console: %s\n", line);
            }
            timed([&] { moved_total += yozora_core_scroll(core, 4); });
            yozora_frame next;
            timed([&] {
                if (yozora_core_tick(core, &next)) {
                    frame = next;
                    ++frames;
                    if (busy_started && !busy_ended)
                        ++pictures_while_busy;
                    if (first_frame < 0)
                        first_frame = step;
                }
            });
            timed([&] { yozora_core_state(core); yozora_core_url(core); yozora_core_input_kind(core); });
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        std::fprintf(stderr, "yozora-render: responsive longest call %lld us, first picture at step %d of 150, scroll requested %d\n", longest_call_us, first_frame, yozora_core_scroll_y(core));
        std::fprintf(stderr, "yozora-render: responsive script started %d ended at step %d, %d pictures while busy\n",
            busy_started, ended_step, pictures_while_busy);
        // after the script ends the page catches up and the picture shows the requested position
        for (int wait = 0; wait < 100; ++wait) {
            yozora_frame next;
            if (yozora_core_tick(core, &next))
                frame = next;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        int top = 0, bottom = 0;
        tile_span(frame, top, bottom);
        std::fprintf(stderr, "yozora-render: tiles cover rows %d to %d\n", top, bottom);
    }
    if (std::getenv("YOZORA_RENDER_RELEASE")) {
        long before = resident_kb();
        yozora_core_release_memory(core);
        std::fprintf(stderr, "yozora-render: memory release %ld kB -> %ld kB resident\n", before, resident_kb());
    }
    if (busy_timeline) {
        std::fprintf(stderr, "yozora-render: busy percent per second:");
        for (int percent : busy_seconds)
            std::fprintf(stderr, " %d", percent);
        std::fputc('\n', stderr);
    }
    if (profile_path && !YozoraSampler::stop(profile_path))
        return 1;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::fprintf(stderr, "yozora-render: %lld ms total after request, %lld ticks, %lld ms inside ticks, longest %lld ms\n",
                 static_cast<long long>(elapsed), ticks, busy_ms, longest_ms);
    auto startup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(initialized - process_started).count();
    std::fprintf(stderr, "yozora-render: timing startup_ms=%lld load_ms=%lld first_tile_ms=%lld initial_state=%d\n",
                 static_cast<long long>(startup_ms), static_cast<long long>(initial_load_ms), first_tile_ms, static_cast<int>(initial_load_state));
    struct rusage usage { };
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        long long peak_kb = usage.ru_maxrss;
#if defined(__APPLE__)
        peak_kb /= 1024;
#endif
        long long user_ms = usage.ru_utime.tv_sec * 1000LL + usage.ru_utime.tv_usec / 1000;
        long long system_ms = usage.ru_stime.tv_sec * 1000LL + usage.ru_stime.tv_usec / 1000;
        std::fprintf(stderr, "yozora-render: resources resident_kb=%ld peak_resident_kb=%lld user_ms=%lld system_ms=%lld\n",
                     resident_kb(), peak_kb, user_ms, system_ms);
    } else
        std::fprintf(stderr, "yozora-render: process resource query failed: %s\n", std::strerror(errno));
    yozora_core_stats stats;
    yozora_core_read_stats(core, &stats);
    std::fprintf(stderr, "yozora-render: events %lld ms, %lld rendering updates %lld ms, %lld layouts, layout %lld ms, paint %lld ms in %lld paints\n",
                 stats.events_ms, stats.rendering_updates, stats.rendering_ms, stats.layouts, stats.layout_ms, stats.paint_ms, stats.paints);
    std::fprintf(stderr, "yozora-render: core thread passes %lld, %lld came more than 100 ms after the one before, the longest wait was %lld ms\n", stats.ticks, stats.slow_ticks, stats.longest_tick_gap_ms);
    // a script of the translation library's kind, run in the isolated world after the load:
    // YOZORA_RENDER_SCRIPT is a file and what it evaluates to is printed
    if (const char* script_path = std::getenv("YOZORA_RENDER_SCRIPT")) {
        std::ifstream stream(script_path);
        std::stringstream text;
        text << yozora_translate_script() << "\n" << stream.rdbuf();
        char* result = yozora_core_run_script(core, text.str().c_str());
        std::fprintf(stderr, "yozora-render: script result %s\n", result ? result : "(none)");
        std::free(result);
        // the page is drawn again with what the script changed
        for (int i = 0; i < 20; ++i) {
            yozora_frame next;
            if (yozora_core_tick(core, &next))
                frame = next;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    // what the page printed to its console, for finding out why a site does not run
    char console_line[512];
    while (yozora_core_take_console(core, console_line, sizeof(console_line)))
        std::fprintf(stderr, "console: %s\n", console_line);
    std::fprintf(stderr, "yozora-render: %llu blocked requests\n", yozora_blocked_requests());
    std::fprintf(stderr, "yozora-render: title %s\n", yozora_core_title(core));
    std::fprintf(stderr, "yozora-render: navigation %s back=%d forward=%d\n", yozora_core_url(core),
                 yozora_core_can_go_back(core), yozora_core_can_go_forward(core));
    bool load_complete = yozora_core_state(core) == YOZORA_LOAD_COMPLETE;
    if (yozora_core_state(core) == YOZORA_LOAD_FAILED)
        std::fprintf(stderr, "yozora-render: page error %d\n", static_cast<int>(yozora_core_page_error(core)));
    if (!load_complete)
        std::fprintf(stderr, "yozora-render: %s\n", yozora_core_status(core));
    // scripts and timers of the page run for this long after the load, then the picture is taken
    if (const char* wait_ms = std::getenv("YOZORA_RENDER_WAIT_MS")) {
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::atoi(wait_ms));
        while (std::chrono::steady_clock::now() < until) {
            yozora_frame next;
            if (yozora_core_tick(core, &next))
                frame = next;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    // a page painted at one pixel for each point while it loaded gets its final tiles once the load is over
    for (int wait = 0, quiet = 0; wait < 500 && quiet < 5; ++wait) {
        yozora_frame next;
        if (yozora_core_tick(core, &next))
            frame = next;
        quiet = yozora_core_painting(core) ? 0 : quiet + 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        float lowest = 0, highest = 0;
        for (unsigned i = 0; i < frame.count; ++i) {
            lowest = i ? std::min(lowest, frame.tiles[i].scale) : frame.tiles[i].scale;
            highest = i ? std::max(highest, frame.tiles[i].scale) : frame.tiles[i].scale;
        }
        yozora_core_stats tile_stats;
        yozora_core_read_stats(core, &tile_stats);
        std::fprintf(stderr, "yozora-render: tile scales %g to %g, %lld painted whole, %lld in part\n", lowest, highest, tile_stats.tiles_full, tile_stats.tiles_partial);
    }
    if (!frame.count) {
        yozora_core_destroy(core);
        return 1;
    }
    // the whole span of the tiles, with the rows the interface slides over, instead of only the view
    int top = 0, bottom = 0;
    tile_span(frame, top, bottom);
    std::fprintf(stderr, "yozora-render: tiles %u cover rows %d to %d, view at %d\n", frame.count, top, bottom, yozora_core_scroll_y(core));
    // YOZORA_RENDER_OVERSCAN=1 writes the whole span of the tiles, a larger number only that many points
    // above and below the view, the tiles the pool keeps beyond the reach differ with the way the page was
    // scrolled to its place
    const char* overscan_text = std::getenv("YOZORA_RENDER_OVERSCAN");
    long overscan_points = overscan_text ? std::atol(overscan_text) : 0;
    SkBitmap picture;
    if (overscan_points > 1) {
        int window_top = std::max(0, yozora_core_scroll_y(core) - static_cast<int>(overscan_points));
        picture = compose(frame, scale, window_top, static_cast<int>(height) + 2 * static_cast<int>(overscan_points));
    } else if (overscan_text)
        picture = compose(frame, scale, top, bottom - top);
    else
        picture = compose(frame, scale, yozora_core_scroll_y(core), static_cast<int>(height));
    bool written = write_png(picture, argv[4]) && snapshots_written;
    if (std::getenv("YOZORA_RENDER_BYTECODE")) {
        yozora_flush_bytecode_cache();
        unsigned long long loaded, stale, stored, bytes;
        yozora_bytecode_cache_statistics(&loaded, &stale, &stored, &bytes);
        std::fprintf(stderr, "yozora-render: bytecode cache loaded %llu stale %llu stored %llu files, %llu bytes written\n", loaded, stale, stored, bytes);
    }
    yozora_core_destroy(core);
    yozora_flush_http_cache();
    yozora_flush_site_data();
    if (!written) {
        std::fputs("yozora-render: PNG output failed\n", stderr);
        return 1;
    }
    std::puts(load_complete ? "yozora-render: loaded and painted page" : "yozora-render: saved partial page after incomplete load");
    return load_complete ? 0 : 1;
}
