/*
 * Copyright (C) 2020 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebContentReader.h"

#if PLATFORM(YOZORA)
#include "Blob.h"
#include "BlobURL.h"
#include "DOMURL.h"
#include "Document.h"
#include "DocumentFragment.h"
#include "Editor.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "Page.h"
#include "Settings.h"
#include "markup.h"
#include <wtf/URL.h>

namespace WebCore {

static String sanitized_markup(const String& string, Document* document)
{
    return sanitizeMarkup(string, document, MSOListQuirks::Disabled, Function<void(DocumentFragment&)> { [](DocumentFragment& fragment) {
        removeSubresourceURLAttributes(fragment, [](const URL& url) {
            return !(url.protocolIsInHTTPFamily() || url.protocolIsData());
        });
    } });
}

bool WebContentReader::readFilePath(const String& path, PresentationSize, const String&)
{
    // clipboard paths never authorize a local file read in the headless port
    UNUSED_PARAM(path);
    return false;
}

bool WebContentReader::readHTML(const String& string)
{
    if (frame().settings().preferMIMETypeForImages() || !frame().document())
        return false;

    auto markup = shouldSanitize() ? sanitized_markup(string, frame().document()) : string;
    addFragment(createFragmentFromMarkup(*protect(frame().document()), markup, emptyString(), { }));
    return true;
}

bool WebContentReader::readPlainText(const String& text)
{
    if (!m_allowPlainText)
        return false;

    addFragment(createFragmentFromText(m_context, text));

    m_madeFragmentFromPlainText = true;
    return true;
}

bool WebContentReader::readImage(Ref<FragmentedSharedBuffer>&& buffer, const String& type, PresentationSize preferredPresentationSize)
{
    ASSERT(frame().document());
    Ref document = *frame().document();
    addFragment(createFragmentForImageAndURL(document, DOMURL::createObjectURL(document, Blob::create(document.ptr(), buffer->extractData(), type)), preferredPresentationSize));

    return m_fragment;
}

bool WebContentReader::readURL(const URL& url, const String& title)
{
    if (!frame().document() || !(url.protocolIsInHTTPFamily() || url.protocolIsData() || url.protocolIsBlob()))
        return false;
    addFragment(createFragmentFromMarkup(*protect(frame().document()), urlToMarkup(url, title), emptyString(), { }));
    return true;
}

bool WebContentMarkupReader::readHTML(const String& string)
{
    if (!frame().document())
        return false;

    m_markup = shouldSanitize() ? sanitized_markup(string, frame().document()) : string;

    return !m_markup.isEmpty();
}

} // namespace WebCore

#endif
