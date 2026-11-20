/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "TestPage.h"

#include "Document.h"
#include "DocumentLoader.h"
#include "DocumentWriter.h"
#include "EmptyClients.h"
#include "EditorClient.h"
#include "FrameLoader.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PageConfiguration.h"
#include "ProcessWarming.h"
#include "Settings.h"
#include <pal/SessionID.h>
#include <wtf/StdLibExtras.h>

namespace WebCore {

// editing policy belongs to this fixture, other notifications retain upstream test behavior
class YozoraTestEditorClient final : public EditorClient {
public:
    explicit YozoraTestEditorClient(UniqueRef<EditorClient>&& client)
        : m_client(WTF::move(client))
    { }

    bool shouldDeleteRange(const std::optional<SimpleRange>&) final { return true; }
    bool shouldBeginEditing(const SimpleRange&) final { return true; }
    bool shouldEndEditing(const SimpleRange&) final { return true; }
    bool shouldInsertNode(Node&, const std::optional<SimpleRange>&, EditorInsertAction) final { return true; }
    bool shouldInsertText(const String&, const std::optional<SimpleRange>&, EditorInsertAction) final { return true; }
    bool shouldChangeSelectedRange(const std::optional<SimpleRange>&, const std::optional<SimpleRange>&, Affinity, bool) final { return true; }
    bool shouldApplyStyle(const StyleProperties&, const std::optional<SimpleRange>&) final { return true; }
    bool shouldMoveRangeAfterDelete(const SimpleRange&, const SimpleRange&) final { return true; }

    bool smartInsertDeleteEnabled() final { return m_client->smartInsertDeleteEnabled(); }
    bool isSelectTrailingWhitespaceEnabled() const final { return m_client->isSelectTrailingWhitespaceEnabled(); }
    bool isContinuousSpellCheckingEnabled() final { return m_client->isContinuousSpellCheckingEnabled(); }
    void toggleContinuousSpellChecking() final { m_client->toggleContinuousSpellChecking(); }
    bool isGrammarCheckingEnabled() final { return m_client->isGrammarCheckingEnabled(); }
    void toggleGrammarChecking() final { m_client->toggleGrammarChecking(); }
    int spellCheckerDocumentTag() final { return m_client->spellCheckerDocumentTag(); }
    void didApplyStyle() final { m_client->didApplyStyle(); }
    void didBeginEditing() final { m_client->didBeginEditing(); }
    void didEndEditing() final { m_client->didEndEditing(); }
    void respondToChangedContents() final { m_client->respondToChangedContents(); }
    void respondToChangedSelection(LocalFrame* frame) final { m_client->respondToChangedSelection(frame); }
    void didEndUserTriggeredSelectionChanges() final { m_client->didEndUserTriggeredSelectionChanges(); }
    void updateEditorStateAfterLayoutIfEditabilityChanged() final { m_client->updateEditorStateAfterLayoutIfEditabilityChanged(); }
    void willWriteSelectionToPasteboard(const std::optional<SimpleRange>& range) final { m_client->willWriteSelectionToPasteboard(range); }
    void didWriteSelectionToPasteboard() final { m_client->didWriteSelectionToPasteboard(); }
    void getClientPasteboardData(const std::optional<SimpleRange>& range, Vector<std::pair<String, RefPtr<SharedBuffer>>>& data) final { m_client->getClientPasteboardData(range, data); }
    DOMPasteAccessResponse requestDOMPasteAccess(DOMPasteAccessCategory category, FrameIdentifier frame, const String& origin) final { return m_client->requestDOMPasteAccess(category, frame, origin); }
    void discardedComposition(const Document& document) final { m_client->discardedComposition(document); }
    void canceledComposition() final { m_client->canceledComposition(); }
    void didUpdateComposition() final { m_client->didUpdateComposition(); }
    void registerUndoStep(UndoStep& step) final { m_client->registerUndoStep(step); }
    void registerRedoStep(UndoStep& step) final { m_client->registerRedoStep(step); }
    void clearUndoRedoOperations() final { m_client->clearUndoRedoOperations(); }
    bool canCopyCut(LocalFrame* frame, bool value) const final { return m_client->canCopyCut(frame, value); }
    bool canPaste(LocalFrame* frame, bool value) const final { return m_client->canPaste(frame, value); }
    bool canUndo() const final { return m_client->canUndo(); }
    bool canRedo() const final { return m_client->canRedo(); }
    void undo() final { m_client->undo(); }
    void redo() final { m_client->redo(); }
    void handleKeyboardEvent(KeyboardEvent& event) final { m_client->handleKeyboardEvent(event); }
    void handleInputMethodKeydown(KeyboardEvent& event) final { m_client->handleInputMethodKeydown(event); }
    void textFieldDidBeginEditing(Element& element) final { m_client->textFieldDidBeginEditing(element); }
    void textFieldDidEndEditing(Element& element) final { m_client->textFieldDidEndEditing(element); }
    void textDidChangeInTextField(Element& element) final { m_client->textDidChangeInTextField(element); }
    bool doTextFieldCommandFromEvent(Element& element, KeyboardEvent* event) final { return m_client->doTextFieldCommandFromEvent(element, event); }
    void textWillBeDeletedInTextField(Element& element) final { m_client->textWillBeDeletedInTextField(element); }
    void textDidChangeInTextArea(Element& element) final { m_client->textDidChangeInTextArea(element); }
    void overflowScrollPositionChanged() final { m_client->overflowScrollPositionChanged(); }
    void subFrameScrollPositionChanged() final { m_client->subFrameScrollPositionChanged(); }
    TextCheckerClient* textChecker() final { return m_client->textChecker(); }
    void updateSpellingUIWithGrammarString(const String& text, const GrammarDetail& detail) final { m_client->updateSpellingUIWithGrammarString(text, detail); }
    void updateSpellingUIWithMisspelledWord(const String& text) final { m_client->updateSpellingUIWithMisspelledWord(text); }
    void showSpellingUI(bool show) final { m_client->showSpellingUI(show); }
    bool spellingUIIsShowing() final { return m_client->spellingUIIsShowing(); }
    void setInputMethodState(Element* element) final { m_client->setInputMethodState(element); }
    bool performTwoStepDrop(DocumentFragment& fragment, const SimpleRange& range, bool move) final { return m_client->performTwoStepDrop(fragment, range, move); }
private:
    UniqueRef<EditorClient> m_client;
};

Ref<Page> createTestPage(const TestPageOptions& options)
{
    static bool prewarmed = [] {
        ProcessWarming::prewarmGlobally();
        return true;
    }();
    UNUSED_VARIABLE(prewarmed);

    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    configuration.editorClient = makeUniqueRef<YozoraTestEditorClient>(WTF::move(configuration.editorClient));
    Ref page = Page::create(WTF::move(configuration));

    page->settings().setScriptEnabled(false);
    page->settings().setAcceleratedCompositingEnabled(false);
#if ENABLE(VIDEO)
    page->settings().setMediaEnabled(false);
#endif
    if (options.configureSettings)
        options.configureSettings(page->settings());

    RefPtr frame = page->localMainFrame();
    RELEASE_ASSERT(frame);

    frame->setView(LocalFrameView::create(*frame, options.viewportSize));
    frame->init();

    return page;
}

void loadHTMLIntoTestPage(LocalFrame& frame, const String& html)
{
    RefPtr activeDocumentLoader = frame.loader().activeDocumentLoader();
    ASSERT(activeDocumentLoader);
    auto& writer = activeDocumentLoader->writer();
    writer.setMIMEType("text/html"_s);
    writer.begin();
    writer.insertDataSynchronously(html);
    writer.end();

    if (RefPtr document = frame.document())
        document->updateLayoutIgnorePendingStylesheets();
}

} // namespace WebCore
