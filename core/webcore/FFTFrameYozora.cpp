// web audio frames (analyser, convolver, panner) hold a real transform in the packed form of vDSP: real[0] is
// the DC bin, imag[0] the Nyquist bin, bins 1 to N/2 - 1 are complex. forward is the plain DFT without
// scaling, the inverse restores the input exactly. N real samples are one complex transform of N/2 points,
// even samples as real parts and odd as imaginary

#include "config.h"

#if ENABLE(WEB_AUDIO)

#include "FFTFrame.h"

#include <pthread.h>
#include <cmath>
#include <mutex>
#include <numbers>
#include <vector>
#include <wtf/StdLibExtras.h>

namespace WebCore {

namespace {

constexpr int minPow2Size = 2;
constexpr int maxPow2Size = 24;

// the twiddle factors and the bit reversal of one transform size, made once and shared by every frame of that size
struct Plan {
    unsigned points;
    std::vector<float> cosine, sine;
    std::vector<uint32_t> reverse;
};

const Plan& planFor(unsigned points)
{
    static std::mutex lock;
    static std::vector<std::unique_ptr<Plan>> plans;
    std::lock_guard guard(lock);
    for (auto& plan : plans) {
        if (plan->points == points)
            return *plan;
    }
    auto plan = std::make_unique<Plan>();
    plan->points = points;
    plan->cosine.resize(points / 2);
    plan->sine.resize(points / 2);
    for (unsigned i = 0; i < points / 2; ++i) {
        double angle = -2.0 * std::numbers::pi * i / points;
        plan->cosine[i] = static_cast<float>(std::cos(angle));
        plan->sine[i] = static_cast<float>(std::sin(angle));
    }
    plan->reverse.resize(points);
    unsigned bits = 0;
    while ((1u << bits) < points)
        ++bits;
    for (unsigned i = 0; i < points; ++i) {
        uint32_t reversed = 0;
        for (unsigned bit = 0; bit < bits; ++bit)
            reversed |= ((i >> bit) & 1u) << (bits - 1 - bit);
        plan->reverse[i] = reversed;
    }
    plans.push_back(std::move(plan));
    return *plans.back();
}

// an in place transform of split complex data, inverse takes the conjugate twiddles and no scale
void transform(const Plan& plan, float* real, float* imag, bool inverse)
{
    unsigned points = plan.points;
    for (unsigned i = 0; i < points; ++i) {
        unsigned j = plan.reverse[i];
        if (j > i) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }
    for (unsigned size = 2; size <= points; size <<= 1) {
        unsigned half = size / 2;
        unsigned step = points / size;
        for (unsigned start = 0; start < points; start += size) {
            for (unsigned k = 0; k < half; ++k) {
                float wr = plan.cosine[k * step];
                float wi = inverse ? -plan.sine[k * step] : plan.sine[k * step];
                unsigned a = start + k, b = a + half;
                float tr = real[b] * wr - imag[b] * wi;
                float ti = real[b] * wi + imag[b] * wr;
                real[b] = real[a] - tr;
                imag[b] = imag[a] - ti;
                real[a] += tr;
                imag[a] += ti;
            }
        }
    }
}

// the audio thread must not allocate for each call: the scratch of a thread grows to the largest size it has used
struct Scratch {
    std::vector<float> real, imag;
};

// clang refuses thread_local for ios before 8 (the armv7 slice builds for ios 5.0), so the scratch of a
// thread is kept under a pthread key and deleted when the thread ends
void destroyScratch(void* scratch)
{
    delete static_cast<Scratch*>(scratch);
}

Scratch& scratchFor(unsigned points)
{
    static pthread_key_t key;
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, [] { pthread_key_create(&key, destroyScratch); });
    auto* scratch = static_cast<Scratch*>(pthread_getspecific(key));
    if (!scratch) {
        scratch = new Scratch;
        pthread_setspecific(key, scratch);
    }
    if (scratch->real.size() < points) {
        scratch->real.resize(points);
        scratch->imag.resize(points);
    }
    return *scratch;
}

} // namespace

FFTFrame::FFTFrame(unsigned fftSize)
    : m_FFTSize(fftSize)
    , m_log2FFTSize(static_cast<unsigned>(std::log2(fftSize)))
    , m_realData(fftSize / 2)
    , m_imagData(fftSize / 2)
{
    ASSERT(fftSize >= static_cast<unsigned>(minFFTSize()) && fftSize <= static_cast<unsigned>(maxFFTSize()) && !(fftSize & (fftSize - 1)));
}

FFTFrame::FFTFrame()
    : m_FFTSize(0)
    , m_log2FFTSize(0)
{
}

FFTFrame::FFTFrame(const FFTFrame& frame)
    : m_FFTSize(frame.m_FFTSize)
    , m_log2FFTSize(frame.m_log2FFTSize)
    , m_realData(frame.m_FFTSize / 2)
    , m_imagData(frame.m_FFTSize / 2)
{
    if (m_FFTSize) {
        memcpySpan(m_realData.span(), frame.realData().span());
        memcpySpan(m_imagData.span(), frame.imagData().span());
    }
}

FFTFrame::~FFTFrame() = default;

void FFTFrame::initialize()
{
}

int FFTFrame::minFFTSize()
{
    return 1 << minPow2Size;
}

int FFTFrame::maxFFTSize()
{
    return 1 << maxPow2Size;
}

void FFTFrame::doFFT(std::span<const float> data)
{
    ASSERT(data.size() >= m_FFTSize);
    unsigned half = m_FFTSize / 2;
    auto& plan = planFor(half);
    auto& scratch = scratchFor(half);
    float* real = scratch.real.data();
    float* imag = scratch.imag.data();
    for (unsigned i = 0; i < half; ++i) {
        real[i] = data[2 * i];
        imag[i] = data[2 * i + 1];
    }
    transform(plan, real, imag, false);

    auto realOut = m_realData.span();
    auto imagOut = m_imagData.span();
    // the even and odd parts come apart from Z[k] and the conjugate of Z[N/2 - k], the odd part is turned by e^(-2 pi i k / N)
    double angleStep = -2.0 * std::numbers::pi / m_FFTSize;
    for (unsigned k = 1; k < half; ++k) {
        float zr = real[k], zi = imag[k];
        float cr = real[half - k], ci = -imag[half - k];
        float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
        float dr = 0.5f * (zr - cr), di = 0.5f * (zi - ci);
        // divided by 2i: (dr + i di) / i = di - i dr
        float or_ = di, oi = -dr;
        float wr = static_cast<float>(std::cos(angleStep * k)), wi = static_cast<float>(std::sin(angleStep * k));
        realOut[k] = er + or_ * wr - oi * wi;
        imagOut[k] = ei + or_ * wi + oi * wr;
    }
    realOut[0] = real[0] + imag[0];
    imagOut[0] = real[0] - imag[0];
}

void FFTFrame::doInverseFFT(std::span<float> data)
{
    ASSERT(data.size() >= m_FFTSize);
    unsigned half = m_FFTSize / 2;
    auto& plan = planFor(half);
    auto& scratch = scratchFor(half);
    float* real = scratch.real.data();
    float* imag = scratch.imag.data();

    auto realIn = m_realData.span();
    auto imagIn = m_imagData.span();
    double angleStep = 2.0 * std::numbers::pi / m_FFTSize;
    // E[k] = (X[k] + conj(X[N/2 - k])) / 2 and O[k] = (X[k] - conj(X[N/2 - k])) e^(+2 pi i k / N) / 2, then Z[k] = E[k] + i O[k]
    for (unsigned k = 0; k < half; ++k) {
        float xr, xi, yr, yi;
        if (k == 0) {
            xr = realIn[0];
            xi = 0;
            yr = imagIn[0];
            yi = 0;
        } else {
            xr = realIn[k];
            xi = imagIn[k];
            yr = realIn[half - k];
            yi = imagIn[half - k];
        }
        float er = 0.5f * (xr + yr), ei = 0.5f * (xi - yi);
        float dr = 0.5f * (xr - yr), di = 0.5f * (xi + yi);
        float wr = static_cast<float>(std::cos(angleStep * k)), wi = static_cast<float>(std::sin(angleStep * k));
        float or_ = dr * wr - di * wi, oi = dr * wi + di * wr;
        real[k] = er - oi;
        imag[k] = ei + or_;
    }
    transform(plan, real, imag, true);

    float scale = 1.0f / half;
    for (unsigned i = 0; i < half; ++i) {
        data[2 * i] = real[i] * scale;
        data[2 * i + 1] = imag[i] * scale;
    }
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
