#include "config.h"
#include "YozoraMediaSource.h"
#include "../yozora_log.h"

#if ENABLE(MEDIA_SOURCE)

#include "ContentType.h"
#include "FloatSize.h"
#include "Logging.h"
#include "MediaDescription.h"
#include "MediaPlayer.h"
#include "MediaSample.h"
#include "MediaSourcePrivateClient.h"
#include "SharedBuffer.h"
#include "SourceBufferPrivate.h"
#include "SourceBufferPrivateClient.h"
#include "AudioTrackPrivate.h"
#include "VideoTrackPrivate.h"
#include "yozora_mp4.h"
#include <cstdio>
#include <wtf/MainThread.h>
#include <wtf/NativePromise.h>
#include <wtf/text/MakeString.h>

using namespace WebCore;

namespace Yozora {

namespace {

// samples waiting for the other track's config are kept up to this many bytes, past it the stream is broken:
// a page that appends video for minutes without its audio init segment has no stream to play
constexpr size_t earlyLimit = 8 * 1024 * 1024;
constexpr int64_t hlsRate = 90000;

int64_t toHlsTime(const MediaTime& time)
{
    return time.toTimeScale(hlsRate).timeValue();
}

const char* describe(yozora_hls_status status)
{
    switch (status) {
    case YOZORA_HLS_OK:
        return "ok";
    case YOZORA_HLS_BAD_ARGUMENT:
        return "bad argument";
    case YOZORA_HLS_BAD_CONFIG:
        return "a track config the muxer refuses";
    case YOZORA_HLS_BAD_SAMPLE:
        return "a sample the muxer refuses";
    case YOZORA_HLS_NO_MEMORY:
        return "no memory";
    case YOZORA_HLS_TOO_LARGE:
        return "too much data waiting for a segment";
    case YOZORA_HLS_ENDED:
        return "samples after the end of the stream";
    }
    return "unknown";
}

}

MseSession::MseSession() = default;

Ref<MseSession> MseSession::create()
{
    return adoptRef(*new MseSession);
}

MseSession::~MseSession()
{
    if (m_hls)
        yozora_hls_release(m_hls);
}

void MseSession::setCallbacks(Function<void(unsigned)>&& epochStarted, Function<void(String&&)>&& failed)
{
    auto callbacks = adoptRef(*new Callbacks);
    callbacks->epochStarted = WTF::move(epochStarted);
    callbacks->failed = WTF::move(failed);
    Locker locker { m_lock };
    m_callbacks = WTF::move(callbacks);
}

void MseSession::wantTracks(bool video, bool audio)
{
    Locker locker { m_lock };
    m_wantVideo = m_wantVideo || video;
    m_wantAudio = m_wantAudio || audio;
}

void MseSession::setVideo(Vector<uint8_t>&& avcc, unsigned width, unsigned height)
{
    Locker locker { m_lock };
    m_avcc = WTF::move(avcc);
    m_width = width;
    m_height = height;
}

void MseSession::setAudio(Vector<uint8_t>&& config)
{
    Locker locker { m_lock };
    m_audioConfig = WTF::move(config);
}

void MseSession::flush()
{
    Locker locker { m_lock };
    if (m_hls) {
        yozora_hls_release(m_hls);
        m_hls = nullptr;
    }
    m_needsEpoch = true;
    m_broken = false;
    m_ended = false;
    m_early.clear();
    m_earlyBytes = 0;
}

// opens the epoch when the config of every wanted track is known, earlier samples go into it in arrival order
bool MseSession::ensureEpoch(unsigned& startedEpoch)
{
    if (m_hls)
        return true;
    if ((m_wantVideo && m_avcc.isEmpty()) || (m_wantAudio && m_audioConfig.isEmpty()))
        return false;
    yozora_hls* hls = nullptr;
    if (yozora_hls_create(m_wantVideo, m_wantAudio, &hls) != YOZORA_HLS_OK)
        return false;
    if ((m_wantVideo && yozora_hls_set_video(hls, m_avcc.span().data(), m_avcc.size()) != YOZORA_HLS_OK)
        || (m_wantAudio && yozora_hls_set_audio(hls, m_audioConfig.span().data(), m_audioConfig.size()) != YOZORA_HLS_OK)) {
        yozora_hls_release(hls);
        return false;
    }
    m_hls = hls;
    m_needsEpoch = false;
    startedEpoch = ++m_epoch;
    yozora_log(YOZORA_LOG_INFO, "mse", "epoch %u started: video %d, audio %d, %u samples came before the config", startedEpoch, m_wantVideo ? 1 : 0, m_wantAudio ? 1 : 0, static_cast<unsigned>(m_early.size()));
    return true;
}

void MseSession::report(yozora_hls_status status, const char* what, bool& fail)
{
    if (status == YOZORA_HLS_OK)
        return;
    m_broken = true;
    fail = true;
    yozora_log(YOZORA_LOG_ERROR, "mse", "%s: %s, the epoch is broken until the next seek", what, describe(status));
}

void MseSession::addVideo(int64_t dts, int64_t pts, bool sync, const uint8_t* data, size_t size)
{
    unsigned started = 0;
    bool fail = false;
    {
        Locker locker { m_lock };
        if (m_broken || m_ended)
            return;
        if (!ensureEpoch(started)) {
            if (m_earlyBytes + size > earlyLimit) {
                m_broken = true;
                fail = true;
            } else {
                m_early.append(Early { true, dts, pts, sync, Vector<uint8_t>(std::span<const uint8_t> { data, size }) });
                m_earlyBytes += size;
                return;
            }
        } else {
            if (started) {
                auto early = std::exchange(m_early, { });
                m_earlyBytes = 0;
                for (auto& item : early) {
                    report(item.video ? yozora_hls_add_video(m_hls, item.dts, item.pts, item.sync, item.data.span().data(), item.data.size())
                                      : yozora_hls_add_audio(m_hls, item.pts, item.data.span().data(), item.data.size()), "epoch start", fail);
                }
            }
            if (!m_broken)
                report(yozora_hls_add_video(m_hls, dts, pts, sync, data, size), "video sample", fail);
        }
    }
    notify(started, fail);
}

void MseSession::addAudio(int64_t pts, const uint8_t* data, size_t size)
{
    unsigned started = 0;
    bool fail = false;
    {
        Locker locker { m_lock };
        if (m_broken || m_ended)
            return;
        if (!ensureEpoch(started)) {
            if (m_earlyBytes + size > earlyLimit) {
                m_broken = true;
                fail = true;
            } else {
                m_early.append(Early { false, pts, pts, true, Vector<uint8_t>(std::span<const uint8_t> { data, size }) });
                m_earlyBytes += size;
                return;
            }
        } else {
            if (started) {
                auto early = std::exchange(m_early, { });
                m_earlyBytes = 0;
                for (auto& item : early) {
                    report(item.video ? yozora_hls_add_video(m_hls, item.dts, item.pts, item.sync, item.data.span().data(), item.data.size())
                                      : yozora_hls_add_audio(m_hls, item.pts, item.data.span().data(), item.data.size()), "epoch start", fail);
                }
            }
            if (!m_broken)
                report(yozora_hls_add_audio(m_hls, pts, data, size), "audio sample", fail);
        }
    }
    notify(started, fail);
}

void MseSession::end()
{
    bool fail = false;
    {
        Locker locker { m_lock };
        if (m_ended || m_broken)
            return;
        m_ended = true;
        if (m_hls)
            report(yozora_hls_end(m_hls), "end of stream", fail);
    }
    notify(0, fail);
}

// the callbacks run outside the lock, they may call back into the session
void MseSession::notify(unsigned started, bool failed)
{
    if (!started && !failed)
        return;
    RefPtr<Callbacks> callbacks;
    {
        Locker locker { m_lock };
        callbacks = m_callbacks;
    }
    if (!callbacks)
        return;
    if (started && callbacks->epochStarted)
        callbacks->epochStarted(started);
    if (failed && callbacks->failed)
        callbacks->failed("The stream of the media source broke"_s);
}

yozora_hls* MseSession::retainStream(unsigned* epoch)
{
    Locker locker { m_lock };
    if (epoch)
        *epoch = m_epoch;
    if (!m_hls)
        return nullptr;
    yozora_hls_retain(m_hls);
    return m_hls;
}

unsigned MseSession::epoch() const
{
    Locker locker { m_lock };
    return m_epoch;
}

bool MseSession::hasVideo() const
{
    Locker locker { m_lock };
    return m_wantVideo;
}

bool MseSession::hasAudio() const
{
    Locker locker { m_lock };
    return m_wantAudio;
}

unsigned MseSession::width() const
{
    Locker locker { m_lock };
    return m_width;
}

unsigned MseSession::height() const
{
    Locker locker { m_lock };
    return m_height;
}

namespace {

class YozoraMediaSample final : public MediaSample {
public:
    static Ref<YozoraMediaSample> create(TrackID track, MediaTime pts, MediaTime dts, MediaTime duration, bool sync, FloatSize size, Vector<uint8_t>&& data)
    {
        return adoptRef(*new YozoraMediaSample(track, pts, dts, duration, sync, size, WTF::move(data)));
    }

    std::span<const uint8_t> bytes() const { return m_data.span(); }

private:
    YozoraMediaSample(TrackID track, MediaTime pts, MediaTime dts, MediaTime duration, bool sync, FloatSize size, Vector<uint8_t>&& data)
        : m_track(track)
        , m_pts(pts)
        , m_dts(dts)
        , m_duration(duration)
        , m_sync(sync)
        , m_size(size)
        , m_data(WTF::move(data))
    {
    }

    MediaTime presentationTime() const final { return m_pts; }
    MediaTime decodeTime() const final { return m_dts; }
    MediaTime duration() const final { return m_duration; }
    TrackID trackID() const final { return m_track; }
    size_t sizeInBytes() const final { return m_data.size(); }
    FloatSize presentationSize() const final { return m_size; }
    void offsetTimestampsBy(const MediaTime& offset) final
    {
        m_pts += offset;
        m_dts += offset;
    }
    void setTimestamps(const MediaTime& presentation, const MediaTime& decode) final
    {
        m_pts = presentation;
        m_dts = decode;
    }
    SampleFlags flags() const final
    {
        unsigned flags = None;
        if (m_sync)
            flags |= IsSync;
        if (m_nonDisplaying)
            flags |= IsNonDisplaying;
        return SampleFlags(flags);
    }
    // nothing of the platform sample is read: the engine takes the bytes and WebCore never hands samples to a
    // system decoder
    PlatformSample platformSample() const final { return PlatformSample { static_cast<const MockSampleBox*>(nullptr) }; }
    Type type() const final { return Type::Yozora; }

    Ref<MediaSample> createNonDisplayingCopy() const final
    {
        auto copy = create(m_track, m_pts, m_dts, m_duration, m_sync, m_size, Vector<uint8_t>(m_data));
        copy->m_nonDisplaying = true;
        return copy;
    }

    Ref<MediaSample> createCopyWithAdjustedStartTime(const MediaTime& offset) const final
    {
        MediaTime clamped = std::max(MediaTime::zeroTime(), std::min(offset, m_duration));
        return create(m_track, m_pts + clamped, m_dts + clamped, m_duration - clamped, m_sync, m_size, Vector<uint8_t>(m_data));
    }

    TrackID m_track;
    MediaTime m_pts, m_dts, m_duration;
    bool m_sync;
    bool m_nonDisplaying { false };
    FloatSize m_size;
    Vector<uint8_t> m_data;
};

class YozoraMediaDescription final : public MediaDescription {
public:
    static Ref<YozoraMediaDescription> create(String&& codec, bool video) { return adoptRef(*new YozoraMediaDescription(WTF::move(codec), video)); }

    bool isVideo() const final { return m_video; }
    bool isAudio() const final { return !m_video; }
    bool isText() const final { return false; }

private:
    YozoraMediaDescription(String&& codec, bool video)
        : MediaDescription(WTF::move(codec))
        , m_video(video)
    {
    }
    const bool m_video;
};

class YozoraVideoTrack final : public VideoTrackPrivate {
public:
    static Ref<YozoraVideoTrack> create(TrackID id) { return adoptRef(*new YozoraVideoTrack(id)); }
    TrackID id() const final { return m_id; }

private:
    explicit YozoraVideoTrack(TrackID id) : m_id(id) { }
    TrackID m_id;
};

class YozoraAudioTrack final : public AudioTrackPrivate {
public:
    static Ref<YozoraAudioTrack> create(TrackID id) { return adoptRef(*new YozoraAudioTrack(id)); }
    TrackID id() const final { return m_id; }

private:
    explicit YozoraAudioTrack(TrackID id) : m_id(id) { }
    TrackID m_id;
};

// what a page may append: fragmented mp4 with H.264 and AAC, the only streams the oldest phones' system
// player plays after they become MPEG-TS
bool supported(const ContentType& type, bool& video, bool& audio)
{
    String container = type.containerType().convertToASCIILowercase();
    if (container != "video/mp4"_s && container != "audio/mp4"_s)
        return false;
    video = false;
    audio = false;
    auto codecs = type.codecs();
    if (codecs.isEmpty())
        return false;
    for (auto& codec : codecs) {
        String lower = codec.convertToASCIILowercase();
        if (lower.startsWith("avc1"_s) || lower.startsWith("avc3"_s))
            video = true;
        else if (lower.startsWith("mp4a.40."_s) || lower == "mp4a.67"_s)
            audio = true;
        else
            return false;
    }
    return video || audio;
}

class YozoraMediaSourcePrivate;

class YozoraSourceBufferPrivate final : public SourceBufferPrivate {
public:
    static Ref<YozoraSourceBufferPrivate> create(YozoraMediaSourcePrivate&, Ref<MseSession>&&);
    ~YozoraSourceBufferPrivate();

    constexpr MediaPlatformType platformType() const final { return MediaPlatformType::Yozora; }

private:
    YozoraSourceBufferPrivate(YozoraMediaSourcePrivate&, Ref<MseSession>&&);

    Ref<MediaPromise> appendInternal(Ref<SharedBuffer>&&) final;
    void resetParserStateInternal() final;
    bool canSwitchToType(const ContentType&) final;
    void flush(TrackID) final { m_session->flush(); }
    void enqueueSample(Ref<MediaSample>&&, TrackID) final;
    // the stream takes every sample at once, they wait in memory in the segments and the player trims what
    // lies behind the position
    bool isReadyForMoreSamples(TrackID) final { return true; }

    static int initialized(void* context, const yozora_mp4_track* tracks, unsigned count);
    static int sampled(void* context, const yozora_mp4_track* track, const yozora_mp4_sample* sample);

#if !RELEASE_LOG_DISABLED
    const Logger& logger() const final { return m_logger.get(); }
    ASCIILiteral logClassName() const override { return "YozoraSourceBufferPrivate"_s; }
    uint64_t logIdentifier() const final { return m_logIdentifier; }
    WTFLogChannel& logChannel() const final { return LogMediaSource; }
    const Logger& sourceBufferLogger() const final { return m_logger.get(); }
    uint64_t sourceBufferLogIdentifier() final { return logIdentifier(); }
    const Ref<const Logger> m_logger;
    const uint64_t m_logIdentifier;
#endif

    Ref<MseSession> m_session;
    yozora_mp4* m_parser { nullptr };
};

class YozoraMediaSourcePrivate final : public MediaSourcePrivate
#if !RELEASE_LOG_DISABLED
    , private LoggerHelper
#endif
{
public:
    static Ref<YozoraMediaSourcePrivate> create(MsePlayer& player, MediaSourcePrivateClient& client, Ref<MseSession>&& session)
    {
        Ref source = adoptRef(*new YozoraMediaSourcePrivate(player, client, WTF::move(session)));
        client.setPrivateAndOpen(source.copyRef());
        return source;
    }

    constexpr MediaPlatformType platformType() const final { return MediaPlatformType::Yozora; }
    RefPtr<MediaPlayerPrivateInterface> player() const final { return m_player.get(); }
    void setPlayer(MediaPlayerPrivateInterface* player) final { m_player = static_cast<MsePlayer*>(player); }

#if !RELEASE_LOG_DISABLED
    const Logger& logger() const final { return m_logger.get(); }
    ASCIILiteral logClassName() const override { return "YozoraMediaSourcePrivate"_s; }
    uint64_t logIdentifier() const final { return m_logIdentifier; }
    WTFLogChannel& logChannel() const final { return LogMediaSource; }
    uint64_t nextSourceBufferLogIdentifier() { return childLogIdentifier(m_logIdentifier, ++m_nextSourceBufferID); }
#endif

private:
    YozoraMediaSourcePrivate(MsePlayer& player, MediaSourcePrivateClient& client, Ref<MseSession>&& session)
        : MediaSourcePrivate(client)
        , m_player(&player)
        , m_session(WTF::move(session))
#if !RELEASE_LOG_DISABLED
        , m_logger(Logger::create(this))
        , m_logIdentifier(LoggerHelper::uniqueLogIdentifier())
#endif
    {
    }

    AddStatus addSourceBuffer(const ContentType& type, const MediaSourceConfiguration&, RefPtr<SourceBufferPrivate>& out) final
    {
        bool video = false, audio = false;
        if (!supported(type, video, audio))
            return AddStatus::NotSupported;
        // one video buffer and one audio buffer is the usual case and an epoch stream needs both known before
        // it starts
        yozora_log(YOZORA_LOG_INFO, "mse", "source buffer added: %s (video %d, audio %d)", type.raw().utf8().data(), video ? 1 : 0, audio ? 1 : 0);
        m_session->wantTracks(video, audio);
        {
            Locker locker { m_lock };
            m_sourceBuffers.append(YozoraSourceBufferPrivate::create(*this, m_session.copyRef()));
            out = m_sourceBuffers.last();
        }
        out->setMediaSourceDuration(duration());
        return AddStatus::Ok;
    }

    void durationChanged(const MediaTime& duration) final
    {
        MediaSourcePrivate::durationChanged(duration);
        if (RefPtr player = m_player.get())
            player->mseDurationChanged(duration);
    }

    void markEndOfStream(EndOfStreamStatus status) final
    {
        MediaSourcePrivate::markEndOfStream(status);
        // the data is all there: the last segment is cut and the playlist ends, so the system player knows where the stream stops
        if (status == EndOfStreamStatus::NoError)
            m_session->end();
    }

    void notifyActiveSourceBuffersChanged() final
    {
        if (RefPtr player = m_player.get())
            player->mseActiveSourceBuffersChanged();
    }

    friend class YozoraSourceBufferPrivate;

    WeakPtr<MsePlayer> m_player;
    Ref<MseSession> m_session;
#if !RELEASE_LOG_DISABLED
    const Ref<const Logger> m_logger;
    const uint64_t m_logIdentifier;
    uint64_t m_nextSourceBufferID { 0 };
#endif
};

Ref<YozoraSourceBufferPrivate> YozoraSourceBufferPrivate::create(YozoraMediaSourcePrivate& parent, Ref<MseSession>&& session)
{
    return adoptRef(*new YozoraSourceBufferPrivate(parent, WTF::move(session)));
}

YozoraSourceBufferPrivate::YozoraSourceBufferPrivate(YozoraMediaSourcePrivate& parent, Ref<MseSession>&& session)
    : SourceBufferPrivate(parent)
#if !RELEASE_LOG_DISABLED
    , m_logger(parent.logger())
    , m_logIdentifier(parent.nextSourceBufferLogIdentifier())
#endif
    , m_session(WTF::move(session))
{
    yozora_mp4_sink sink { initialized, sampled, this };
    if (yozora_mp4_create(&sink, &m_parser) != YOZORA_MP4_OK)
        m_parser = nullptr;
}

YozoraSourceBufferPrivate::~YozoraSourceBufferPrivate()
{
    yozora_mp4_destroy(m_parser);
}

bool YozoraSourceBufferPrivate::canSwitchToType(const ContentType& type)
{
    bool video, audio;
    return supported(type, video, audio);
}

void YozoraSourceBufferPrivate::resetParserStateInternal()
{
    yozora_mp4_reset(m_parser);
}

int YozoraSourceBufferPrivate::initialized(void* context, const yozora_mp4_track* tracks, unsigned count)
{
    auto& self = *static_cast<YozoraSourceBufferPrivate*>(context);
    InitializationSegment segment;
    // the length of the media is not in an initialization segment of a fragmented file, the page sets it
    segment.duration = MediaTime::invalidTime();
    for (unsigned i = 0; i < count; ++i) {
        const auto& track = tracks[i];
        char codec[40];
        if (!yozora_mp4_track_codec_string(&track, codec, sizeof codec))
            return 1;
        if (track.kind == YOZORA_MP4_TRACK_VIDEO) {
            InitializationSegment::VideoTrackInformation info;
            info.track = YozoraVideoTrack::create(track.id);
            info.description = YozoraMediaDescription::create(String::fromLatin1(codec), true);
            segment.videoTracks.append(WTF::move(info));
            self.m_session->setVideo(Vector<uint8_t>(std::span { track.config, track.config_size }), track.width, track.height);
        } else if (track.kind == YOZORA_MP4_TRACK_AUDIO) {
            InitializationSegment::AudioTrackInformation info;
            info.track = YozoraAudioTrack::create(track.id);
            info.description = YozoraMediaDescription::create(String::fromLatin1(codec), false);
            segment.audioTracks.append(WTF::move(info));
            self.m_session->setAudio(Vector<uint8_t>(std::span { track.config, track.config_size }));
        }
    }
    yozora_log(YOZORA_LOG_INFO, "mse", "init segment: %u tracks", count);
    self.didReceiveInitializationSegment(WTF::move(segment));
    return 0;
}

int YozoraSourceBufferPrivate::sampled(void* context, const yozora_mp4_track* track, const yozora_mp4_sample* sample)
{
    auto& self = *static_cast<YozoraSourceBufferPrivate*>(context);
    FloatSize size = track->kind == YOZORA_MP4_TRACK_VIDEO ? FloatSize(track->width, track->height) : FloatSize();
    self.didReceiveSample(YozoraMediaSample::create(track->id, MediaTime(sample->pts, track->timescale), MediaTime(sample->dts, track->timescale),
        MediaTime(sample->duration, track->timescale), sample->sync != 0, size, Vector<uint8_t>(std::span { sample->data, sample->size })));
    return 0;
}

Ref<MediaPromise> YozoraSourceBufferPrivate::appendInternal(Ref<SharedBuffer>&& data)
{
    if (!m_parser)
        return MediaPromise::createAndReject(PlatformMediaError::MemoryError);
    auto bytes = data->extractData();
    auto status = yozora_mp4_feed(m_parser, bytes.span().data(), bytes.size());
    if (status == YOZORA_MP4_OK)
        return MediaPromise::createAndResolve();
    // the bytes of a box that did not parse are dropped, the next append starts at a box boundary
    yozora_mp4_reset(m_parser);
    yozora_log(YOZORA_LOG_ERROR, "mse", "append refused by the parser (status %d)", static_cast<int>(status));
    return MediaPromise::createAndReject(status == YOZORA_MP4_NO_MEMORY ? PlatformMediaError::MemoryError : PlatformMediaError::ParsingError);
}

void YozoraSourceBufferPrivate::enqueueSample(Ref<MediaSample>&& sample, TrackID)
{
    if (sample->type() != MediaSample::Type::Yozora)
        return;
    auto& own = static_cast<YozoraMediaSample&>(sample.get());
    auto bytes = own.bytes();
    int64_t pts = toHlsTime(sample->presentationTime());
    int64_t dts = toHlsTime(sample->decodeTime());
    // a sample with a size is a picture, the tracks of this engine are told apart by their size
    if (sample->presentationSize().isEmpty())
        m_session->addAudio(pts, bytes.data(), bytes.size());
    else
        m_session->addVideo(dts, pts, sample->isSync(), bytes.data(), bytes.size());
}

}

RefPtr<WebCore::MediaSourcePrivate> createMediaSourcePrivate(MsePlayer& player, WebCore::MediaSourcePrivateClient& client, Ref<MseSession>&& session)
{
    return YozoraMediaSourcePrivate::create(player, client, WTF::move(session));
}

}

#endif
