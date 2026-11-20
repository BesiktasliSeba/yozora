#pragma once

#include <wtf/Forward.h>
#include <wtf/RefPtr.h>

namespace WebCore {
class Document;
class ThreadableWebSocketChannel;
class WebSocketChannelClient;
}

namespace Yozora {

// a WebSocket over CurlStream: handshake and frames are WebCore's, the transport verifies the server
// certificate and host name as page requests do
RefPtr<WebCore::ThreadableWebSocketChannel> createWebSocketChannel(WebCore::Document&, WebCore::WebSocketChannelClient&);

}
