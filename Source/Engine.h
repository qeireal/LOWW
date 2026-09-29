#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace loww {
struct Settings {
    float input = 0, output = -6, mix = 1, gate = -65;
    float lowCut = 200, highCut = 800, compression = .5f;
    float midDrive = .35f, highDrive = .25f;
    float lowLevel = 0, midLevel = -6, highLevel = -9;
    std::array<float, 6> eq {};
    bool parallel = false, monoLow = false;
    float transpose = 0;
    std::array<float, 2> cabLevel {0, -6}, cabPan {0, 0};
    std::array<bool, 2> cabEnabled {true, false}, cabPhase {false, false};
    bool gateOn = false, lowOn = true, midOn = true, highOn = true;
    bool eqOn = true, cabOn = true, bypass = false;
};

// Owned by the audio thread. All working storage is reserved in prepare().
class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize, float transpose = 0);
    void reset();
    void process(juce::AudioBuffer<float>&, const Settings&);
    int latency() const noexcept { return latencySamples; }
    void loadIR(juce::AudioBuffer<float>&&, double sampleRate, int slot = 0);
    void useStockCab(int slot = 0);
    float reductionDb() const noexcept { return gainReduction; }
    static juce::AudioBuffer<float> makeStockIR();
private:
    using Crossover = juce::dsp::LinkwitzRileyFilter<float>;
    struct Biquad {
        std::array<double, 5> c {1, 0, 0, 0, 0};
        std::array<double, 5> target {1, 0, 0, 0, 0};
        double slew = .001;
        std::array<double, 2> z1 {}, z2 {};
        float tick(float x, int ch) noexcept;
        void setPeak(double sr, double hz, double db);
        void setShelf(double sr, double hz, double db, bool high);
        void setBandPass(double sr, double hz);
        void reset() { z1.fill(0); z2.fill(0); }
    };
    double rate = 48000;
    int capacity = 512, latencySamples = 0;
    juce::AudioBuffer<float> bands, dry, cabB, pitchInput, pitched;
    signalsmith::stretch::SignalsmithStretch<float> pitch;
    int baseLatency = 0, pitchLatency = 0;
    bool previousParallel = false;
    float gainReduction = 0;
    juce::SmoothedValue<float> monoBlend;
    Biquad fixedMid;
    juce::dsp::Oversampling<float> oversampling {6, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true};
    Crossover lowSplit, highSplit, lowPhase;
    std::array<juce::dsp::Convolution, 2> cabinet;
    std::array<juce::SmoothedValue<float>, 2> cabGains, cabPans;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelay {32768};
    std::array<Biquad, 6> equalizer;
    std::array<float, 2> compEnvelope {};
    float gateEnvelope = 0, gateGain = 1;
    juce::SmoothedValue<float> inputGain, outputGain, wetMix, bypassBlend, midAmount, highAmount;
    juce::SmoothedValue<float> lowGain, midGain, highGain, compAmount, cabMix, eqMix;
    juce::SmoothedValue<float> lowFrequency, highFrequency;
    std::array<float, 6> previousEq {999, 999, 999, 999, 999, 999};
};
}
