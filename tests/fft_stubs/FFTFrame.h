#pragma once
// the shape of WebCore's FFTFrame as far as FFTFrameYozora.cpp uses it
#include <memory>
#include <span>
#include <vector>
namespace WebCore {
class AudioFloatArray {
public:
    AudioFloatArray() = default;
    explicit AudioFloatArray(size_t size) : m_data(size, 0.0f) { }
    std::span<float> span() { return m_data; }
    std::span<const float> span() const { return m_data; }
    float& operator[](size_t i) { return m_data[i]; }
private:
    std::vector<float> m_data;
};
class FFTFrame {
public:
    FFTFrame(unsigned fftSize);
    FFTFrame();
    FFTFrame(const FFTFrame&);
    ~FFTFrame();
    static void initialize();
    void doFFT(std::span<const float>);
    void doInverseFFT(std::span<float>);
    AudioFloatArray& realData() { return m_realData; }
    AudioFloatArray& imagData() { return m_imagData; }
    const AudioFloatArray& realData() const { return m_realData; }
    const AudioFloatArray& imagData() const { return m_imagData; }
    static int minFFTSize();
    static int maxFFTSize();
    unsigned fftSize() const { return m_FFTSize; }
private:
    unsigned m_FFTSize;
    unsigned m_log2FFTSize;
    AudioFloatArray m_realData;
    AudioFloatArray m_imagData;
};
}
