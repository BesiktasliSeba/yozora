#pragma once

#include "yozora_core.h"
#include "yozora_navigation.h"
#include "IntRect.h"
#include "DOMWrapperWorld.h"
#include "Page.h"
#include "FrameLoaderTypes.h"
#include "VisibleSelection.h"
#include "GeolocationClient.h"
#include "NotificationClient.h"
#include "UserContentProvider.h"
#include "GraphicsLayer.h"
#include "YozoraRaster.h"
#include <atomic>
#include <pthread.h>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>
#include <wtf/MonotonicTime.h>
#include <wtf/CompletionHandler.h>
#include <wtf/RefPtr.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>
#include <memory>

// everything below the core thread comment is touched on that thread only, the shared block is guarded by the lock
struct yozora_core {
    yozora_core() { pthread_mutex_init(&lock, nullptr); }
    ~yozora_core() { pthread_mutex_destroy(&lock); }
    // answers that page controls wait for, by question id. declared before the page so they outlive it, a
    // list the page destroys hides itself and looks here
    unsigned next_control_id { 1 };
    std::unordered_map<unsigned, std::function<void(int)>> choice_waiting;
    std::unordered_map<unsigned, std::function<void(std::vector<int>&&, bool)>> multi_waiting;
    std::unordered_map<unsigned, std::function<void(const char*)>> color_waiting;
    std::unordered_map<unsigned, std::function<void(std::vector<std::string>&&)>> file_waiting;
    std::unordered_map<unsigned, std::function<void(const char*)>> date_waiting;
    // the sheet that hides ad slots of the site, as last given to the page's content provider. core thread
    // only
    WTF::String hiding_css;
    // requests the ad blocker stopped since the main frame began to load, read by the interface
    std::atomic<unsigned> blocked_on_page { 0 };
    RefPtr<WebCore::Page> page;
    // answers to the page for the position of the device, see YozoraGeolocation.cpp. the page lets go of it when it is destroyed
    RefPtr<WebCore::GeolocationClient> geolocation_client;
    std::shared_ptr<WebCore::NotificationClient> notification_client;
    // the world the translation library runs in, made on its first use; destroyed before the page
    RefPtr<WebCore::DOMWrapperWorld> translate_world;
    unsigned popup_depth { 0 };
    // the navigation to an address of another site that waits until the interface says whether an app opened it as a
    // universal link. core thread only; resolved at the next decision, a stop, a new load and the destroy
    WTF::CompletionHandler<void(WebCore::PolicyAction)> external_decision;
    // the page is shown as a desktop browser would: the user agent and the layout width follow it for each new site
    bool desktop { false };
    // the search the find bar runs: its text and the place of the current match, counted by the core
    WTF::String find_text;
    int find_index { 0 };
    int find_count { 0 };
    std::unique_ptr<Yozora::Raster> raster;
    RefPtr<WebCore::GraphicsLayer> graphics_root;
    std::shared_ptr<const Yozora::Scene> recorded_scene;
    uint64_t scene_generation { 0 };
    uint64_t applied_scroll_revision { 0 };
    // the loads that wait for a name and a password, by the number the question carries
    std::unordered_map<unsigned, std::function<void(std::optional<std::pair<WTF::String, WTF::String>>)>> auth_waiting;
    unsigned next_auth_id { 1 };
    // a document was shown at least once, so a load that ends in a download leaves a page on the screen
    bool document_shown { false };
    // the main load was handed to a download, the interruption that WebCore reports for it is no failure
    bool download_handoff { false };
    // the selection whose text the shared copy holds, the text of a large one is read again only when it changes
    std::optional<WebCore::VisibleSelection> published_selection;
    // the zoom of the page layout in percent, set by a pinch, and the site it was set for
    int page_zoom_percent { 100 };
    // the zoom the open document's viewport tag set, 100 while it set none: a document without a tag takes
    // back only a zoom the previous tag made
    int viewport_zoom_percent { 100 };
    WTF::String zoom_host;
    yozora_navigation* navigation { nullptr };
    uint64_t token { 0 };
    // the core already began the navigation for the load that is about to start
    bool begun { false };
    bool committed { false };
    WTF::CString url;
    WTF::CString title;
    char status[256] { };
    yozora_load_state state { YOZORA_LOAD_IDLE };
    yozora_page_error page_error { YOZORA_PAGE_ERROR_OTHER };
    float progress { 0 };
    bool secure { false };
    bool can_go_back { false };
    bool can_go_forward { false };
    bool visit_ready { false };
    yozora_record visit { };
    float scale { 1 };
    unsigned width { 0 };
    unsigned height { 0 };
    // the next frame must repaint the whole view
    yozora_core_stats stats { };
    bool dirty { true };
    bool rendering { true };
    // the last long press found a link or an image, the drag that follows it must not grow a selection
    bool press_on_target { false };
    // the last long press selected nothing and opened no sheet (the finger was on a button, a checkbox or a
    // frame), so the release has to click it
    bool press_passes_through { false };
    // a media element got or lost its stream, or showed its first picture: the video places are published again at the next pass
    bool media_dirty { false };
    bool suspended { false };

    pthread_mutex_t lock;
    struct Shared {
        // ownership passes to the interface when it takes this window
        yozora_core* popup { nullptr };
        bool close_requested { false };
        // a link for another app, until the interface takes it
        bool external_pending { false };
        bool external_universal { false };
        std::string external_url;
        yozora_load_state state { YOZORA_LOAD_IDLE };
        yozora_page_error page_error { YOZORA_PAGE_ERROR_OTHER };
        float progress { 0 };
        bool secure { false };
        bool can_go_back { false };
        bool can_go_forward { false };
        std::string url;
        std::string title;
        std::string status;
        yozora_input_kind input_kind { YOZORA_INPUT_NONE };
        yozora_core_stats stats { };
        std::deque<yozora_record> visits;
        // what media elements asked of the player, bounded so a script that calls play() in a loop cannot grow it
        std::deque<yozora_media_request> media;
        // the places of the videos as of the last layout
        std::vector<yozora_video> videos;
        // what the page wrote to its console and what failed while it loaded, newest last
        std::deque<std::string> console;
        // the scroll position the interface asked for last, which the core thread applies at its next pass
        int scroll_y { 0 };
        int scroll_min { 0 };
        int scroll_max { 0 };
        int page_zoom { 100 };
        bool scroll_pending { false };
        // the page called preventDefault on a touch event of the gesture in progress: the interface must not scroll under the finger
        bool touch_claimed { false };
        uint64_t scroll_revision { 0 };
        // questions for the user that the interface has not taken yet
        std::deque<yozora_auth_request> auth_requests;
        // links and images that a long press found, bounded so a page cannot make the interface fall behind by itself
        std::deque<std::unique_ptr<yozora_hit>> hits;
        // the controls of the page that wait for the person: lists are large, so they are kept by pointer
        std::deque<std::unique_ptr<yozora_choice>> choices;
        std::deque<yozora_file_request> files;
        std::deque<yozora_date_request> dates;
        std::deque<yozora_color_request> colors;
        // questions of pages about the position of the device, and whether a page watches it now
        std::deque<yozora_geolocation_request> geolocation_requests;
        // questions of pages about showing notifications, and the notifications they showed that the interface has not taken yet
        std::deque<yozora_notification_request> notification_requests;
        std::deque<yozora_notification> notifications;
        // the streams that media sources made for the interface, by the id of the player that plays each one, each held once for this table
        std::unordered_map<unsigned, yozora_hls*> mse_streams;
        bool geolocation_wanted { false };
        bool geolocation_accuracy { false };
        bool desktop { false };
        yozora_find_state find { };
        // what the user has selected as of the last pass
        yozora_selection selection { };
        // blocks that scroll on their own as of the last layout, in document points. the movement the
        // interface sent and the core thread has not applied is kept per block so a gesture sees how much
        // room is left
        struct Scroller {
            uint64_t id { 0 };
            int x { 0 }, y { 0 }, width { 0 }, height { 0 };
            int left { 0 }, top { 0 }, max_left { 0 }, max_top { 0 };
            int pending_x { 0 }, pending_y { 0 };
            bool scrolls_x { false }, scrolls_y { false };
        };
        std::vector<Scroller> scrollers;
        int scrollers_scroll_x { 0 };
        int scrollers_scroll_y { 0 };
        struct InnerScroll {
            uint64_t id;
            int dx, dy;
        };
        std::deque<InnerScroll> inner_scrolls;
        // loads the interface asked for and loads the core thread has started, while they differ the state stays loading
        unsigned loads_requested { 0 };
        unsigned loads_started { 0 };

    } shared;
    // the interface thread's copies behind the string getters
    std::string ui_url;
    std::string ui_title;
    std::string ui_status;
};

namespace WebCore {
class Element;
class LocalFrameView;
class NavigationAction;
class ResourceRequest;
}

void yozora_core_console(yozora_core&, const std::string&);
// core thread only: the core has not been destroyed
bool yozora_core_is_live(yozora_core*);
// the roots the translation requests are verified with, the same file the page loads use
void yozora_translate_set_roots(const char*);
// core thread only: gives media elements the player of the interface, once for the process
void yozora_media_install();
// core thread only: finds where the videos of the page are after a layout
void yozora_media_publish(yozora_core&, WebCore::LocalFrameView&);
// core thread only: ends the media ids of a core that is being destroyed
void yozora_media_forget(yozora_core&);
// core thread only: the element is a video with a player of the interface, or holds one, so the full screen player can take it
bool yozora_media_can_fullscreen(const WebCore::Element&);
// core thread only: asks the interface to show (or close) the full screen player of the video; false when the element has none
bool yozora_media_set_fullscreen(yozora_core&, WebCore::Element&, bool on);

namespace WebCore {
class IntPoint;
class LocalFrame;
class ColorChooser;
class ColorChooserClient;
class Color;
class DateTimeChooser;
class DateTimeChooserClient;
class FileChooser;
class PopupMenu;
class PopupMenuClient;
}
// core thread only: page controls. a list, a file field and a date field become questions of the interface
// answered by id
Ref<WebCore::PopupMenu> yozora_create_popup_menu(yozora_core&, WebCore::PopupMenuClient&);
Ref<WebCore::DateTimeChooser> yozora_create_date_chooser(yozora_core&, WebCore::DateTimeChooserClient&);
void yozora_open_file_panel(yozora_core&, WebCore::FileChooser&);
Ref<WebCore::ColorChooser> yozora_create_color_chooser(yozora_core&, WebCore::ColorChooserClient&, const WebCore::Color&);
// core thread only: a tap on a select with multiple opens the interface list instead of reaching the page.
// true when the tap was taken
bool yozora_tap_multiple_select(yozora_core&, WebCore::LocalFrame&, const WebCore::IntPoint&);

void yozora_core_message(yozora_core&, const WTF::String&);
void yozora_core_message(yozora_core&, const char* utf8);
WebCore::PageConfiguration yozora_page_configuration(yozora_core&);
// carries the questions and positions of Geolocation between the page and the interface, installed once the
// page exists
void yozora_geolocation_install(yozora_core&);
void yozora_notification_install(yozora_core&);
// core thread only: the settings of the user, applied to the page again after each commit
void core_apply_options(yozora_core&);
yozora_core* core_create_popup(yozora_core&, WebCore::LocalFrame&);
void core_nav_started(yozora_core&, const WTF::URL&);
// core thread only: hands a followed link to the interface when another app should get it first. true when the
// navigation was taken: dropped for a scheme of another app, held in decision for a universal link
bool core_offer_external(yozora_core&, const WebCore::NavigationAction&, const WebCore::ResourceRequest&, bool main_frame, WTF::CompletionHandler<void(WebCore::PolicyAction)>& decision);
void core_nav_redirected(yozora_core&, WebCore::LocalFrame&, const WTF::URL&);
void core_nav_committed(yozora_core&, const WTF::URL&);
void core_nav_finished(yozora_core&);
void core_viewport_changed(yozora_core&, float width);
void core_nav_failed(yozora_core&, bool cancelled);
// the answer of the load was handed to the download manager: the page before it stays and its state comes back
void core_nav_downloaded(yozora_core&);

// the core that shows the page, null for a page that no core owns. core thread only
yozora_core* yozora_core_for_page(const WebCore::Page*);
// asks the user for a name and a password for a load, the answer comes on the core thread and is empty when
// the user refused
void yozora_core_ask_credentials(yozora_core&, const WTF::String& host, int port, const WTF::String& realm, const WTF::String& scheme, bool secure, unsigned failures,
                                 std::function<void(std::optional<std::pair<WTF::String, WTF::String>>)>&& answer);
void core_set_title(yozora_core&, const WTF::String&);
