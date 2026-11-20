#pragma once

#include <wtf/Platform.h>

#if ENABLE(MEDIA_SOURCE)

#include "MediaPlayerPrivate.h"
#include "MediaSourcePrivate.h"
#include "yozora_hls.h"
#include <wtf/Function.h>
#include <wtf/Lock.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/WeakPtr.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class MediaSourcePrivateClient;
}

// what a page appends becomes the stream of one playback epoch: samples WebCore hands to the engine are cut
// into MPEG-TS segments and served from memory as an HLS playlist (core/yozora_hls.c). a seek that WebCore
// answers by taking its buffers back and resending from a key frame starts a new epoch, the player gets the
// new stream and goes to the place in it
namespace Yozora {

// shared by the source buffers (their thread) and the player (the page thread), every call is safe from any thread
class MseSession final : public ThreadSafeRefCounted<MseSession> {
public:
    static Ref<MseSession> create();
    ~MseSession();

    // the player sets these once before samples arrive. both may be called on any thread, the callbacks hop
    // to the page thread themselves
    void setCallbacks(Function<void(unsigned epoch)>&& epochStarted, Function<void(String&&)>&& failed);
    void wantTracks(bool video, bool audio);
    void setVideo(Vector<uint8_t>&& avcc, unsigned width, unsigned height);
    void setAudio(Vector<uint8_t>&& audioSpecificConfig);
    // a seek: the next sample starts a new epoch
    void flush();
    void addVideo(int64_t dts, int64_t pts, bool sync, const uint8_t* data, size_t size);
    void addAudio(int64_t pts, const uint8_t* data, size_t size);
    // endOfStream: what is left is cut and the playlist ends
    void end();

    // the stream of the current epoch, retained (release it with yozora_hls_release), null before the first one
    yozora_hls* retainStream(unsigned* epoch);
    unsigned epoch() const;
    bool hasVideo() const;
    bool hasAudio() const;
    unsigned width() const;
    unsigned height() const;

private:
    MseSession();
    bool ensureEpoch(unsigned& startedEpoch) WTF_REQUIRES_LOCK(m_lock);
    void report(yozora_hls_status, const char* what, bool& fail) WTF_REQUIRES_LOCK(m_lock);
    void notify(unsigned startedEpoch, bool failed);

    struct Early {
        bool video;
        int64_t dts, pts;
        bool sync;
        Vector<uint8_t> data;
    };

    // the two callbacks of the player, shared so that they can be called after the lock is let go
    struct Callbacks : ThreadSafeRefCounted<Callbacks> {
        Function<void(unsigned)> epochStarted;
        Function<void(String&&)> failed;
    };

    mutable Lock m_lock;
    RefPtr<Callbacks> m_callbacks WTF_GUARDED_BY_LOCK(m_lock);
    bool m_wantVideo WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_wantAudio WTF_GUARDED_BY_LOCK(m_lock) { false };
    Vector<uint8_t> m_avcc WTF_GUARDED_BY_LOCK(m_lock);
    Vector<uint8_t> m_audioConfig WTF_GUARDED_BY_LOCK(m_lock);
    unsigned m_width WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    unsigned m_height WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    yozora_hls* m_hls WTF_GUARDED_BY_LOCK(m_lock) { nullptr };
    unsigned m_epoch WTF_GUARDED_BY_LOCK(m_lock) { 0 };
    // after a seek, or at the start: the next sample opens an epoch
    bool m_needsEpoch WTF_GUARDED_BY_LOCK(m_lock) { true };
    // the epoch broke (a sample the muxer refused, no memory): nothing more goes into it until the next seek
    bool m_broken WTF_GUARDED_BY_LOCK(m_lock) { false };
    bool m_ended WTF_GUARDED_BY_LOCK(m_lock) { false };
    // samples that came before the config of every wanted track, kept for the epoch that they open
    Vector<Early> m_early WTF_GUARDED_BY_LOCK(m_lock);
    size_t m_earlyBytes WTF_GUARDED_BY_LOCK(m_lock) { 0 };
};

class MsePlayer;

// the MediaSource of the page for the engine of the interface
RefPtr<WebCore::MediaSourcePrivate> createMediaSourcePrivate(MsePlayer&, WebCore::MediaSourcePrivateClient&, Ref<MseSession>&&);

// what the media source needs of the player, implemented by the player of YozoraMedia.cpp
class MsePlayer : public WebCore::MediaPlayerPrivateInterface, public CanMakeWeakPtr<MsePlayer> {
public:
    virtual void mseDurationChanged(const MediaTime&) = 0;
    virtual void mseActiveSourceBuffersChanged() = 0;
};

}

#endif
