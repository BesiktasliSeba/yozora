#include "config.h"
#include "YozoraWebSocketChannel.h"
#include "YozoraAdblock.h"

#include <JavaScriptCore/ConsoleTypes.h>
#include "CookieJar.h"
#include "CurlStreamScheduler.h"
#include "Document.h"
#include "DocumentPage.h"
#include "Page.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "Blob.h"
#include "FileReaderLoader.h"
#include "FileReaderLoaderClient.h"
#include "NetworkStorageSession.h"
#include "SecurityOrigin.h"
#include "YozoraCookies.h"
#include "SharedBuffer.h"
#include "ThreadableWebSocketChannel.h"
#include "WebSocketChannelClient.h"
#include "WebSocketExtensionProcessor.h"
#include "WebSocketFrame.h"
#include "WebSocketHandshake.h"
#include <JavaScriptCore/ArrayBuffer.h>
#include <wtf/Deque.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RefCounted.h>
#include <wtf/RunLoop.h>
#include <wtf/UniqueArray.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringToIntegerConversion.h>
#include <zlib.h>
#include <optional>

using namespace WebCore;
using namespace WTF::StringLiterals;

namespace {

// a frame or reassembled message above this is refused with 1009, and a peer that fills the receive buffer
// with a frame that never ends is cut off at the same bound
constexpr size_t maximumMessageBytes = 8 * 1024 * 1024;
constexpr size_t maximumHandshakeBytes = 16 * 1024;
// bytes the page has queued and the socket has not taken yet
constexpr size_t maximumBufferedBytes = 16 * 1024 * 1024;
constexpr Seconds closeTimeout { 5_s };

// RFC 7692 on zlib. the deflate-frame code of WebCore is the older x-webkit extension, and its inflater has no bound on the output
struct DeflateParameters {
    bool accepted { false };
    bool clientNoContextTakeover { false };
    bool serverNoContextTakeover { false };
    int clientWindowBits { 15 };
};

class PermessageDeflateProcessor final : public WebSocketExtensionProcessor {
public:
    explicit PermessageDeflateProcessor(std::shared_ptr<DeflateParameters> parameters)
        : WebSocketExtensionProcessor("permessage-deflate"_s)
        , m_parameters(WTF::move(parameters))
    {
    }

    String handshakeString() final { return "permessage-deflate; client_max_window_bits"_s; }

    bool processResponse(const HashMap<String, String>& response) final
    {
        if (m_parameters->accepted) {
            m_failure = "Received duplicate permessage-deflate response"_s;
            return false;
        }
        DeflateParameters accepted;
        for (auto& [name, value] : response) {
            if (name == "server_no_context_takeover"_s || name == "client_no_context_takeover"_s) {
                if (!value.isNull()) {
                    m_failure = makeString("Received invalid "_s, name, " parameter"_s);
                    return false;
                }
                (name == "server_no_context_takeover"_s ? accepted.serverNoContextTakeover : accepted.clientNoContextTakeover) = true;
            } else if (name == "client_max_window_bits"_s || name == "server_max_window_bits"_s) {
                auto bits = parseInteger<int>(value);
                if (!bits || *bits < 8 || *bits > 15) {
                    m_failure = makeString("Received invalid "_s, name, " parameter"_s);
                    return false;
                }
                // zlib refuses a raw deflate window of 8 bits, 9 bits is the same stream for the peer
                if (name == "client_max_window_bits"_s)
                    accepted.clientWindowBits = std::max(*bits, 9);
            } else {
                m_failure = makeString("Received unexpected permessage-deflate parameter "_s, name);
                return false;
            }
        }
        accepted.accepted = true;
        *m_parameters = accepted;
        return true;
    }

    String failureReason() final { return m_failure; }

private:
    std::shared_ptr<DeflateParameters> m_parameters;
    String m_failure;
};

class MessageCompressor {
public:
    explicit MessageCompressor(const DeflateParameters& parameters)
        : m_parameters(parameters)
    {
    }

    ~MessageCompressor()
    {
        if (m_deflateReady)
            deflateEnd(&m_deflate);
        if (m_inflateReady)
            inflateEnd(&m_inflate);
    }

    bool initialize()
    {
        memset(&m_deflate, 0, sizeof(m_deflate));
        memset(&m_inflate, 0, sizeof(m_inflate));
        m_deflateReady = deflateInit2(&m_deflate, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -m_parameters.clientWindowBits, 8, Z_DEFAULT_STRATEGY) == Z_OK;
        // a smaller window of the server is read by an inflater with the largest one
        m_inflateReady = inflateInit2(&m_inflate, -15) == Z_OK;
        return m_deflateReady && m_inflateReady;
    }

    bool compress(std::span<const uint8_t> input, Vector<uint8_t>& output)
    {
        output.clear();
        m_deflate.next_in = const_cast<Bytef*>(input.data());
        m_deflate.avail_in = static_cast<uInt>(input.size());
        uint8_t chunk[16 * 1024];
        do {
            m_deflate.next_out = chunk;
            m_deflate.avail_out = sizeof(chunk);
            int result = deflate(&m_deflate, Z_SYNC_FLUSH);
            if (result != Z_OK && result != Z_BUF_ERROR)
                return false;
            output.append(std::span { chunk, sizeof(chunk) - m_deflate.avail_out });
        } while (!m_deflate.avail_out);
        // a message ends with the empty block of the sync flush, which the peer puts back
        static constexpr uint8_t tail[] = { 0x00, 0x00, 0xff, 0xff };
        bool ended = output.size() >= 4 && !memcmp(output.span().last(4).data(), tail, 4);
        if (ended)
            output.shrink(output.size() - 4);
        if (m_parameters.clientNoContextTakeover)
            deflateReset(&m_deflate);
        return ended;
    }

    // false for a broken stream and for output above the limit
    bool decompress(std::span<const uint8_t> input, Vector<uint8_t>& output, size_t limit)
    {
        output.clear();
        static constexpr uint8_t tail[] = { 0x00, 0x00, 0xff, 0xff };
        bool ok = inflateAll(input, output, limit) && inflateAll(std::span<const uint8_t> { tail, 4 }, output, limit);
        if (m_parameters.serverNoContextTakeover)
            inflateReset(&m_inflate);
        return ok;
    }

private:
    bool inflateAll(std::span<const uint8_t> input, Vector<uint8_t>& output, size_t limit)
    {
        m_inflate.next_in = const_cast<Bytef*>(input.data());
        m_inflate.avail_in = static_cast<uInt>(input.size());
        uint8_t chunk[16 * 1024];
        do {
            m_inflate.next_out = chunk;
            m_inflate.avail_out = sizeof(chunk);
            int result = inflate(&m_inflate, Z_SYNC_FLUSH);
            if (result != Z_OK && result != Z_BUF_ERROR && result != Z_STREAM_END)
                return false;
            size_t produced = sizeof(chunk) - m_inflate.avail_out;
            if (output.size() + produced > limit)
                return false;
            output.append(std::span { chunk, produced });
            if (result == Z_STREAM_END || (result == Z_BUF_ERROR && !produced))
                break;
        } while (m_inflate.avail_in || !m_inflate.avail_out);
        return true;
    }

    DeflateParameters m_parameters;
    z_stream m_deflate;
    z_stream m_inflate;
    bool m_deflateReady { false };
    bool m_inflateReady { false };
};

// reads a Blob the page wants to send. the page gets its bufferedAmount at once and the bytes follow in the order of the sends
class BlobReader final : public FileReaderLoaderClient, public RefCounted<BlobReader> {
public:
    static Ref<BlobReader> create(uint64_t id, Function<void(RefPtr<JSC::ArrayBuffer>&&)>&& done) { return adoptRef(*new BlobReader(id, WTF::move(done))); }

    uint64_t id() const { return m_id; }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    void start(ScriptExecutionContext& context, Blob& blob)
    {
        m_loader = FileReaderLoader::create(FileReaderLoader::ReadAsArrayBuffer, this);
        m_loader->start(&context, blob);
    }

    void cancel()
    {
        m_done = nullptr;
        if (m_loader)
            m_loader->cancel();
    }

private:
    BlobReader(uint64_t id, Function<void(RefPtr<JSC::ArrayBuffer>&&)>&& done)
        : m_id(id)
        , m_done(WTF::move(done))
    {
    }

    void didStartLoading() final { }
    void didReceiveData() final { }

    void didFinishLoading() final
    {
        if (auto done = std::exchange(m_done, nullptr))
            done(m_loader->arrayBufferResult());
    }

    void didFail(ExceptionCode) final
    {
        if (auto done = std::exchange(m_done, nullptr))
            done(nullptr);
    }

    uint64_t m_id;
    RefPtr<FileReaderLoader> m_loader;
    Function<void(RefPtr<JSC::ArrayBuffer>&&)> m_done;
};

CurlStreamScheduler& streamScheduler()
{
    static NeverDestroyed<CurlStreamScheduler> scheduler;
    return scheduler.get();
}

// the channel lives on the core thread. CurlStream does its transfers on a thread of its own and calls back through callOnMainThread
class YozoraWebSocketChannel final : public ThreadableWebSocketChannel, public RefCounted<YozoraWebSocketChannel>, private CurlStream::Client {
public:
    explicit YozoraWebSocketChannel(Document& document, WebSocketChannelClient& client)
        : m_document(document)
        , m_client(client)
    {
    }

    ~YozoraWebSocketChannel() final
    {
        releaseStream();
    }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    ConnectStatus connect(const URL& requested, const String& protocol) final
    {
        RefPtr document = m_document.get();
        if (!document || m_state != State::Idle)
            return ConnectStatus::KO;
        auto validated = validateURL(*document, requested);
        if (!validated)
            return ConnectStatus::KO;
        if (YozoraAdblock::blocksWebSocket(*document, validated->url))
            return ConnectStatus::KO;
        m_url = validated->url;
        m_handshake = makeUnique<WebSocketHandshake>(m_url, protocol, document->userAgent(m_url), document->securityOrigin().toString(), validated->areCookiesAllowed, false);
        m_handshake->reset();
        m_deflateParameters = std::make_shared<DeflateParameters>();
        m_handshake->addExtensionProcessor(std::make_unique<PermessageDeflateProcessor>(m_deflateParameters));
        m_cookiesAllowed = validated->areCookiesAllowed;
        if (validated->areCookiesAllowed) {
            RefPtr page = document->page();
            m_cookie = page ? page->cookieJar().cookieRequestHeaderFieldValue(*document, m_handshake->httpURLForAuthenticationAndCookies()) : String { };
        }
        m_state = State::Connecting;
        m_streamID = streamScheduler().createStream(m_url, *this, CurlStream::ServerTrustEvaluation::Enable, CurlStream::LocalhostAlias::Disable);
        return ConnectStatus::OK;
    }

    String subprotocol() final { return m_handshake ? m_handshake->serverWebSocketProtocol() : emptyString(); }
    String extensions() final { return m_handshake ? m_handshake->acceptedExtensions() : emptyString(); }

    void send(CString&& text) final
    {
        sendOrdered(WebSocketFrame::OpCodeText, byteCast<uint8_t>(text.span()));
    }

    void send(const JSC::ArrayBuffer& buffer, unsigned offset, unsigned length) final
    {
        auto bytes = buffer.span();
        if (offset > bytes.size() || length > bytes.size() - offset) {
            fail("The range of the buffer to send is out of bounds"_s);
            return;
        }
        sendOrdered(WebSocketFrame::OpCodeBinary, bytes.subspan(offset, length));
    }

    void send(Blob& blob) final
    {
        RefPtr document = m_document.get();
        if (m_state != State::Open || !document)
            return;
        uint64_t size = blob.size();
        if (size > maximumMessageBytes || m_bufferedBytes + size > maximumBufferedBytes) {
            fail("The Blob is larger than the send buffer of the WebSocket"_s);
            return;
        }
        uint64_t id = ++m_outgoingID;
        m_outgoing.append({ id, WebSocketFrame::OpCodeBinary, { }, false, static_cast<size_t>(size) });
        m_bufferedBytes += static_cast<size_t>(size);
        if (RefPtr client = m_client.get())
            client->didUpdateBufferedAmount(static_cast<unsigned>(m_bufferedBytes));
        auto reader = BlobReader::create(id, [this, protectedThis = Ref { *this }, id](RefPtr<JSC::ArrayBuffer>&& buffer) {
            blobRead(id, WTF::move(buffer));
        });
        m_readers.append(reader.copyRef());
        reader->start(*document, blob);
    }

    void close(int code, const String& reason) final
    {
        if (m_state == State::Closed || m_state == State::Closing)
            return;
        if (m_state != State::Open) {
            fail("WebSocket is closed before the connection is established"_s);
            return;
        }
        if (code == CloseEventCodeNotSpecified)
            code = CloseEventCodeNormalClosure;
        sendClose(static_cast<unsigned short>(code), reason);
        m_state = State::Closing;
        armCloseTimeout();
    }

    void fail(String&& reason) final
    {
        if (m_state == State::Closed)
            return;
        Ref protectedThis { *this };
        if (m_failureWhenFlushed) {
            reason = WTF::move(*m_failureWhenFlushed);
            m_failureWhenFlushed.reset();
        }
        if (RefPtr document = m_document.get())
            document->addConsoleMessage(MessageSource::Network, MessageLevel::Error, reason);
        if (RefPtr client = m_client.get())
            client->didReceiveMessageError(WTF::move(reason));
        finish(WebSocketChannelClient::ClosingHandshakeIncomplete, CloseEventCodeAbnormalClosure, emptyString());
    }

    // the page lets go of the socket and wants no close event
    void disconnect() final
    {
        m_client = nullptr;
        m_state = State::Closed;
        releaseStream();
    }

    void suspend() final { m_suspended = true; }

    void resume() final
    {
        m_suspended = false;
        processReceived();
    }

    WebSocketChannelIdentifier progressIdentifier() const final { return m_identifier; }
    bool hasCreatedHandshake() const final { return !!m_handshake; }
    bool isConnected() const final { return m_state == State::Open; }

    ResourceRequest clientHandshakeRequest(const CookieGetter& cookies) const final
    {
        return m_handshake ? m_handshake->clientHandshakeRequest(cookies) : ResourceRequest { };
    }

    const ResourceResponse& serverHandshakeResponse() const final
    {
        static NeverDestroyed<ResourceResponse> none;
        return m_handshake ? m_handshake->serverHandshakeResponse() : none.get();
    }

private:
    enum class State { Idle, Connecting, Handshaking, Open, Closing, Closed };

    struct Queued {
        size_t wireBytes;
        size_t payloadBytes;
    };

    void didOpen(CurlStreamID) final
    {
        if (m_state != State::Connecting)
            return;
        auto message = m_handshake->clientHandshakeMessage();
        m_state = State::Handshaking;
        auto bytes = byteCast<uint8_t>(message.span());
        // the handshake class leaves the cookie to the network process of its browser, so it goes in just
        // before the blank line. a value with a line break would end the request early and is dropped
        if (m_cookie.isEmpty() || m_cookie.contains('\r') || m_cookie.contains('\n') || bytes.size() < 2) {
            queue(bytes, 0);
            return;
        }
        Vector<uint8_t> request;
        request.append(bytes.first(bytes.size() - 2));
        request.append(byteCast<uint8_t>(makeString("Cookie: "_s, m_cookie, "\r\n\r\n"_s).utf8().span()));
        queue(request.span(), 0);
    }

    void didSendData(CurlStreamID, size_t sent) final
    {
        m_sentCarry += sent;
        size_t buffered = m_bufferedBytes;
        while (!m_queue.isEmpty() && m_sentCarry >= m_queue.first().wireBytes) {
            m_sentCarry -= m_queue.first().wireBytes;
            m_bufferedBytes -= m_queue.first().payloadBytes;
            m_queue.removeFirst();
        }
        if (buffered != m_bufferedBytes) {
            if (RefPtr client = m_client.get())
                client->didUpdateBufferedAmount(static_cast<unsigned>(m_bufferedBytes));
        }
        if (m_failureWhenFlushed && m_queue.isEmpty()) {
            fail(String { *m_failureWhenFlushed });
            return;
        }
        if (m_closeWhenFlushed && m_queue.isEmpty())
            finish(WebSocketChannelClient::ClosingHandshakeComplete, m_peerCloseCode, m_peerCloseReason);
    }

    void didReceiveData(CurlStreamID, const SharedBuffer& data) final
    {
        if (m_state == State::Closed)
            return;
        // CurlStream reports a closed connection as an empty read
        if (data.isEmpty()) {
            connectionEnded();
            return;
        }
        if (m_failureWhenFlushed)
            return;
        if (m_receive.size() + data.size() > maximumMessageBytes + maximumHandshakeBytes) {
            failWithClose(CloseEventCodeMessageTooBig, "A received message is larger than the limit"_s);
            return;
        }
        m_receive.append(data.span());
        processReceived();
    }

    void didFail(CurlStreamID, CURLcode code, CertificateInfo&&) final
    {
        if (m_state == State::Closed)
            return;
        if (m_state == State::Connecting || m_state == State::Handshaking) {
            fail(makeString("WebSocket connection to '"_s, m_url.string(), "' failed: "_s, String::fromLatin1(curl_easy_strerror(code))));
            return;
        }
        connectionEnded();
    }

    void connectionEnded()
    {
        if (m_failureWhenFlushed) {
            fail(String { *m_failureWhenFlushed });
            return;
        }
        if (m_state == State::Connecting || m_state == State::Handshaking) {
            fail(makeString("WebSocket connection to '"_s, m_url.string(), "' failed: the server closed the connection"_s));
            return;
        }
        // no close frame arrived, or the one that did was answered and the server hung up first
        finish(m_peerClosed ? WebSocketChannelClient::ClosingHandshakeComplete : WebSocketChannelClient::ClosingHandshakeIncomplete,
            m_peerClosed ? m_peerCloseCode : static_cast<unsigned short>(CloseEventCodeAbnormalClosure), m_peerClosed ? m_peerCloseReason : emptyString());
    }

    void processReceived()
    {
        if (m_suspended || m_state == State::Closed || m_failureWhenFlushed)
            return;
        Ref protectedThis { *this };
        if (m_state == State::Handshaking && !processHandshake())
            return;
        if (m_state == State::Open || m_state == State::Closing)
            processFrames();
    }

    // false when the handshake is not finished or failed
    bool processHandshake()
    {
        int consumed = m_handshake->readServerHandshake(m_receive.span());
        if (consumed < 0) {
            if (m_receive.size() > maximumHandshakeBytes)
                fail("The server handshake response is too long"_s);
            return false;
        }
        if (m_handshake->mode() != WebSocketHandshake::Connected) {
            fail(makeString("Error during WebSocket handshake: "_s, m_handshake->failureReason()));
            return false;
        }
        applyServerCookies(m_receive.span().first(static_cast<size_t>(consumed)));
        m_receive.removeAt(0, static_cast<size_t>(consumed));
        if (m_deflateParameters->accepted) {
            m_compressor = std::make_unique<MessageCompressor>(*m_deflateParameters);
            if (!m_compressor->initialize()) {
                fail("Could not start the permessage-deflate compressor"_s);
                return false;
            }
        }
        m_state = State::Open;
        if (RefPtr client = m_client.get())
            client->didConnect();
        return m_state != State::Closed;
    }

    void processFrames()
    {
        size_t offset = 0;
        while (offset < m_receive.size() && m_state != State::Closed) {
            WebSocketFrame frame;
            const uint8_t* end = nullptr;
            String error;
            auto result = WebSocketFrame::parseFrame(m_receive.mutableSpan().subspan(offset), frame, end, error);
            if (result == WebSocketFrame::FrameIncomplete)
                break;
            if (result == WebSocketFrame::FrameError) {
                failWithClose(CloseEventCodeProtocolError, error);
                return;
            }
            size_t length = static_cast<size_t>(end - (m_receive.span().data() + offset));
            offset += length;
            if (!handleFrame(frame))
                return;
        }
        if (offset)
            m_receive.removeAt(0, offset);
    }

    // false when the connection ended
    bool handleFrame(const WebSocketFrame& frame)
    {
        if (frame.masked) {
            failWithClose(CloseEventCodeProtocolError, "A server must not mask any frames that it sends to the client"_s);
            return false;
        }
        if (frame.reserved2 || frame.reserved3) {
            failWithClose(CloseEventCodeProtocolError, "A frame uses a reserved bit"_s);
            return false;
        }
        // the compress bit marks the first frame of a compressed message and only exists when permessage-deflate was agreed
        if (frame.compress && (!m_compressor || !WebSocketFrame::isNonControlOpCode(frame.opCode) || frame.opCode == WebSocketFrame::OpCodeContinuation)) {
            failWithClose(CloseEventCodeProtocolError, "A frame is compressed without an agreed extension"_s);
            return false;
        }
        if (WebSocketFrame::isReservedOpCode(frame.opCode)) {
            failWithClose(CloseEventCodeProtocolError, makeString("Unknown opcode "_s, static_cast<int>(frame.opCode)));
            return false;
        }
        if (WebSocketFrame::isControlOpCode(frame.opCode) && (!frame.final || frame.payload.size() > 125)) {
            failWithClose(CloseEventCodeProtocolError, "A control frame is fragmented or longer than 125 bytes"_s);
            return false;
        }
        switch (frame.opCode) {
        case WebSocketFrame::OpCodeText:
        case WebSocketFrame::OpCodeBinary:
            if (m_fragmenting) {
                failWithClose(CloseEventCodeProtocolError, "A new message started before the previous one ended"_s);
                return false;
            }
            m_message.clear();
            m_messageOpCode = frame.opCode;
            m_messageCompressed = frame.compress;
            return appendMessage(frame);
        case WebSocketFrame::OpCodeContinuation:
            if (!m_fragmenting) {
                failWithClose(CloseEventCodeProtocolError, "A continuation frame has no message to continue"_s);
                return false;
            }
            return appendMessage(frame);
        case WebSocketFrame::OpCodePing:
            if (m_state == State::Open)
                sendFrame(WebSocketFrame::OpCodePong, frame.payload);
            return true;
        case WebSocketFrame::OpCodePong:
            return true;
        case WebSocketFrame::OpCodeClose:
            return handleClose(frame);
        default:
            return true;
        }
    }

    bool appendMessage(const WebSocketFrame& frame)
    {
        if (m_message.size() + frame.payload.size() > maximumMessageBytes) {
            failWithClose(CloseEventCodeMessageTooBig, "A received message is larger than the limit"_s);
            return false;
        }
        m_message.append(frame.payload);
        m_fragmenting = !frame.final;
        if (!frame.final)
            return true;
        if (m_messageCompressed) {
            Vector<uint8_t> plain;
            if (!m_compressor->decompress(m_message.span(), plain, maximumMessageBytes)) {
                failWithClose(CloseEventCodeMessageTooBig, "A compressed message is broken or larger than the limit"_s);
                return false;
            }
            m_message = WTF::move(plain);
            m_messageCompressed = false;
        }
        RefPtr client = m_client.get();
        if (m_messageOpCode == WebSocketFrame::OpCodeText) {
            auto text = String::fromUTF8(m_message.span());
            if (text.isNull()) {
                failWithClose(CloseEventCodeInvalidFramePayloadData, "Could not decode a text frame as UTF-8"_s);
                return false;
            }
            m_message.clear();
            if (client)
                client->didReceiveMessage(WTF::move(text));
        } else {
            auto data = std::exchange(m_message, { });
            if (client)
                client->didReceiveBinaryData(WTF::move(data));
        }
        return m_state != State::Closed;
    }

    bool handleClose(const WebSocketFrame& frame)
    {
        unsigned short code = CloseEventCodeNoStatusRcvd;
        String reason;
        if (frame.payload.size() == 1) {
            failWithClose(CloseEventCodeProtocolError, "A close frame carries a one byte payload"_s);
            return false;
        }
        if (frame.payload.size() >= 2) {
            code = static_cast<unsigned short>(frame.payload[0] << 8 | frame.payload[1]);
            reason = String::fromUTF8(frame.payload.subspan(2));
            if (reason.isNull()) {
                failWithClose(CloseEventCodeInvalidFramePayloadData, "A close reason is not UTF-8"_s);
                return false;
            }
        }
        m_peerClosed = true;
        m_peerCloseCode = code;
        m_peerCloseReason = reason;
        if (m_state == State::Open) {
            if (RefPtr client = m_client.get())
                client->didStartClosingHandshake();
            if (m_state == State::Closed)
                return false;
            // the answer repeats the code, and the socket goes once the answer has left
            sendClose(code == CloseEventCodeNoStatusRcvd ? static_cast<unsigned short>(CloseEventCodeNormalClosure) : code, emptyString());
            m_state = State::Closing;
            m_closeWhenFlushed = true;
            armCloseTimeout();
            if (m_queue.isEmpty())
                finish(WebSocketChannelClient::ClosingHandshakeComplete, code, reason);
            return false;
        }
        // the answer to the close that was sent
        finish(WebSocketChannelClient::ClosingHandshakeComplete, code, reason);
        return false;
    }

    // a send waits behind a Blob that is still being read, so the page sees its messages leave in order
    void sendOrdered(WebSocketFrame::OpCode opCode, std::span<const uint8_t> payload)
    {
        if (m_state != State::Open)
            return;
        if (m_outgoing.isEmpty()) {
            sendData(opCode, payload);
            return;
        }
        if (m_bufferedBytes + payload.size() > maximumBufferedBytes) {
            fail("The send buffer of the WebSocket is full"_s);
            return;
        }
        m_outgoing.append({ ++m_outgoingID, opCode, Vector<uint8_t> { payload }, true, 0 });
        m_bufferedBytes += payload.size();
        m_outgoing.last().size = payload.size();
    }

    void blobRead(uint64_t id, RefPtr<JSC::ArrayBuffer>&& buffer)
    {
        // the reader is inside its own callback and the bytes it read stay with it, so it goes once the callback is over
        RunLoop::currentSingleton().dispatch([this, protectedThis = Ref { *this }, id] {
            m_readers.removeFirstMatching([id](auto& reader) { return reader->id() == id; });
        });
        if (m_state != State::Open)
            return;
        if (!buffer) {
            fail("The Blob could not be read"_s);
            return;
        }
        for (auto& item : m_outgoing) {
            if (item.id == id) {
                item.data = Vector<uint8_t> { buffer->span() };
                item.ready = true;
                break;
            }
        }
        Ref protectedThis { *this };
        while (!m_outgoing.isEmpty() && m_outgoing.first().ready && m_state == State::Open) {
            auto item = m_outgoing.takeFirst();
            m_bufferedBytes -= item.size;
            sendData(item.opCode, item.data.span());
        }
    }

    void sendData(WebSocketFrame::OpCode opCode, std::span<const uint8_t> payload)
    {
        if (m_state != State::Open)
            return;
        if (m_bufferedBytes + payload.size() > maximumBufferedBytes) {
            fail("The send buffer of the WebSocket is full"_s);
            return;
        }
        // short messages do not repay the compression
        if (m_compressor && payload.size() >= 128) {
            Vector<uint8_t> compressed;
            if (!m_compressor->compress(payload, compressed)) {
                fail("A message could not be compressed"_s);
                return;
            }
            sendFrame(opCode, compressed.span(), true, payload.size());
            return;
        }
        sendFrame(opCode, payload, false, payload.size());
    }

    // accounted is what the page sent, the frame may hold fewer bytes when compressed
    void sendFrame(WebSocketFrame::OpCode opCode, std::span<const uint8_t> payload, bool compressed = false, size_t accounted = 0)
    {
        WebSocketFrame frame(opCode, true, compressed, true, payload);
        Vector<uint8_t> data;
        frame.makeFrameData(data);
        queue(data.span(), WebSocketFrame::isControlOpCode(opCode) ? 0 : accounted);
    }

    // the handshake class leaves Set-Cookie to the network process of its browser, so lines are read from the
    // raw reply. cookies the page may not have are dropped and the store applies its own accept policy
    void applyServerCookies(std::span<const uint8_t> header)
    {
        RefPtr document = m_document.get();
        if (!document || !m_cookiesAllowed)
            return;
        StringView text { header };
        for (auto line : text.splitAllowingEmptyEntries('\n')) {
            auto trimmed = line.trim(isASCIIWhitespace<UChar>);
            static constexpr auto name = "set-cookie:"_s;
            if (trimmed.length() <= name.length() || !trimmed.startsWithIgnoringASCIICase(name))
                continue;
            auto value = trimmed.substring(name.length()).trim(isASCIIWhitespace<UChar>).toString();
            YozoraCookies::current().setCookiesFromHTTPResponse(document->firstPartyForCookies(), m_handshake->httpURLForAuthenticationAndCookies(), value);
        }
    }

    void sendClose(unsigned short code, const String& reason)
    {
        auto utf8 = reason.utf8();
        // a control frame carries at most 125 bytes, two of them the code
        auto bytes = byteCast<uint8_t>(utf8.span());
        size_t length = std::min<size_t>(bytes.size(), 123);
        // a cut must not land inside a multibyte sequence
        while (length && length < bytes.size() && (bytes[length] & 0xc0) == 0x80)
            --length;
        Vector<uint8_t> payload;
        payload.append(static_cast<uint8_t>(code >> 8));
        payload.append(static_cast<uint8_t>(code & 0xff));
        payload.append(bytes.first(length));
        sendFrame(WebSocketFrame::OpCodeClose, payload.span());
    }

    void queue(std::span<const uint8_t> bytes, size_t payloadBytes)
    {
        if (m_streamID == invalidCurlStreamID || bytes.empty())
            return;
        auto buffer = makeUniqueArray<uint8_t>(bytes.size());
        memcpySpan(std::span { buffer.get(), bytes.size() }, bytes);
        m_queue.append({ bytes.size(), payloadBytes });
        m_bufferedBytes += payloadBytes;
        streamScheduler().send(m_streamID, WTF::move(buffer), bytes.size());
    }

    // destroying the stream before didSendData drops the queued protocol-error close frame
    void failWithClose(unsigned short code, const String& reason)
    {
        if (m_state != State::Open) {
            fail(String { reason });
            return;
        }
        m_failureWhenFlushed = reason;
        m_state = State::Closing;
        sendClose(code, emptyString());
        if (m_queue.isEmpty()) {
            fail(String { reason });
            return;
        }
        armCloseTimeout();
    }

    void armCloseTimeout()
    {
        m_closeTimer = RunLoop::currentSingleton().dispatchAfter(closeTimeout, [this, protectedThis = Ref { *this }] {
            if (m_state != State::Closing)
                return;
            if (m_failureWhenFlushed)
                fail(String { *m_failureWhenFlushed });
            else
                finish(WebSocketChannelClient::ClosingHandshakeIncomplete, CloseEventCodeAbnormalClosure, emptyString());
        });
    }

    void finish(WebSocketChannelClient::ClosingHandshakeCompletionStatus status, unsigned short code, const String& reason)
    {
        if (m_state == State::Closed)
            return;
        m_state = State::Closed;
        releaseStream();
        RefPtr client = m_client.get();
        if (client)
            client->didClose(static_cast<unsigned>(m_bufferedBytes), status, code, reason);
    }

    void releaseStream()
    {
        for (auto& reader : std::exchange(m_readers, { }))
            reader->cancel();
        m_outgoing.clear();
        if (m_closeTimer) {
            m_closeTimer->stop();
            m_closeTimer = nullptr;
        }
        if (m_streamID != invalidCurlStreamID) {
            streamScheduler().destroyStream(m_streamID);
            m_streamID = invalidCurlStreamID;
        }
    }

    WeakPtr<Document, WeakPtrImplWithEventTargetData> m_document;
    ThreadSafeWeakPtr<WebSocketChannelClient> m_client;
    WebSocketChannelIdentifier m_identifier { WebSocketChannelIdentifier::generate() };
    State m_state { State::Idle };
    URL m_url;
    std::unique_ptr<WebSocketHandshake> m_handshake;
    String m_cookie;
    CurlStreamID m_streamID { invalidCurlStreamID };
    Vector<uint8_t> m_receive;
    Vector<uint8_t> m_message;
    WebSocketFrame::OpCode m_messageOpCode { WebSocketFrame::OpCodeInvalid };
    bool m_fragmenting { false };
    bool m_suspended { false };
    bool m_peerClosed { false };
    bool m_closeWhenFlushed { false };
    unsigned short m_peerCloseCode { CloseEventCodeAbnormalClosure };
    String m_peerCloseReason;
    std::optional<String> m_failureWhenFlushed;
    struct Outgoing {
        uint64_t id;
        WebSocketFrame::OpCode opCode;
        Vector<uint8_t> data;
        bool ready;
        size_t size;
    };

    Deque<Queued> m_queue;
    Deque<Outgoing> m_outgoing;
    Vector<Ref<BlobReader>> m_readers;
    uint64_t m_outgoingID { 0 };
    std::shared_ptr<DeflateParameters> m_deflateParameters;
    std::unique_ptr<MessageCompressor> m_compressor;
    bool m_messageCompressed { false };
    bool m_cookiesAllowed { true };
    size_t m_sentCarry { 0 };
    size_t m_bufferedBytes { 0 };
    RefPtr<RunLoop::DispatchTimer> m_closeTimer;
};

}

namespace Yozora {

RefPtr<ThreadableWebSocketChannel> createWebSocketChannel(Document& document, WebSocketChannelClient& client)
{
    return adoptRef(*new YozoraWebSocketChannel(document, client));
}

}
