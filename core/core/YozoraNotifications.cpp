#include "config.h"
#include "YozoraCoreInternal.h"
#include "YozoraCoreThread.h"
#include "YozoraSites.h"
#include "NotificationClient.h"
#include "NotificationController.h"
#include "NotificationData.h"
#include "NotificationPermission.h"
#include "Page.h"
#include "ScriptExecutionContext.h"
#include "SecurityOrigin.h"
#include <algorithm>
#include <cstring>
#include <pthread.h>
#include <wtf/CompletionHandler.h>
#include <wtf/HashMap.h>
#include <wtf/text/CString.h>

using namespace WebCore;

#if ENABLE(NOTIFICATIONS)

namespace {

// the questions and the notifications wait for the interface a bounded time: a page that asks in a loop must not fill the queue
constexpr size_t request_limit = 8;
constexpr size_t notification_limit = 16;

String origin_of(ScriptExecutionContext* context)
{
    if (!context)
        return String();
    auto* origin = context->securityOrigin();
    return origin ? origin->toString() : String();
}

class YozoraNotificationClient final : public NotificationClient {
public:
    explicit YozoraNotificationClient(yozora_core& core)
        : m_core(core)
    {
    }

    // everything below runs on the core thread
    bool show(ScriptExecutionContext& context, NotificationData&& data, RefPtr<NotificationResources>&&, CompletionHandler<void()>&& done) final
    {
        // WebCore checks the permission before it asks the client to show, a client that finds it gone refuses all the same
        if (checkPermission(&context) != Permission::Granted)
            return false;
        yozora_notification notification { };
        copy(notification.origin, sizeof notification.origin, origin_of(&context));
        copy(notification.title, sizeof notification.title, data.title);
        copy(notification.body, sizeof notification.body, data.body);
        copy(notification.tag, sizeof notification.tag, data.tag);
        pthread_mutex_lock(&m_core.lock);
        auto& queue = m_core.shared.notifications;
        // a page that sends more than the interface can show loses its oldest ones
        if (queue.size() >= notification_limit)
            queue.pop_front();
        queue.push_back(notification);
        pthread_mutex_unlock(&m_core.lock);
        m_core.rendering = true;
        done();
        return true;
    }

    void cancel(NotificationData&&) final { }
    void notificationObjectDestroyed(NotificationData&&) final { }

    void notificationControllerDestroyed() final
    {
        // the pages that waited for an answer hear "default", which is what they would read if they had not asked
        auto pending = WTF::move(m_pending);
        for (auto& entry : pending)
            entry.value(Permission::Default);
    }

    void requestPermission(ScriptExecutionContext& context, PermissionHandler&& handler) final
    {
        String origin = origin_of(&context);
        yozora_grant known = origin.isEmpty() ? YOZORA_GRANT_DENY : YozoraSites::get(YOZORA_SITE_NOTIFICATIONS, origin.utf8().data());
        if (known != YOZORA_GRANT_NONE) {
            handler(known == YOZORA_GRANT_ALLOW ? Permission::Granted : Permission::Denied);
            return;
        }
        unsigned id = m_next++;
        yozora_notification_request request { };
        request.id = id;
        copy(request.origin, sizeof request.origin, origin);
        pthread_mutex_lock(&m_core.lock);
        bool queued = m_core.shared.notification_requests.size() < request_limit;
        if (queued)
            m_core.shared.notification_requests.push_back(request);
        pthread_mutex_unlock(&m_core.lock);
        // a queue that is full refuses at once, the page hears a denial and not a silence. the denial is not remembered
        if (!queued) {
            handler(Permission::Denied);
            return;
        }
        m_pending.add(id, WTF::move(handler));
        m_origins.add(id, origin);
        m_core.rendering = true;
    }

    Permission checkPermission(ScriptExecutionContext* context) final
    {
        String origin = origin_of(context);
        if (origin.isEmpty())
            return Permission::Denied;
        yozora_grant found = YozoraSites::get(YOZORA_SITE_NOTIFICATIONS, origin.utf8().data());
        return found == YOZORA_GRANT_NONE ? Permission::Default : found == YOZORA_GRANT_ALLOW ? Permission::Granted : Permission::Denied;
    }

    void answer(unsigned id, bool allow)
    {
        auto handler = m_pending.take(id);
        String origin = m_origins.take(id);
        if (!handler)
            return;
        Permission result = allow ? Permission::Granted : Permission::Denied;
        // the private session keeps nothing: the site asks again
        if (!origin.isEmpty())
            YozoraSites::remember(YOZORA_SITE_NOTIFICATIONS, origin.utf8().data(), allow ? YOZORA_GRANT_ALLOW : YOZORA_GRANT_DENY);
        handler(result);
    }

private:
    static void copy(char* out, size_t capacity, const String& text)
    {
        auto utf8 = text.utf8();
        // cut at a character boundary, a half of a multi byte character is not text
        size_t length = std::min(utf8.length(), capacity - 1);
        while (length && length < utf8.length() && (static_cast<unsigned char>(utf8.data()[length]) & 0xC0) == 0x80)
            --length;
        std::memcpy(out, utf8.data(), length);
        out[length] = '\0';
    }

    yozora_core& m_core;
    unsigned m_next { 1 };
    HashMap<unsigned, PermissionHandler> m_pending;
    HashMap<unsigned, String> m_origins;
};

YozoraNotificationClient* client_of(yozora_core& core)
{
    return static_cast<YozoraNotificationClient*>(core.notification_client.get());
}

}

void yozora_notification_install(yozora_core& core)
{
    auto client = std::make_shared<YozoraNotificationClient>(core);
    provideNotification(core.page.get(), client.get());
    core.notification_client = std::static_pointer_cast<NotificationClient>(client);
}

extern "C" int yozora_core_take_notification_request(yozora_core* core, yozora_notification_request* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.notification_requests.empty();
    if (taken) {
        *out = core->shared.notification_requests.front();
        core->shared.notification_requests.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

extern "C" void yozora_core_notification_answer(yozora_core* core, unsigned id, int allow)
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

extern "C" int yozora_core_take_notification(yozora_core* core, yozora_notification* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.notifications.empty();
    if (taken) {
        *out = core->shared.notifications.front();
        core->shared.notifications.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

#else

void yozora_notification_install(yozora_core&) { }

extern "C" int yozora_core_take_notification_request(yozora_core*, yozora_notification_request*) { return 0; }
extern "C" void yozora_core_notification_answer(yozora_core*, unsigned, int) { }
extern "C" int yozora_core_take_notification(yozora_core*, yozora_notification*) { return 0; }

#endif
