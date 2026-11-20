#include "config.h"
#include "YozoraAdblock.h"
#include "YozoraCoreInternal.h"
#include "YozoraSites.h"
#include "DOMWrapperWorld.h"
#include "Document.h"
#include "DocumentPage.h"
#include "LocalFrame.h"
#include "Page.h"
#include "RegistrableDomain.h"
#include "UserContentController.h"
#include "UserStyleSheet.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <pthread.h>
#include <string>
#include <vector>
#include <wtf/text/CString.h>

using namespace WebCore;

namespace {

// one switch for the whole app, the setting belongs to the user and not to a tab. set from the interface
// thread, read on the core thread
std::atomic<bool> blocking { true };
std::atomic<unsigned long long> blocked_total { 0 };

// the sheet that hides ad slots is at most this many bytes: 1000 selectors of at most 512 bytes would be more than a page needs
constexpr size_t hiding_limit = 96 * 1024;

pthread_mutex_t rules_lock = PTHREAD_MUTEX_INITIALIZER;
std::shared_ptr<yozora_filter>& rules()
{
    static std::shared_ptr<yozora_filter> set;
    return set;
}

std::shared_ptr<yozora_filter> build(const char* text, size_t length, yozora_filter_report* report)
{
    std::shared_ptr<yozora_filter> set(yozora_filter_create(), yozora_filter_destroy);
    if (!set)
        return nullptr;
    // the shipped rules are tested lines, a refusal here is a bug of the table and the person's rules still load
    yozora_filter_add_builtin(set.get());
    yozora_filter_add_text(set.get(), text, length, report);
    return set;
}

std::shared_ptr<yozora_filter> current()
{
    pthread_mutex_lock(&rules_lock);
    if (!rules())
        rules() = build("", 0, nullptr);
    auto set = rules();
    pthread_mutex_unlock(&rules_lock);
    return set;
}

bool paused_for(const std::string& host)
{
    return !host.empty() && YozoraSites::getForHost(YOZORA_SITE_ADBLOCK_PAUSED, host) == YOZORA_GRANT_ALLOW;
}

const URL& hiding_sheet_url()
{
    static const URL url { String("yozora-adblock://hiding"_s) };
    return url;
}

}

namespace YozoraAdblock {

yozora_filter_type typeOf(CachedResource::Type type, bool mainFrame)
{
    switch (type) {
    case CachedResource::Type::MainResource: return mainFrame ? YOZORA_FILTER_DOCUMENT : YOZORA_FILTER_SUBDOCUMENT;
    case CachedResource::Type::ImageResource:
    case CachedResource::Type::Icon:
    case CachedResource::Type::SVGDocumentResource: return YOZORA_FILTER_IMAGE;
    case CachedResource::Type::CSSStyleSheet: return YOZORA_FILTER_STYLESHEET;
    case CachedResource::Type::Script: return YOZORA_FILTER_SCRIPT;
    case CachedResource::Type::FontResource:
    case CachedResource::Type::SVGFontResource: return YOZORA_FILTER_FONT;
    case CachedResource::Type::MediaResource: return YOZORA_FILTER_MEDIA;
    // fetch() and XMLHttpRequest load as raw resources, a page cannot tell them apart from here
    case CachedResource::Type::RawResource: return YOZORA_FILTER_XHR;
    case CachedResource::Type::Beacon:
    case CachedResource::Type::Ping: return YOZORA_FILTER_PING;
    default: return YOZORA_FILTER_OTHER;
    }
}

bool blocks(Page* page, yozora_filter_type type, const URL& request, const URL& firstParty)
{
    if (!blocking.load())
        return false;
    auto host = request.host().utf8();
    if (!host.length())
        return false;
    auto top = firstParty.host().utf8();
    std::string page_host(top.data(), top.length());
    if (paused_for(page_host))
        return false;
    auto set = current();
    if (!set)
        return false;
    auto address = request.string().utf8();
    yozora_filter_request query { };
    query.url = address.data();
    query.url_length = address.length();
    query.host = host.data();
    query.host_length = host.length();
    query.page_host = page_host.c_str();
    query.page_host_length = page_host.size();
    query.type = type;
    // a request that has no top document (a worker) is not called third party: only what is certain is held against it
    query.third_party = !page_host.empty() && !RegistrableDomain(firstParty).matches(request) ? 1 : 0;
    if (!yozora_filter_blocks(set.get(), &query))
        return false;
    ++blocked_total;
    if (page) {
        if (auto* core = yozora_core_for_page(page))
            ++core->blocked_on_page;
    }
    if (std::getenv("YOZORA_LOG_REQUESTS"))
        std::fprintf(stderr, "net: blocked  %s\n", host.data());
    return true;
}

bool blocksWebSocket(Document& document, const URL& request)
{
    return blocks(document.page(), YOZORA_FILTER_WEBSOCKET, request, document.firstPartyForCookies());
}

void documentCommitted(Page& page, const URL& document)
{
    auto* core = yozora_core_for_page(&page);
    if (!core)
        return;
    String css;
    auto host = document.host().utf8();
    std::string name(host.data(), host.length());
    if (blocking.load() && !name.empty() && !paused_for(name)) {
        if (auto set = current()) {
            std::vector<char> buffer(hiding_limit);
            size_t length = yozora_filter_hiding_css(set.get(), name.c_str(), name.size(), buffer.data(), buffer.size());
            if (length)
                css = String::fromUTF8(std::span<const char> { buffer.data(), length });
        }
    }
    // the same sites follow each other often, a sheet that is already in place is not made again
    if (css == core->hiding_css)
        return;
    core->hiding_css = css;
    auto& controller = static_cast<UserContentController&>(page.userContentProviderForFrame());
    auto& world = mainThreadNormalWorldSingleton();
    controller.removeUserStyleSheet(world, hiding_sheet_url());
    if (css.isEmpty())
        return;
    controller.addUserStyleSheet(world, makeUnique<UserStyleSheet>(css, hiding_sheet_url(), Vector<String> { }, Vector<String> { },
        UserContentInjectedFrames::InjectInAllFrames, UserContentMatchParentFrame::Never, UserStyleLevel::User), InjectInExistingDocuments);
}

void setEnabled(bool on) { blocking.store(on); }
bool enabled() { return blocking.load(); }
unsigned long long blockedTotal() { return blocked_total.load(); }

}

extern "C" void yozora_set_tracker_blocking(int enabled) { YozoraAdblock::setEnabled(enabled != 0); }
extern "C" int yozora_tracker_blocking(void) { return YozoraAdblock::enabled() ? 1 : 0; }
extern "C" unsigned long long yozora_blocked_requests(void) { return YozoraAdblock::blockedTotal(); }

extern "C" unsigned yozora_core_blocked_on_page(yozora_core* core)
{
    return core ? core->blocked_on_page.load() : 0;
}

extern "C" void yozora_adblock_set_rules(const char* text, size_t length, yozora_adblock_report* report)
{
    yozora_filter_report counted { };
    auto set = build(text ? text : "", text ? length : 0, &counted);
    if (report) {
        report->added = counted.added;
        report->skipped = counted.skipped;
        report->unsupported = counted.unsupported;
        report->invalid = counted.invalid;
        report->dropped = counted.dropped;
    }
    // a set that could not be made leaves the one that works
    if (!set)
        return;
    pthread_mutex_lock(&rules_lock);
    rules() = set;
    pthread_mutex_unlock(&rules_lock);
}

extern "C" void yozora_adblock_set_site_paused(const char* host, int paused)
{
    if (!host || !*host)
        return;
    if (paused)
        YozoraSites::remember(YOZORA_SITE_ADBLOCK_PAUSED, host, YOZORA_GRANT_ALLOW);
    else
        yozora_site_remove(YOZORA_SITE_ADBLOCK_PAUSED, host);
}

extern "C" int yozora_adblock_site_paused(const char* host)
{
    return host && *host && paused_for(host) ? 1 : 0;
}
