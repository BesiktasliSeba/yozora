#include "config.h"
#include "yozora_core.h"
#include "YozoraCoreInternal.h"
#include "YozoraCoreThread.h"
#include "DOMWrapperWorld.h"
#include "CommonVM.h"
#include "JSDOMGlobalObject.h"
#include "LocalFrame.h"
#include "ScriptController.h"
#include <JavaScriptCore/JSCJSValueInlines.h>
#include <JavaScriptCore/JSString.h>
#include <curl/curl.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

using namespace WebCore;

// the library of page translation (core/core/yozora_translate.js), made into a string literal by CMake
static const char translate_library[] =
#include "yozora_translate_script.inc"
;

extern "C" const char* yozora_translate_script(void)
{
    return translate_library;
}

extern "C" char* yozora_core_run_script(yozora_core* core, const char* script)
{
    if (!core || !script)
        return nullptr;
    std::string result;
    bool ok = false;
    String source = String::fromUTF8(script);
    YozoraCoreThread::call([&] {
        // the interface may ask for a core that went away while the request waited its turn
        if (!yozora_core_is_live(core))
            return;
        RefPtr frame = core->page ? core->page->localMainFrame() : nullptr;
        if (!frame)
            return;
        // an isolated world of the page: the page sees none of what the script keeps, and the script sees the document
        if (!core->translate_world)
            core->translate_world = DOMWrapperWorld::create(commonVM(), DOMWrapperWorld::Type::User, "yozora-translate"_s);
        JSC::JSValue value = frame->script().executeUserAgentScriptInWorldIgnoringException(*core->translate_world, source, false);
        if (!value || !value.isString())
            return;
        JSC::JSLockHolder lock(commonVM());
        auto* global = frame->script().globalObject(*core->translate_world);
        if (!global)
            return;
        auto text = value.toWTFString(global).utf8();
        result.assign(text.data(), text.length());
        ok = true;
    });
    if (!ok)
        return nullptr;
    char* copy = static_cast<char*>(std::malloc(result.size() + 1));
    if (!copy)
        return nullptr;
    std::memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}

static std::mutex roots_lock;
static std::string roots_path;

void yozora_translate_set_roots(const char* path)
{
    std::lock_guard<std::mutex> guard(roots_lock);
    roots_path = path ? path : "";
}

namespace {
struct Collected {
    std::string body;
    size_t limit;
    bool overflow;
};

size_t collect(char* data, size_t size, size_t count, void* context)
{
    auto* collected = static_cast<Collected*>(context);
    size_t bytes = size * count;
    if (collected->body.size() + bytes > collected->limit) {
        collected->overflow = true;
        return 0;
    }
    collected->body.append(data, bytes);
    return bytes;
}
}

extern "C" int yozora_http_post(const char* url, const char* content_type, const char* body, size_t length, int timeout_seconds,
                                char** answer, size_t* answer_length, char* error, size_t error_capacity)
{
    if (answer)
        *answer = nullptr;
    if (answer_length)
        *answer_length = 0;
    if (error_capacity)
        error[0] = '\0';
    auto fail = [&](const char* text) {
        if (error_capacity)
            std::snprintf(error, error_capacity, "%s", text);
        return -1;
    };
    if (!url || !body || !answer)
        return fail("no request");
    if (std::strncmp(url, "https://", 8))
        return fail("only https");
    CURL* handle = curl_easy_init();
    if (!handle)
        return fail("curl");
    std::string roots;
    {
        std::lock_guard<std::mutex> guard(roots_lock);
        roots = roots_path;
    }
    Collected collected { std::string(), 4 * 1024 * 1024, false };
    struct curl_slist* headers = nullptr;
    std::string type = std::string("Content-Type: ") + (content_type ? content_type : "application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, type.c_str());
    headers = curl_slist_append(headers, "Accept: */*");
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(length));
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, yozora_user_agent());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &collected);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds > 0 ? timeout_seconds : 20));
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    if (!roots.empty())
        curl_easy_setopt(handle, CURLOPT_CAINFO, roots.c_str());
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    CURLcode code = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    if (code != CURLE_OK) {
        if (error_capacity)
            std::snprintf(error, error_capacity, "%s", collected.overflow ? "the answer is too large" : curl_easy_strerror(code));
        return -1;
    }
    char* copy = static_cast<char*>(std::malloc(collected.body.size() + 1));
    if (!copy)
        return fail("memory");
    std::memcpy(copy, collected.body.c_str(), collected.body.size() + 1);
    *answer = copy;
    if (answer_length)
        *answer_length = collected.body.size();
    return static_cast<int>(status);
}
