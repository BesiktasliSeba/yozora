/*
 * Copyright (C) 2013, 2014 Apple Inc. All rights reserved.
 * Copyright (C) 2014 Igalia S.L.
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
#include "Editor.h"

#if PLATFORM(YOZORA)
#include "CSSPropertyNames.h"
#include "MutableStyleProperties.h"
#include <array>
#include <wtf/NeverDestroyed.h>
#include "CachedImage.h"
#include "ContainerNodeInlines.h"
#include "DocumentFragment.h"
#include "ElementInlines.h"
#include "FrameDestructionObserverInlines.h"
#include "HTMLEmbedElement.h"
#include "HTMLImageElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLObjectElement.h"
#include "LocalFrame.h"
#include "Pasteboard.h"
#include "RenderImage.h"
#include "SVGElementTypeHelpers.h"
#include "SVGImageElement.h"
#include "Settings.h"
#include "WebContentReader.h"
#include "XLinkNames.h"
#include "markup.h"

namespace WebCore {

void Editor::pasteWithPasteboard(Pasteboard* pasteboard, OptionSet<PasteOption> options)
{
    auto range = selectedRange();
    if (!range)
        return;

    bool chosePlainText = false;
    auto fragment = webContentFromPasteboard(*pasteboard, *range, options.contains(PasteOption::AllowPlainText), chosePlainText);

    if (fragment && options.contains(PasteOption::AsQuotation))
        quoteFragmentForPasting(*fragment);

    if (fragment && shouldInsertFragment(*fragment, *range, EditorInsertAction::Pasted))
        pasteAsFragment(fragment.releaseNonNull(), canSmartReplaceWithPasteboard(*pasteboard), chosePlainText, options.contains(PasteOption::IgnoreMailBlockquote) ? MailBlockquoteHandling::IgnoreBlockquote : MailBlockquoteHandling::RespectBlockquote);
}

static NeverDestroyed<RefPtr<MutableStyleProperties>> copied_font;

void Editor::platformCopyFont()
{
    Ref style = MutableStyleProperties::create();
    constexpr auto properties = std::to_array<CSSPropertyID>({ CSSPropertyFontFamily,
        CSSPropertyFontSize, CSSPropertyFontStyle, CSSPropertyFontWeight,
        CSSPropertyFontWidth, CSSPropertyFontVariantCaps });
    for (auto property : properties) {
        auto value = selectionStartCSSPropertyValue(property);
        if (value.length() > 4096) {
            WTFLogAlways("yozora editor: copied font property exceeds 4096 UTF-16 units");
            return;
        }
        if (value.isEmpty())
            continue;
        bool failed = false;
        style->setProperty(property, value, IsImportant::No, &failed);
        if (failed) {
            WTFLogAlways("yozora editor: cannot parse copied font property");
            return;
        }
    }
    copied_font.get() = WTF::move(style);
}

void Editor::platformPasteFont()
{
    if (RefPtr style = copied_font.get())
        applyStyle(style.get(), EditAction::PasteFont);
}

static String elementURL(Element& element)
{
    if (is<HTMLImageElement>(element) || is<HTMLInputElement>(element))
        return element.attributeWithoutSynchronization(HTMLNames::srcAttr);
    if (is<SVGImageElement>(element))
        return element.attributeWithoutSynchronization(XLinkNames::hrefAttr);
    if (is<HTMLEmbedElement>(element) || is<HTMLObjectElement>(element))
        return element.imageSourceURL();
    return nullString();
}

static bool getImageForElement(Element& element, RefPtr<Image>& image)
{
    auto* renderer = element.renderer();
    if (!is<RenderImage>(renderer))
        return false;

    CachedImage* cachedImage = downcast<RenderImage>(*renderer).cachedImage();
    if (!cachedImage || cachedImage->errorOccurred())
        return false;

    image = cachedImage->imageForRenderer(renderer);
    return image;
}

void Editor::writeImageToPasteboard(Pasteboard& pasteboard, Element& imageElement, const URL&, const String& title)
{
    PasteboardImage pasteboardImage;

    if (!getImageForElement(imageElement, pasteboardImage.image))
        return;
    ASSERT(pasteboardImage.image);

    pasteboardImage.url.url = imageElement.document().encodingParseURL(elementURL(imageElement));
    pasteboardImage.url.title = title;
    pasteboardImage.url.markup = serializeFragment(imageElement, SerializedNodes::SubtreeIncludingNode, nullptr, ResolveURLs::Yes);
    pasteboard.write(pasteboardImage);
}

void Editor::writeSelectionToPasteboard(Pasteboard& pasteboard)
{
    PasteboardWebContent pasteboardContent;
    pasteboardContent.canSmartCopyOrDelete = canSmartCopyOrDelete();
    pasteboardContent.text = selectedTextForDataTransfer();
    pasteboardContent.markup = serializePreservingVisualAppearance(document().selection().selection(), ResolveURLs::YesExcludingURLsForPrivacy, SerializeComposedTree::Yes, IgnoreUserSelectNone::Yes);
    pasteboardContent.contentOrigin = document().originIdentifierForPasteboard();
    pasteboard.write(pasteboardContent);
}

RefPtr<DocumentFragment> Editor::webContentFromPasteboard(Pasteboard& pasteboard, const SimpleRange& context, bool allowPlainText, bool& chosePlainText)
{
    WebContentReader reader(*document().frame(), context, allowPlainText);
    pasteboard.read(reader);
    chosePlainText = reader.madeFragmentFromPlainText();
    return reader.takeFragment();
}

} // namespace WebCore

#endif
