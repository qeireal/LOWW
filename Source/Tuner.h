#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace loww {
// Difference-function pitch detector. Called by the editor, never by the audio callback.
struct TunerReading { double frequency = 0, cents = 0; int midi = 0; };
inline TunerReading detectPitch(const float* input, int count, double rate) {
    std::array<double, 4096> samples {}, difference {};
    const int stride = std::max(1, (int)(rate / 12000));
    const int n = std::min(4096, count / stride);
    const double sr = rate / stride;
    if (n < 128) return {};
    double energy = 0;
    for (int i = 0; i < n; ++i) {
        double value = 0;
        for (int j = 0; j < stride; ++j) value += input[i * stride+j];
        samples[(size_t)i] = value / stride; energy += samples[(size_t)i] * samples[(size_t)i];
    }
    if (energy / n < 1e-7) return {};
    const int low = std::max(2, (int)(sr / 1000)), high = std::min(n / 2 - 1, (int)(sr / 28));
    const int window = n - high;
    double sum = 0;
    difference[0] = 1;
    for (int lag = 1; lag <= high; ++lag) {
        double d = 0;
        for (int i = 0; i < window; ++i) { const auto delta = samples[(size_t)i] - samples[(size_t)(i+lag)]; d += delta*delta; }
        sum += d; difference[(size_t)lag] = sum > 0 ? d * lag / sum : 1;
    }
    for (int lag = low; lag < high; ++lag) {
        if (difference[(size_t)lag] >= .12) continue;
        while (lag+1 < high && difference[(size_t)(lag+1)] < difference[(size_t)lag]) ++lag;
        const auto a = difference[(size_t)(lag-1)], b = difference[(size_t)lag], c = difference[(size_t)(lag+1)];
        const auto denominator = a - 2*b + c;
        const auto offset = std::abs(denominator) > 1e-12 ? .5*(a-c)/denominator : 0;
        const double frequency = sr / (lag + std::clamp(offset,-.5,.5));
        const double note = 69 + 12 * std::log2(frequency/440.0);
        const int midi = (int)std::round(note);
        return {frequency, (note-midi)*100, midi};
    }
    return {};
}
}
