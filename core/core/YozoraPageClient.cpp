#include "config.h"
#include <curl/curl.h>
#include "YozoraCoreInternal.h"
#include "YozoraCoreThread.h"
#include "../yozora_log.h"
#include "YozoraScene.h"
#include "YozoraCookies.h"
#include "YozoraDownloads.h"
#include "YozoraIDB.h"
#include "DatabaseProvider.h"
#include "YozoraStorage.h"
#include "YozoraWebSocketChannel.h"
#include "YozoraAdblock.h"
#include "UserContentController.h"
#if defined(YOZORA_WASM_ENGINE)
#include "YozoraWasm.h"
#include "ScriptController.h"
#include <JavaScriptCore/APICast.h>
#endif
#include "AuthenticationChallenge.h"
#include "AuthenticationClient.h"
#include "CookieJar.h"
#include "DOMWrapperWorld.h"
#include "Document.h"
#include "DocumentPage.h"
#include "DocumentView.h"
#include "DocumentLoader.h"
#include "ColorChooser.h"
#include "ColorChooserClient.h"
#include "DateTimeChooser.h"
#include "DateTimeChooserClient.h"
#include "Editor.h"
#include "EditorClient.h"
#include "EmptyClients.h"
#include "ViewportArguments.h"
#include "EmptyFrameLoaderClient.h"
#include "FileChooser.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "FrameLoaderTypes.h"
#include "FrameTree.h"
#include "ExceptionOr.h"
#include "HTMLFrameOwnerElement.h"
#include "HTMLVideoElement.h"
#include "EventNames.h"
#include "KeyboardEvent.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "MIMETypeRegistry.h"
#include "NodeDocument.h"
#include "PageConfiguration.h"
#include "PlatformKeyboardEvent.h"
#include "PopupMenu.h"
#include "PopupMenuClient.h"
#include "ProgressTracker.h"
#include "ProgressTrackerClient.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SocketProvider.h"
#include "ThreadableWebSocketChannel.h"
#include "WebTransportSession.h"
#include "StorageSessionProvider.h"
#include "Scrollbar.h"
#include "StringWithDirection.h"
#include "TextCheckerClient.h"
#include "WindowsKeyboardCodes.h"
#include <pal/SessionID.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RefCounted.h>

using namespace WebCore;
using namespace WTF::StringLiterals;

class YozoraTextCheckerClient final : public TextCheckerClient {
    bool shouldEraseMarkersAfterChangeSelection(TextCheckingType) const final { return true; }
    void ignoreWordInSpellDocument(const String&) final { }
    void learnWord(const String&) final { }
    void checkSpellingOfString(StringView, int*, int*) final { }
    void checkGrammarOfString(StringView, Vector<GrammarDetail>&, int*, int*) final { }
#if USE(UNIFIED_TEXT_CHECKING)
    Vector<TextCheckingResult> checkTextOfParagraph(StringView, OptionSet<TextCheckingType>, const VisibleSelection&) final { return { }; }
#endif
    void getGuessesForWord(const String&, const String&, const VisibleSelection&, Vector<String>&) final { }
    void requestCheckingOfString(TextCheckingRequest&, const VisibleSelection&) final { }
    void requestExtendedCheckingOfString(TextCheckingRequest&, const VisibleSelection&) final { }
};

// the empty editor client refuses every edit and ignores keys, so no caret moves and no typed text arrives
class YozoraEditorClient final : public EditorClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(YozoraEditorClient);
    // the base class deletes through its own destroying operator delete, which calls the complete destructor
    // of the abstract EditorClient, clang compiles that to a trap on armv7 so destroying a core aborted the
    // process
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(YozoraEditorClient);
public:
    YozoraEditorClient() = default;
private:
    bool shouldDeleteRange(const std::optional<SimpleRange>&) final { return true; }
    bool smartInsertDeleteEnabled() final { return false; }
    bool isSelectTrailingWhitespaceEnabled() const final { return false; }
    bool isContinuousSpellCheckingEnabled() final { return false; }
    void toggleContinuousSpellChecking() final { }
    bool isGrammarCheckingEnabled() final { return false; }
    void toggleGrammarChecking() final { }
    int spellCheckerDocumentTag() final { return -1; }

    bool shouldBeginEditing(const SimpleRange&) final { return true; }
    bool shouldEndEditing(const SimpleRange&) final { return true; }
    bool shouldInsertNode(Node&, const std::optional<SimpleRange>&, EditorInsertAction) final { return true; }
    bool shouldInsertText(const String&, const std::optional<SimpleRange>&, EditorInsertAction) final { return true; }
    bool shouldChangeSelectedRange(const std::optional<SimpleRange>&, const std::optional<SimpleRange>&, Affinity, bool) final { return true; }

    bool shouldApplyStyle(const StyleProperties&, const std::optional<SimpleRange>&) final { return true; }
    void didApplyStyle() final { }
    bool shouldMoveRangeAfterDelete(const SimpleRange&, const SimpleRange&) final { return true; }

    void didBeginEditing() final { }
    void respondToChangedContents() final { }
    void respondToChangedSelection(LocalFrame*) final { }
    void updateEditorStateAfterLayoutIfEditabilityChanged() final { }
    void discardedComposition(const Document&) final { }
    void canceledComposition() final { }
    void didUpdateComposition() final { }
    void didEndEditing() final { }
    void didEndUserTriggeredSelectionChanges() final { }
    void willWriteSelectionToPasteboard(const std::optional<SimpleRange>&) final { }
    void didWriteSelectionToPasteboard() final { }
    void getClientPasteboardData(const std::optional<SimpleRange>&, Vector<std::pair<String, RefPtr<SharedBuffer>>>&) final { }
    void requestCandidatesForSelection(const VisibleSelection&) final { }
    void handleAcceptedCandidateWithSoftSpaces(TextCheckingResult) final { }

    void registerUndoStep(UndoStep&) final { }
    void registerRedoStep(UndoStep&) final { }
    void clearUndoRedoOperations() final { }

    DOMPasteAccessResponse requestDOMPasteAccess(DOMPasteAccessCategory, FrameIdentifier, const String&) final { return DOMPasteAccessResponse::DeniedForGesture; }

    bool canCopyCut(LocalFrame*, bool defaultValue) const final { return defaultValue; }
    bool canPaste(LocalFrame*, bool defaultValue) const final { return defaultValue; }
    bool canUndo() const final { return false; }
    bool canRedo() const final { return false; }

    void undo() final { }
    void redo() final { }

    // keys reach here only when the page did not take them, a text field already turns return into a form submit
    void handleKeyboardEvent(KeyboardEvent& event) final
    {
        auto* key = event.underlyingPlatformEvent();
        RefPtr node = dynamicDowncast<Node>(event.target());
        RefPtr frame = node ? node->document().frame() : nullptr;
        if (!key || !frame)
            return;
        Editor& editor = frame->editor();
        if (!editor.canEdit())
            return;
        if (event.type() == eventNames().keydownEvent) {
            auto* name = key->windowsVirtualKeyCode() == VK_BACK ? "DeleteBackward" : key->windowsVirtualKeyCode() == VK_DELETE ? "DeleteForward" : nullptr;
            if (name && editor.command(String::fromLatin1(name)).execute())
                event.setDefaultHandled();
            return;
        }
        if (event.type() != eventNames().keypressEvent)
            return;
        String text = key->text();
        bool handled = false;
        if (text == "\r"_s)
            handled = editor.command("InsertNewline"_s).execute();
        else if (!text.isEmpty() && text[0] >= ' ')
            handled = editor.insertText(text, &event);
        if (handled)
            event.setDefaultHandled();
    }
    void handleInputMethodKeydown(KeyboardEvent&) final { }

    void textFieldDidBeginEditing(Element&) final { }
    void textFieldDidEndEditing(Element&) final { }
    void textDidChangeInTextField(Element&) final { }
    bool doTextFieldCommandFromEvent(Element&, KeyboardEvent*) final { return false; }
    void textWillBeDeletedInTextField(Element&) final { }
    void textDidChangeInTextArea(Element&) final { }
    void overflowScrollPositionChanged() final { }
    void subFrameScrollPositionChanged() final { }

    TextCheckerClient* textChecker() final { return &m_textChecker; }
    void updateSpellingUIWithGrammarString(const String&, const GrammarDetail&) final { }
    void updateSpellingUIWithMisspelledWord(const String&) final { }
    void showSpellingUI(bool) final { }
    bool spellingUIIsShowing() final { return false; }
    void setInputMethodState(Element*) final { }

    bool performTwoStepDrop(DocumentFragment&, const SimpleRange&, bool) final { return false; }

    YozoraTextCheckerClient m_textChecker;
};

extern "C" const char* yozora_user_agent(void)
{
    return "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1 Yozora/1";
}

// Safari on a Mac as sites that serve a desktop layout expect it: the platform token is frozen at 10_15_7 by Safari itself
static const char* yozora_desktop_user_agent(void)
{
    return "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15 Yozora/1";
}

class YozoraStorageSessionProvider final : public StorageSessionProvider {
public:
    NetworkStorageSession* storageSession() const final { return &YozoraCookies::current(); }
};

class YozoraChromeClient final : public EmptyChromeClient {
public:
    explicit YozoraChromeClient(yozora_core& core) : m_core(core) { }
    RefPtr<Page> createWindow(LocalFrame& frame, const String&, const WindowFeatures&, const NavigationAction& action) final
    {
        if (!action.processingUserGesture()) {
            yozora_core_message(m_core, "Всплывающее окно заблокировано");
            return nullptr;
        }
        auto* child = core_create_popup(m_core, frame);
        return child ? child->page : nullptr;
    }
    void closeWindow() final
    {
        pthread_mutex_lock(&m_core.lock);
        m_core.shared.close_requested = true;
        pthread_mutex_unlock(&m_core.lock);
    }
    // a video plays on the page until the page asks for full screen: the controls button
    // (webkitEnterFullscreen) or the fullscreen api of the video or of a script player's container. the
    // interface player presents itself and tells the core when closed. called for the main document each time
    // its viewport tag is read, also for one with none (width is auto then)
    void dispatchViewportPropertiesDidChange(const ViewportArguments& arguments) const final { core_viewport_changed(m_core, arguments.width); }
    bool supportsVideoFullscreen(HTMLMediaElementEnums::VideoFullscreenMode mode) final { return mode == HTMLMediaElementEnums::VideoFullscreenModeStandard; }
    bool canEnterVideoFullscreen(HTMLVideoElement& video, HTMLMediaElementEnums::VideoFullscreenMode) const final { return yozora_media_can_fullscreen(video); }
    void enterVideoFullscreenForVideoElement(HTMLVideoElement& video, HTMLMediaElementEnums::VideoFullscreenMode, bool) final
    {
        yozora_media_set_fullscreen(m_core, video, true);
        // nothing else reports that the mode changed, the element would wait for it and refuse the next request
        video.didBecomeFullscreenElement();
    }
    void exitVideoFullscreenForVideoElement(HTMLVideoElement& video, CompletionHandler<void(bool)>&& completionHandler) final
    {
        yozora_media_set_fullscreen(m_core, video, false);
        video.didStopBeingFullscreenElement();
        completionHandler(true);
    }
#if ENABLE(FULLSCREEN_API)
    bool supportsFullScreenForElement(const Element& element, bool) final { return yozora_media_can_fullscreen(element); }
    void enterFullScreenForElement(Element& element, HTMLMediaElementEnums::VideoFullscreenMode, CompletionHandler<void(ExceptionOr<void>)>&& willEnterFullscreen, CompletionHandler<bool(bool)>&& didEnterFullscreen) final
    {
        if (!yozora_media_set_fullscreen(m_core, element, true)) {
            willEnterFullscreen(Exception { ExceptionCode::TypeError, "The element holds no video that the player can take."_s });
            didEnterFullscreen(false);
            return;
        }
        willEnterFullscreen({ });
        didEnterFullscreen(true);
    }
    void exitFullScreenForElement(Element* element, CompletionHandler<void()>&& completionHandler) final
    {
        if (element)
            yozora_media_set_fullscreen(m_core, *element, false);
        completionHandler();
    }
#endif
    // a select, a file field and a date field become questions of the interface, EmptyChromeClient would drop them
    RefPtr<PopupMenu> createPopupMenu(PopupMenuClient& client) const override { return yozora_create_popup_menu(m_core, client); }
    RefPtr<ColorChooser> createColorChooser(ColorChooserClient& client, const Color& color) override { return yozora_create_color_chooser(m_core, client, color); }
    RefPtr<DateTimeChooser> createDateTimeChooser(DateTimeChooserClient& client) override { return yozora_create_date_chooser(m_core, client); }
    void runOpenPanel(LocalFrame&, FileChooser& chooser) override { yozora_open_file_panel(m_core, chooser); }
    GraphicsLayerFactory* graphicsLayerFactory() const final { return &yozoraGraphicsLayerFactory(); }
    void attachRootGraphicsLayer(LocalFrame& frame, GraphicsLayer* layer) final
    {
        if (frame.isMainFrame())
            m_core.graphics_root = layer;
        m_core.dirty = true;
    }
    void attachViewOverlayGraphicsLayer(GraphicsLayer*) final { }
    void setNeedsOneShotDrawingSynchronization() final
    {
        m_core.dirty = true;
        YozoraCoreThread::wake();
    }
    void triggerRenderingUpdate() final
    {
        m_core.rendering = true;
        YozoraCoreThread::wake();
    }
    void invalidateContentsAndRootView(const IntRect&) final
    {
        m_core.dirty = true;
        YozoraCoreThread::wake();
    }
    void addMessageToConsole(JSC::MessageSource, JSC::MessageLevel level, const String& message, unsigned line, unsigned, const String& source) final
    {
        const char* names[] = { "log", "warning", "error", "debug", "info" };
        auto text = message.utf8();
        auto where = source.utf8();
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s: %s (%s:%u)", names[std::min<size_t>(static_cast<size_t>(level), 4)], text.data(), where.data(), line);
        yozora_core_console(m_core, buffer);
    }
    void invalidateContentsForSlowScroll(const IntRect&) final
    {
        m_core.dirty = true;
        YozoraCoreThread::wake();
    }
    bool scheduleRenderingUpdate() final
    {
        m_core.rendering = true;
        YozoraCoreThread::wake();
        return true;
    }
private:
    yozora_core& m_core;
};

class YozoraProgressClient final : public ProgressTrackerClient {
public:
    explicit YozoraProgressClient(yozora_core& core) : m_core(core) { }
    void progressStarted(LocalFrame&) final { m_core.progress = 0.05f; }
    void progressEstimateChanged(LocalFrame& frame) final
    {
        if (RefPtr page = frame.page())
            m_core.progress = static_cast<float>(page->progress().estimatedProgress());
    }
    void progressFinished(LocalFrame&) final { m_core.progress = 1; }
private:
    yozora_core& m_core;
};

class YozoraFrameLoaderClient final : public EmptyFrameLoaderClient {
public:
    YozoraFrameLoaderClient(LocalFrame& frame, FrameLoader& loader, yozora_core& core)
        : EmptyFrameLoaderClient(loader), m_frame(frame), m_core(core) { }
    bool hasWebView() const final { return true; }
    bool isEmptyFrameLoaderClient() const final { return false; }
#if defined(YOZORA_WASM_ENGINE)
    // this slice's JavaScriptCore has no WebAssembly object, a fresh page global gets the one of core/wasm.
    // other worlds (the translator) do not
    void dispatchDidClearWindowObjectInWorld(DOMWrapperWorld& world) final
    {
        if (!world.isNormal())
            return;
        if (auto* global = m_frame.script().globalObject(world))
            yozora_wasm_js_install(::toRef(static_cast<JSC::JSGlobalObject*>(global)));
    }
#endif
    bool canHandleRequest(const ResourceRequest& request) const final
    {
        return request.url().protocolIsInHTTPFamily() || request.url().isAboutBlank() || request.url().isAboutSrcDoc();
    }
    bool canShowMIMEType(const String& type) const final
    {
        return MIMETypeRegistry::isSupportedImageMIMEType(type) || MIMETypeRegistry::isSupportedNonImageMIMEType(type);
    }
    bool canShowMIMETypeAsHTML(const String& type) const final { return type == "text/html"_s || type == "application/xhtml+xml"_s; }
    String userAgent(const URL&) const final { return String::fromUTF8(m_core.desktop ? yozora_desktop_user_agent() : yozora_user_agent()); }
    void dispatchDecidePolicyForNavigationAction(const NavigationAction& action, const ResourceRequest& request, const ResourceResponse&, FormState*, const String&, std::optional<NavigationIdentifier>, std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode, FramePolicyFunction&& decision) final
    {
        if (core_offer_external(m_core, action, request, m_frame.isMainFrame(), decision))
            return;
        bool allowed = canHandleRequest(request) && request.url().user().isEmpty() && request.url().password().isEmpty() && request.url().string().length() < 4096;
        if (!allowed)
            yozora_core_message(m_core, "Неподдерживаемый адрес страницы");
        decision(allowed ? PolicyAction::Use : PolicyAction::Ignore);
    }
    void dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&, const String&, FramePolicyFunction&& decision) final
    {
        bool supported = canShowMIMEType(response.mimeType());
        // a file the page offers as an attachment, or one that no viewer here can show, is saved. a frame inside a page only ignores it
        bool download = m_frame.isMainFrame() && (response.isAttachment() || !supported);
        if (download) {
            decision(PolicyAction::Download);
            return;
        }
        if (!supported)
            yozora_core_message(m_core, "Этот тип документа пока не поддерживается");
        decision(supported ? PolicyAction::Use : PolicyAction::Ignore);
    }
    // one view per tab: a link with a target opens here, a script that opens a window without a tap is a
    // popup and is dropped
    void dispatchDecidePolicyForNewWindowAction(const NavigationAction& action, const ResourceRequest& request, FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& decision) final
    {
        decision(PolicyAction::Ignore);
        if (!action.processingUserGesture()) {
            yozora_core_message(m_core, "Всплывающее окно заблокировано");
            return;
        }
        FrameLoadRequest load(m_frame, ResourceRequest(request));
        load.setIsRequestFromClientOrUserInput();
        m_frame.loader().load(WTF::move(load));
    }
    void dispatchDidStartProvisionalLoad() final
    {
        if (!m_frame.isMainFrame())
            return;
        m_core.blocked_on_page = 0;
        if (RefPtr loader = m_frame.loader().provisionalDocumentLoader())
            core_nav_started(m_core, loader->url());
        yozora_core_message(m_core, "Загрузка…");
    }
    void dispatchDidReceiveServerRedirectForProvisionalLoad() final
    {
        if (!m_frame.isMainFrame())
            return;
        if (RefPtr loader = m_frame.loader().provisionalDocumentLoader())
            core_nav_redirected(m_core, m_frame, loader->url());
    }
    // fragment links and the history api change the address without a load
    void dispatchDidChangeLocationWithinPage() final { updateAddress(); }
    void dispatchDidPushStateWithinPage() final { updateAddress(); }
    void dispatchDidReplaceStateWithinPage() final { updateAddress(); }
    void dispatchDidPopStateWithinPage() final { updateAddress(); }
    void dispatchDidCommitLoad(std::optional<HasInsecureContent>, std::optional<UsedLegacyTLS>, std::optional<WasPrivateRelayed>) final
    {
        if (m_frame.isMainFrame()) {
            if (auto* document = m_frame.document()) {
                core_nav_committed(m_core, document->url());
                if (RefPtr page = m_frame.page())
                    YozoraAdblock::documentCommitted(*page, document->url());
            }
        }
        m_core.dirty = true;
    }
    void dispatchDidReceiveTitle(const StringWithDirection& title) final
    {
        if (!m_frame.isMainFrame())
            return;
        core_set_title(m_core, title.string);
        yozora_core_message(m_core, title.string);
    }
    void dispatchDidFailProvisionalLoad(const ResourceError& error, WillContinueLoading, WillInternallyHandleFailure) final { fail(error); }
    void dispatchDidFailLoad(const ResourceError& error) final { fail(error); }
    void dispatchDidFinishLoad() final
    {
        m_core.dirty = true;
        if (!m_frame.isMainFrame())
            return;
        if (m_frame.document() && !m_frame.document()->title().isEmpty())
            core_set_title(m_core, m_frame.document()->title());
        core_nav_finished(m_core);
        yozora_core_message(m_core, "Готово");
    }
    // layout reports what changed through the chrome client, a whole repaint per data chunk would starve the loads
    void committedLoad(DocumentLoader* loader, const SharedBuffer& buffer) final { loader->commitData(buffer); }
    // the document loader finishes itself before this notification
    void finishedLoading(DocumentLoader*) final { }
    void transitionToCommittedForNewPage(InitializingIframe) final
    {
        if (!m_frame.view())
            m_frame.setView(LocalFrameView::create(m_frame, { 1, 1 }));
        m_core.dirty = true;
    }
    void dispatchUnableToImplementPolicy(const ResourceError& error) final { fail(error); }
    void setMainDocumentError(DocumentLoader*, const ResourceError& error) final { if (!error.isNull()) report(error); }
    void dispatchDidFailLoading(DocumentLoader*, ResourceLoaderIdentifier, const ResourceError& error) final { report(error); }
    void dispatchDidReceiveAuthenticationChallenge(DocumentLoader*, ResourceLoaderIdentifier, const AuthenticationChallenge& challenge) final
    {
        yozora_core_message(m_core, "HTTP-аутентификация пока недоступна");
        if (auto* client = challenge.authenticationClient())
            client->receivedCancellation(challenge);
    }
    void dispatchWillSendRequest(DocumentLoader*, ResourceLoaderIdentifier, ResourceRequest& request, const ResourceResponse&) final
    {
        request.setTimeoutInterval(30);
        request.setHTTPUserAgent(userAgent(request.url()));
    }
    void startDownload(const ResourceRequest& request, const String& suggestedName, FromDownloadAttribute) final { beginDownload(request, suggestedName); }
    void convertMainResourceLoadToDownload(DocumentLoader*, const ResourceRequest& request, const ResourceResponse& response) final
    {
        beginDownload(request, response.suggestedFilename());
        core_nav_downloaded(m_core);
    }
    void beginDownload(const ResourceRequest& request, const String& suggestedName)
    {
        if (YozoraDownloads::start(request, suggestedName))
            yozora_core_message(m_core, "Загрузка файла началась");
        else
            yozora_core_message(m_core, "Этот файл не удалось загрузить");
    }
    RefPtr<LocalFrame> createFrame(const AtomString& name, HTMLFrameOwnerElement& owner) final
    {
        RefPtr page = m_frame.page();
        if (!page)
            return nullptr;
        // a nested challenge keeps both its own restrictions and those inherited from its parent
        auto sandbox = owner.sandboxFlags();
        sandbox.add(m_frame.effectiveSandboxFlags());
        auto policy = owner.referrerPolicy();
        if (policy == ReferrerPolicy::EmptyString && m_frame.document())
            policy = m_frame.document()->referrerPolicy();
        Ref frame = LocalFrame::createSubframe(*page, [core = &m_core](LocalFrame& child, FrameLoader& loader) {
            return makeUniqueRefWithoutRefCountedCheck<YozoraFrameLoaderClient>(child, loader, *core);
        }, generateFrameIdentifier(), sandbox, policy, owner, FrameTreeSyncData::create());
        frame->tree().setSpecifiedName(name);
        frame->init();
        // initializing about:blank may run script that removes the iframe
        if (!frame->page() || !frame->tree().parent())
            return nullptr;
        return frame;
    }
private:
    void updateAddress()
    {
        if (!m_frame.isMainFrame())
            return;
        if (auto* document = m_frame.document())
            m_core.url = document->url().string().utf8();
    }
    void fail(const ResourceError& error)
    {
        // WebCore ends the load that became a download with an interruption, the page that was shown stays as it is
        if (m_frame.isMainFrame() && m_core.download_handoff && error.domain() == "YozoraNetwork"_s && error.errorCode() == 14) {
            m_core.download_handoff = false;
            return;
        }
        if (m_frame.isMainFrame() && !error.isCancellation()) {
            yozora_log(YOZORA_LOG_WARN, "nav", "load failed: %s code %d: %s", error.domain().utf8().data(), static_cast<int>(error.errorCode()), error.localizedDescription().utf8().data());
            m_core.page_error = YOZORA_PAGE_ERROR_OTHER;
            if (error.isTimeout())
                m_core.page_error = YOZORA_PAGE_ERROR_TIMEOUT;
            else if (error.domain() == "CurlErrorDomain"_s) {
                switch (error.errorCode()) {
                case CURLE_COULDNT_RESOLVE_HOST: m_core.page_error = YOZORA_PAGE_ERROR_DNS; break;
                case CURLE_COULDNT_CONNECT: m_core.page_error = YOZORA_PAGE_ERROR_CONNECTION; break;
                case CURLE_OPERATION_TIMEDOUT: m_core.page_error = YOZORA_PAGE_ERROR_TIMEOUT; break;
                case CURLE_PEER_FAILED_VERIFICATION:
                case CURLE_SSL_CONNECT_ERROR:
                case CURLE_SSL_CERTPROBLEM: m_core.page_error = YOZORA_PAGE_ERROR_TLS; break;
                case CURLE_RECV_ERROR:
                case CURLE_SEND_ERROR:
                case CURLE_PARTIAL_FILE:
                case CURLE_GOT_NOTHING: m_core.page_error = YOZORA_PAGE_ERROR_INTERRUPTED; break;
                default: break;
                }
            }
        }
        if (m_frame.isMainFrame())
            core_nav_failed(m_core, error.isCancellation());
        report(error);
    }
    void report(const ResourceError& error)
    {
        if (!error.isCancellation())
            yozora_core_message(m_core, error.localizedDescription());
    }
    LocalFrame& m_frame;
    yozora_core& m_core;
};

class YozoraSocketProvider final : public SocketProvider {
public:
    RefPtr<ThreadableWebSocketChannel> createWebSocketChannel(Document& document, WebSocketChannelClient& client, IsInitiatedByDedicatedWorker) final
    {
        return Yozora::createWebSocketChannel(document, client);
    }
    std::pair<RefPtr<WebTransportSession>, Ref<WebTransportSessionPromise>> initializeWebTransportSession(ScriptExecutionContext&, WebTransportSessionClient&, const URL&, const WebTransportOptions&) final
    {
        return { nullptr, WebTransportSessionPromise::createAndReject() };
    }
#if USE(LIBRICE)
    RefPtr<RiceBackend> createRiceBackend(RiceBackendClient&) final { return nullptr; }
#endif
};

PageConfiguration yozora_page_configuration(yozora_core& core)
{
    auto configuration = pageConfigurationWithEmptyClients(PageIdentifier::generate(), PAL::SessionID::defaultSessionID());
    configuration.socketProvider = adoptRef(*new YozoraSocketProvider);
    // the sheet that hides ad slots reaches every document of the page through the provider, the empty one of
    // the default configuration ignores it
    configuration.userContentProvider = UserContentController::create();
    configuration.chromeClient = makeUniqueRef<YozoraChromeClient>(core);
    configuration.cookieJar = CookieJar::create(adoptRef(*new YozoraStorageSessionProvider));
    configuration.storageNamespaceProvider = YozoraStorage::createProvider();
    configuration.databaseProvider = YozoraIDB::provider();
    configuration.editorClient = makeUniqueRef<YozoraEditorClient>();
    configuration.progressTrackerClient = makeUniqueRef<YozoraProgressClient>(core);
    configuration.mainFrameCreationParameters = PageConfiguration::LocalMainFrameCreationParameters {
        CompletionHandler<UniqueRef<LocalFrameLoaderClient>(LocalFrame&, FrameLoader&)> {
            [&core](LocalFrame& frame, FrameLoader& loader) {
                return makeUniqueRefWithoutRefCountedCheck<YozoraFrameLoaderClient>(frame, loader, core);
            }
        }, { }, ReferrerPolicy::StrictOriginWhenCrossOrigin
    };
    return configuration;
}
