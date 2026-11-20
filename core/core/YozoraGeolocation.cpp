#include "config.h"
#include "YozoraCoreInternal.h"
#include "YozoraCoreThread.h"
#include "YozoraSites.h"
#include "Document.h"
#include "Geolocation.h"
#include "GeolocationClient.h"
#include "GeolocationController.h"
#include "GeolocationError.h"
#include "GeolocationPositionData.h"
#include "Page.h"
#include "SecurityOrigin.h"
#include <cmath>
#include <cstring>
#include <wtf/HashMap.h>
#include <wtf/RefCounted.h>
#include <wtf/text/CString.h>

using namespace WebCore;

namespace {

// the questions wait for the interface a bounded time: a page that asks in a loop must not fill the queue
constexpr size_t request_limit = 16;

class YozoraGeolocationClient final : public GeolocationClient, public RefCounted<YozoraGeolocationClient> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(YozoraGeolocationClient);
public:
    virtual ~YozoraGeolocationClient() = default;

    explicit YozoraGeolocationClient(yozora_core& core)
        : m_core(core)
    {
    }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    // everything below runs on the core thread
    void geolocationDestroyed() final
    {
        m_pending.clear();
        m_origins.clear();
        setWanted(false, false);
    }

    void startUpdating(const String&, bool needsHighAccuracy) final { setWanted(true, needsHighAccuracy); }
    void stopUpdating() final { setWanted(false, false); }
    void setEnableHighAccuracy(bool enable) final
    {
        pthread_mutex_lock(&m_core.lock);
        m_core.shared.geolocation_accuracy = enable;
        pthread_mutex_unlock(&m_core.lock);
    }
    std::optional<GeolocationPositionData> lastPosition() final { return m_last; }

    void requestPermission(Geolocation& geolocation) final
    {
        unsigned id = m_next++;
        yozora_geolocation_request request { };
        request.id = id;
        String origin;
        if (RefPtr document = geolocation.document()) {
            origin = document->securityOrigin().toString();
            auto utf8 = origin.utf8();
            std::snprintf(request.origin, sizeof(request.origin), "%s", utf8.data());
        }
        // a site the person decided for is not asked again, the decision stays until it is taken back in the settings
        yozora_grant known = origin.isEmpty() ? YOZORA_GRANT_NONE : YozoraSites::get(YOZORA_SITE_GEOLOCATION, origin.utf8().data());
        if (known != YOZORA_GRANT_NONE) {
            geolocation.setIsAllowed(known == YOZORA_GRANT_ALLOW, { });
            return;
        }
        pthread_mutex_lock(&m_core.lock);
        bool queued = m_core.shared.geolocation_requests.size() < request_limit;
        if (queued)
            m_core.shared.geolocation_requests.push_back(request);
        pthread_mutex_unlock(&m_core.lock);
        // a queue that is full refuses at once, the page hears a denial and not a silence
        if (!queued) {
            geolocation.setIsAllowed(false, { });
            return;
        }
        m_pending.add(id, Ref { geolocation });
        m_origins.add(id, origin);
    }

    void cancelPermissionRequest(Geolocation& geolocation) final
    {
        m_pending.removeIf([&](auto& entry) {
            bool match = entry.value.ptr() == &geolocation;
            if (match)
                m_origins.remove(entry.key);
            return match;
        });
    }

    void answer(unsigned id, bool allow)
    {
        auto found = m_pending.take(id);
        String origin = m_origins.take(id);
        if (!found)
            return;
        // the private session keeps nothing: the site asks again
        if (!origin.isEmpty())
            YozoraSites::remember(YOZORA_SITE_GEOLOCATION, origin.utf8().data(), allow ? YOZORA_GRANT_ALLOW : YOZORA_GRANT_DENY);
        found->setIsAllowed(allow, { });
    }

    void position(const GeolocationPositionData& data)
    {
        m_last = data;
        if (auto* controller = GeolocationController::from(m_core.page.get()))
            controller->positionChanged(m_last);
    }

    void fail(bool denied, const String& message)
    {
        // a denied position cannot be served from the one that was known before it
        if (denied)
            m_last = std::nullopt;
        auto error = GeolocationError::create(denied ? GeolocationError::PermissionDenied : GeolocationError::PositionUnavailable, message);
        if (auto* controller = GeolocationController::from(m_core.page.get()))
            controller->errorOccurred(error.get());
    }

private:
    void setWanted(bool wanted, bool accuracy)
    {
        pthread_mutex_lock(&m_core.lock);
        m_core.shared.geolocation_wanted = wanted;
        m_core.shared.geolocation_accuracy = accuracy;
        pthread_mutex_unlock(&m_core.lock);
    }

    yozora_core& m_core;
    unsigned m_next { 1 };
    HashMap<unsigned, Ref<Geolocation>> m_pending;
    HashMap<unsigned, String> m_origins;
    std::optional<GeolocationPositionData> m_last;
};

YozoraGeolocationClient* client_of(yozora_core& core)
{
    return static_cast<YozoraGeolocationClient*>(core.geolocation_client.get());
}

std::optional<double> known(double value)
{
    return std::isnan(value) ? std::nullopt : std::optional<double>(value);
}

}

void yozora_geolocation_install(yozora_core& core)
{
    auto client = adoptRef(*new YozoraGeolocationClient(core));
    provideGeolocationTo(core.page.get(), client.get());
    core.geolocation_client = WTF::move(client);
}

extern "C" int yozora_core_take_geolocation_request(yozora_core* core, yozora_geolocation_request* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.geolocation_requests.empty();
    if (taken) {
        *out = core->shared.geolocation_requests.front();
        core->shared.geolocation_requests.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

extern "C" void yozora_core_geolocation_answer(yozora_core* core, unsigned id, int allow)
{
    if (!core)
        return;
    YozoraCoreThread::post([core, id, allow] {
        if (auto* client = client_of(*core))
            client->answer(id, allow != 0);
        core->dirty = true;
        core->rendering = true;
    });
}

extern "C" int yozora_core_geolocation_wanted(yozora_core* core, int* high_accuracy)
{
    if (!core)
        return 0;
    pthread_mutex_lock(&core->lock);
    int wanted = core->shared.geolocation_wanted ? 1 : 0;
    if (high_accuracy)
        *high_accuracy = core->shared.geolocation_accuracy ? 1 : 0;
    pthread_mutex_unlock(&core->lock);
    return wanted;
}

extern "C" void yozora_core_geolocation_position(yozora_core* core, double latitude, double longitude, double accuracy, double altitude, double altitude_accuracy,
    double heading, double speed, double timestamp)
{
    if (!core)
        return;
    // a position outside the globe or without an accuracy is not one the page may be told about
    if (!(latitude >= -90 && latitude <= 90) || !(longitude >= -180 && longitude <= 180) || !(accuracy >= 0))
        return;
    YozoraCoreThread::post([=] {
        if (auto* client = client_of(*core)) {
            client->position(GeolocationPositionData(timestamp, latitude, longitude, accuracy, known(altitude), known(altitude_accuracy), known(heading), known(speed), std::nullopt));
            core->rendering = true;
        }
    });
}

extern "C" void yozora_core_geolocation_error(yozora_core* core, int denied, const char* message)
{
    if (!core)
        return;
    String text = message ? String::fromUTF8(message) : String("Position unavailable"_s);
    YozoraCoreThread::post([core, denied, text = WTF::move(text)] {
        if (auto* client = client_of(*core))
            client->fail(denied != 0, text);
        core->rendering = true;
    });
}
