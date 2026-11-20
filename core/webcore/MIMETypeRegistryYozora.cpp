#include "config.h"
#include "MIMETypeRegistry.h"

#include <array>
#include <wtf/Vector.h>
#include <wtf/text/StringView.h>

namespace WebCore {

// recognition does not enable a decoder, media player or plugin
static constexpr auto mime_types = std::to_array<TypeExtensionPair>({
    { "text/html"_s, "html"_s },
    { "text/html"_s, "htm"_s },
    { "text/css"_s, "css"_s },
    { "text/javascript"_s, "js"_s },
    { "text/javascript"_s, "mjs"_s },
    { "application/json"_s, "json"_s },
    { "application/manifest+json"_s, "webmanifest"_s },
    { "application/xhtml+xml"_s, "xhtml"_s },
    { "application/xml"_s, "xml"_s },
    { "application/rss+xml"_s, "rss"_s },
    { "application/atom+xml"_s, "atom"_s },
    { "text/plain"_s, "txt"_s },
    { "text/plain"_s, "text"_s },
    { "text/csv"_s, "csv"_s },
    { "text/vtt"_s, "vtt"_s },
    { "image/png"_s, "png"_s },
    { "image/jpeg"_s, "jpg"_s },
    { "image/jpeg"_s, "jpeg"_s },
    { "image/jpeg"_s, "jpe"_s },
    { "image/gif"_s, "gif"_s },
    { "image/webp"_s, "webp"_s },
    { "image/svg+xml"_s, "svg"_s },
    { "image/bmp"_s, "bmp"_s },
    { "image/vnd.microsoft.icon"_s, "ico"_s },
    { "image/avif"_s, "avif"_s },
    { "font/ttf"_s, "ttf"_s },
    { "font/otf"_s, "otf"_s },
    { "font/woff"_s, "woff"_s },
    { "font/woff2"_s, "woff2"_s },
    { "application/wasm"_s, "wasm"_s },
    { "application/pdf"_s, "pdf"_s },
    { "application/zip"_s, "zip"_s },
    { "application/gzip"_s, "gz"_s },
    { "application/octet-stream"_s, "bin"_s },
    { "audio/mpeg"_s, "mp3"_s },
    { "audio/mp4"_s, "m4a"_s },
    { "audio/aac"_s, "aac"_s },
    { "audio/ogg"_s, "ogg"_s },
    { "audio/ogg"_s, "oga"_s },
    { "audio/wav"_s, "wav"_s },
    { "audio/flac"_s, "flac"_s },
    { "audio/webm"_s, "weba"_s },
    { "video/mp4"_s, "mp4"_s },
    { "video/mp4"_s, "m4v"_s },
    { "video/webm"_s, "webm"_s },
    { "video/ogg"_s, "ogv"_s },
    { "video/mpeg"_s, "mpeg"_s },
    { "video/mpeg"_s, "mpg"_s },
    { "video/quicktime"_s, "mov"_s },
    { "application/vnd.apple.mpegurl"_s, "m3u8"_s },
});

String MIMETypeRegistry::mimeTypeForExtension(StringView extension)
{
    for (auto& entry : mime_types) {
        if (equalIgnoringASCIICase(extension, entry.extension))
            return entry.type;
    }
    return emptyString();
}

String MIMETypeRegistry::preferredExtensionForMIMEType(const String& type)
{
    for (auto& entry : mime_types) {
        if (equalIgnoringASCIICase(type, entry.type))
            return entry.extension;
    }
    return emptyString();
}

Vector<String> MIMETypeRegistry::extensionsForMIMEType(const String& type)
{
    Vector<String> result;
    for (auto& entry : mime_types) {
        if (equalIgnoringASCIICase(type, entry.type))
            result.append(entry.extension);
    }
    return result;
}

bool MIMETypeRegistry::isApplicationPluginMIMEType(const String&)
{
    // yozora has no application plugins, recognized files still use WebCore's support checks
    return false;
}

}
