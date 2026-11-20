// FFTFrameYozora.cpp against a plain DFT in the packed form Web Audio expects: DC in real[0], Nyquist in
// imag[0], the inverse restores the input. the WebCore headers are replaced by the small ones of
// tests/fft_stubs
#include "../core/webcore/FFTFrameYozora.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

using namespace WebCore;

static int failures;

static void check(bool ok, const char* what, unsigned size)
{
    if (!ok) {
        std::fprintf(stderr, "fft frame: %s failed for size %u\n", what, size);
        ++failures;
    }
}

int main()
{
    for (unsigned size : { 4u, 8u, 64u, 128u, 1024u, 2048u, 16384u }) {
        std::vector<float> input(size);
        unsigned seed = size;
        for (auto& value : input) {
            seed = seed * 1664525u + 1013904223u;
            value = static_cast<float>((seed >> 8) & 0xffff) / 32768.0f - 1.0f;
        }
        FFTFrame frame(size);
        frame.doFFT(input);
        double tolerance = 2e-3 * std::sqrt(static_cast<double>(size));
        // the plain DFT of a few bins, the whole spectrum for the small sizes
        unsigned bins = size <= 128 ? size / 2 : 16;
        double worst = 0;
        for (unsigned b = 0; b <= bins; ++b) {
            unsigned k = size <= 128 ? b : b * (size / 2) / bins;
            double re = 0, im = 0;
            for (unsigned n = 0; n < size; ++n) {
                double angle = -2.0 * std::numbers::pi * ((static_cast<unsigned long long>(k) * n) % size) / size;
                re += input[n] * std::cos(angle);
                im += input[n] * std::sin(angle);
            }
            double gotRe, gotIm;
            if (k == 0) {
                gotRe = frame.realData().span()[0];
                gotIm = 0;
            } else if (k == size / 2) {
                gotRe = frame.imagData().span()[0];
                gotIm = 0;
            } else {
                gotRe = frame.realData().span()[k];
                gotIm = frame.imagData().span()[k];
            }
            worst = std::max(worst, std::max(std::fabs(gotRe - re), std::fabs(gotIm - im)));
        }
        check(worst < tolerance, "forward transform differs from the plain DFT", size);
        std::vector<float> back(size);
        frame.doInverseFFT(back);
        double roundTrip = 0;
        for (unsigned n = 0; n < size; ++n)
            roundTrip = std::max(roundTrip, static_cast<double>(std::fabs(back[n] - input[n])));
        check(roundTrip < 1e-4, "inverse does not restore the input", size);
        // a copy keeps the spectrum
        FFTFrame copy(frame);
        std::vector<float> again(size);
        copy.doInverseFFT(again);
        check(again == back, "a copy gives another signal", size);
        std::printf("fft frame: size %u worst bin error %.2e, round trip %.2e\n", size, worst, roundTrip);
    }
    // a sine of exactly bin 5 puts all its energy in that bin
    {
        unsigned size = 256;
        std::vector<float> sine(size);
        for (unsigned n = 0; n < size; ++n)
            sine[n] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 5 * n / size));
        FFTFrame frame(size);
        frame.doFFT(sine);
        double peak = std::hypot(frame.realData().span()[5], frame.imagData().span()[5]);
        check(std::fabs(peak - size / 2.0) < 1e-2, "the bin of a sine is not N/2", size);
        check(std::hypot(frame.realData().span()[6], frame.imagData().span()[6]) < 1e-2, "energy leaked to the next bin", size);
    }
    check(FFTFrame::minFFTSize() == 4 && FFTFrame::maxFFTSize() == (1 << 24), "size limits", 0);
    if (failures)
        return 1;
    std::puts("fft frame: forward and inverse transforms match the DFT");
    return 0;
}
