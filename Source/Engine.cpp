#include "Engine.h"
#include <cmath>

namespace loww {
float Engine::Biquad::tick(float x, int ch) noexcept {
    if (ch == 0) for (size_t i = 0; i < c.size(); ++i) c[i] += slew * (target[i] - c[i]);
    const auto y = c[0] * x + z1[(size_t) ch];
    z1[(size_t) ch] = c[1] * x - c[3] * y + z2[(size_t) ch];
    z2[(size_t) ch] = c[2] * x - c[4] * y;
    return (float) y;
}
void Engine::Biquad::setPeak(double sr, double hz, double db) {
    hz = juce::jmin(hz, sr * .45);
    slew = 1 - std::exp(-1 / (.015 * sr));
    const auto a = std::pow(10.0, db / 40.0);
    const auto w = juce::MathConstants<double>::twoPi * hz / sr;
    const auto alpha = std::sin(w) / 1.6;
    const auto a0 = 1 + alpha / a;
    target = {(1 + alpha * a) / a0, -2 * std::cos(w) / a0,
         (1 - alpha * a) / a0, -2 * std::cos(w) / a0, (1 - alpha / a) / a0};
}
void Engine::Biquad::setShelf(double sr, double hz, double db, bool high) {
    hz = juce::jmin(hz, sr * .45);
    slew = 1 - std::exp(-1 / (.015 * sr));
    auto coeff = high ? juce::dsp::IIR::ArrayCoefficients<double>::makeHighShelf(sr, hz, .70710678, std::pow(10.0, db / 20))
                      : juce::dsp::IIR::ArrayCoefficients<double>::makeLowShelf(sr, hz, .70710678, std::pow(10.0, db / 20));
    target = {coeff[0]/coeff[3], coeff[1]/coeff[3], coeff[2]/coeff[3], coeff[4]/coeff[3], coeff[5]/coeff[3]};
}
void Engine::Biquad::setBandPass(double sr, double hz) {
    auto coeff = juce::dsp::IIR::ArrayCoefficients<double>::makeBandPass(sr, hz, .70710678);
    c = target = {coeff[0]/coeff[3], coeff[1]/coeff[3], coeff[2]/coeff[3], coeff[4]/coeff[3], coeff[5]/coeff[3]};
}
void Engine::prepare(double sr, int maxBlockSize, float transpose) {
    rate = sr;
    capacity = juce::jmax(1, maxBlockSize);
    bands.setSize(6, capacity);
    dry.setSize(2, capacity); cabB.setSize(2, capacity);
    pitchInput.setSize(2, capacity); pitched.setSize(2, capacity);
    pitch.presetDefault(2, (float)sr);
    pitchLatency = pitch.inputLatency() + pitch.outputLatency();
    oversampling.initProcessing((size_t) capacity);
    baseLatency = juce::roundToInt(oversampling.getLatencyInSamples());
    latencySamples = baseLatency + (std::abs(transpose) > .001f ? pitchLatency : 0);
    const juce::dsp::ProcessSpec internal {sr * 4, (juce::uint32) capacity * 4, 2};
    for (auto* filter : {&lowSplit, &highSplit, &lowPhase}) filter->prepare(internal);
    lowPhase.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    for (auto& cab : cabinet) cab.prepare({sr, (juce::uint32) capacity, 2});
    fixedMid.setBandPass(sr * 4, 400);
    monoBlend.reset(sr * 4, .02);
    for (int i = 0; i < 2; ++i) { cabGains[(size_t)i].reset(sr, .02); cabPans[(size_t)i].reset(sr, .02); }
    dryDelay.setMaximumDelayInSamples(baseLatency + pitchLatency + 1);
    dryDelay.prepare({sr, (juce::uint32) capacity, 2});
    dryDelay.setDelay((float) latencySamples);
    for (auto* sm : {&inputGain, &outputGain, &wetMix, &bypassBlend, &cabMix, &eqMix}) sm->reset(sr, .02);
    for (auto* sm : {&midAmount, &highAmount, &lowGain, &midGain, &highGain, &compAmount})
        sm->reset(sr * 4, .02);
    lowFrequency.reset(sr * 4, .03); highFrequency.reset(sr * 4, .03);
    reset();
}
void Engine::reset() {
    oversampling.reset();
    for (auto* filter : {&lowSplit, &highSplit, &lowPhase}) filter->reset();
    for (auto& eq : equalizer) eq.reset();
    for (auto& cab : cabinet) cab.reset();
    pitch.reset(); fixedMid.reset(); gainReduction = 0;
    dryDelay.reset();
    compEnvelope.fill(0);
    gateEnvelope = 0;
    gateGain = 1;
    previousEq.fill(999);
    primed = false;
}
void Engine::loadIR(juce::AudioBuffer<float>&& ir, double sr, int slot) {
    cabinet[(size_t)juce::jlimit(0, 1, slot)].loadImpulseResponse(std::move(ir), sr, juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
}
void Engine::useStockCab(int slot) {
    loadIR(makeStockIR(), 48000, slot);
}
juce::AudioBuffer<float> Engine::makeStockIR() {
    // Original synthetic cabinet: a short, windowed band-pass impulse with reflections.
    // No factory IRs or other assets from the reference product are used.
    constexpr int n = 1024;
    constexpr double sr = 48000;
    juce::AudioBuffer<float> ir(1, n);
    ir.clear();
    auto* h = ir.getWritePointer(0);
    for (int i = 0; i < 385; ++i) {
        const auto t = (double) (i - 192);
        const auto sinc = [t, i](double f) { return i == 192 ? 2 * f / sr
            : std::sin(2 * juce::MathConstants<double>::pi * f * t / sr)
                / (juce::MathConstants<double>::pi * t); };
        const auto window = .5 - .5 * std::cos(2 * juce::MathConstants<double>::pi * i / 384);
        const auto v = (float) ((sinc(4800) - sinc(100)) * window);
        h[i] += v;
        h[i + 71] += .15f * v;
        h[i + 139] -= .08f * v;
    }
    return ir;
}
void Engine::process(juce::AudioBuffer<float>& buffer, const Settings& s) {
    const auto count = buffer.getNumSamples();
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (count == 0 || channels == 0) return;
    // Hosts may occasionally exceed their announced maximum; process bounded chunks.
    if (count > capacity) {
        for (int offset = 0; offset < count; offset += capacity) {
            juce::AudioBuffer<float> part(buffer.getArrayOfWritePointers(), channels,
                offset, juce::jmin(capacity, count - offset));
            process(part, s);
        }
        return;
    }
    const bool shift = std::abs(s.transpose) > .001f;
    latencySamples = baseLatency + (shift ? pitchLatency : 0);
    dryDelay.setDelay((float)latencySamples);
    pitchInput.clear();
    for (int ch = 0; ch < channels; ++ch) pitchInput.copyFrom(ch, 0, buffer, ch, 0, count);
    pitch.setTransposeSemitones(s.transpose);
    pitch.process(pitchInput.getArrayOfReadPointers(), count, pitched.getArrayOfWritePointers(), count);
    monoBlend.setTargetValue(s.monoLow ? 1.f : 0.f);
    for (size_t c = 0; c < 2; ++c) {
        cabGains[c].setTargetValue(s.cabEnabled[c] ? juce::Decibels::decibelsToGain(s.cabLevel[c]) * (s.cabPhase[c] ? -1.f : 1.f) : 0.f);
        cabPans[c].setTargetValue(s.cabPan[c]);
    }
    if (previousParallel != s.parallel) { previousEq.fill(999); previousParallel = s.parallel; }
    inputGain.setTargetValue(juce::Decibels::decibelsToGain(s.input));
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(s.output));
    wetMix.setTargetValue(s.mix);
    bypassBlend.setTargetValue(s.bypass ? 0.0f : 1.0f);
    midAmount.setTargetValue(s.midOn ? s.midDrive : 0);
    highAmount.setTargetValue(s.highOn ? s.highDrive : 0);
    compAmount.setTargetValue(s.lowOn ? s.compression : 0);
    lowGain.setTargetValue(juce::Decibels::decibelsToGain(s.lowLevel));
    midGain.setTargetValue(juce::Decibels::decibelsToGain(s.midLevel));
    highGain.setTargetValue(juce::Decibels::decibelsToGain(s.highLevel));
    cabMix.setTargetValue(s.cabOn ? 1.0f : 0.0f);
    eqMix.setTargetValue(s.eqOn ? 1.0f : 0.0f);
    lowFrequency.setTargetValue(s.lowCut);
    highFrequency.setTargetValue(s.highCut);
    constexpr std::array<double, 6> frequencies {80, 250, 500, 1000, 1500, 5000};
    for (size_t i = 0; i < equalizer.size(); ++i)
        if (std::abs(s.eq[i] - previousEq[i]) > .00001f) {
            if (s.parallel && (i == 0 || i == 5)) equalizer[i].setShelf(rate, i == 0 ? 100 : 5000, s.eq[i], i == 5);
            else equalizer[i].setPeak(rate, frequencies[i], s.eq[i]);
            previousEq[i] = s.eq[i];
        }
    if (!primed) {
        // After prepare or reset, start at the current settings instead of ramping from stale values.
        for (auto* sm : {&monoBlend, &cabGains[0], &cabGains[1], &cabPans[0], &cabPans[1], &inputGain, &outputGain, &wetMix,
                         &bypassBlend, &midAmount, &highAmount, &lowGain, &midGain, &highGain, &compAmount, &cabMix, &eqMix,
                         &lowFrequency, &highFrequency})
            sm->setCurrentAndTargetValue(sm->getTargetValue());
        for (auto& band : equalizer) band.c = band.target;
        primed = true;
    }
    bands.clear();
    const auto gateThreshold = juce::Decibels::decibelsToGain(s.gate);
    const auto gateRelease = (float) std::exp(-1.0 / (.08 * rate));
    const auto gateAttack = (float) std::exp(-1.0 / (.002 * rate));
    for (int i = 0; i < count; ++i) {
        const auto inGain = inputGain.getNextValue();
        float peak = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const auto raw = buffer.getSample(ch, i);
            dryDelay.pushSample(ch, raw);
            dry.setSample(ch, i, dryDelay.popSample(ch));
            // Detect on the signal being gated; the pitched path lags the raw input.
            peak = juce::jmax(peak, std::abs((shift ? pitched.getSample(ch, i) : raw) * inGain));
        }
        gateEnvelope = juce::jmax(peak, gateEnvelope * gateRelease);
        const auto target = !s.gateOn || gateEnvelope >= gateThreshold ? 1.0f : 0.0f;
        const auto coeff = target > gateGain ? gateAttack : gateRelease;
        gateGain = target + coeff * (gateGain - target);
        for (int ch = 0; ch < channels; ++ch)
            bands.setSample(ch, i, (shift ? pitched.getSample(ch, i) : buffer.getSample(ch, i)) * inGain * gateGain);
    }
    auto block = juce::dsp::AudioBlock<float>(bands).getSubBlock(0, (size_t) count);
    auto up = oversampling.processSamplesUp(block);
    const auto compAttack = (float) std::exp(-1.0 / ((s.parallel ? .003 : .015) * rate * 4));
    const auto compRelease = (float) std::exp(-1.0 / ((s.parallel ? .6 : .12) * rate * 4));
    const auto distort = [](float x, float amount, float asymmetry) {
        const auto gain = 1.0f + 24.0f * amount * amount;
        const auto bias = asymmetry * amount;
        const auto shaped = (std::tanh(x * gain + bias) - std::tanh(bias))
            / std::sqrt(gain);
        return x + amount * (shaped - x);
    };
    for (size_t i = 0; i < up.getNumSamples(); ++i) {
        if (i % 32 == 0) {
            const auto step = (int) juce::jmin((size_t) 32, up.getNumSamples() - i);
            lowSplit.setCutoffFrequency(lowFrequency.skip(step));
            const auto highHz = highFrequency.skip(step);
            highSplit.setCutoffFrequency(highHz); lowPhase.setCutoffFrequency(highHz);
        }
        const auto md = midAmount.getNextValue(), hd = highAmount.getNextValue();
        const auto lg = lowGain.getNextValue(), mg = midGain.getNextValue(), hg = highGain.getNextValue();
        const auto compression = compAmount.getNextValue();
        const auto threshold = -12.0f - 24.0f * compression;
        const auto ratio = s.parallel ? 4.f : 1.0f + 7.0f * compression;
        const auto mono = monoBlend.getNextValue();
        std::array<float, 2> lows {}, mids {}, highs {};
        for (int ch = 0; ch < channels; ++ch) {
            float low = 0, upper = 0, mid = 0, high = 0;
            const auto input = up.getSample(ch, (int)i);
            lowSplit.processSample(ch, input, low, upper);
            highSplit.processSample(ch, s.parallel ? input : upper, mid, high);
            const auto fixed = fixedMid.tick(input, ch);
            if (s.parallel) mid = fixed;
            else low = lowPhase.processSample(ch, low);
            lows[(size_t)ch] = low; mids[(size_t)ch] = mid; highs[(size_t)ch] = high;
            up.setSample(ch + 4, (int)i, low + mid + high);
        }
        const auto monoValue = channels == 2 ? (lows[0] + lows[1]) * .5f : lows[0];
        for (int ch = 0; ch < channels; ++ch) {
            const auto low = lows[(size_t)ch] + mono * (monoValue - lows[(size_t)ch]);
            const auto mid = mids[(size_t)ch], high = highs[(size_t)ch];
            auto& env = compEnvelope[(size_t) ch];
            const auto detector = std::abs(low);
            const auto c = detector > env ? compAttack : compRelease;
            env = detector + c * (env - detector);
            const auto above = juce::jmax(0.0f, juce::Decibels::gainToDecibels(env, -100.0f) - threshold);
            const auto reduction = above * (1.0f - 1.0f / ratio) * (s.parallel ? juce::jmin(1.f, compression * 20.f) : 1.f);
            gainReduction = juce::jmax(reduction, gainReduction * .9999f);
            const auto gainDb = -reduction + compression * 8.0f;
            up.setSample(ch, (int) i, low * juce::Decibels::decibelsToGain(gainDb) * lg);
            up.setSample(ch + 2, (int) i,
                distort(mid, md, .15f) * mg + distort(high, hd, 0) * hg);
        }
    }
    oversampling.processSamplesDown(block);
    // Keep the unfiltered upper bands for smooth cabinet bypass.
    for (int ch = 0; ch < channels; ++ch)
        buffer.copyFrom(ch, 0, bands, ch + 2, 0, count);
    auto upperBlock = block.getSubsetChannelBlock(2, 2);
    juce::dsp::ProcessContextReplacing<float> cabinetContext(upperBlock);
    for (int ch = 0; ch < 2; ++ch) cabB.copyFrom(ch, 0, bands, ch + 2, 0, count);
    auto second = juce::dsp::AudioBlock<float>(cabB).getSubBlock(0, (size_t)count);
    juce::dsp::ProcessContextReplacing<float> secondContext(second);
    cabinet[0].process(cabinetContext); cabinet[1].process(secondContext);
    for (int i = 0; i < count; ++i) {
        const auto cm = cabMix.getNextValue(), em = eqMix.getNextValue();
        const float gainA = cabGains[0].getNextValue(), gainB = cabGains[1].getNextValue();
        const float panA = cabPans[0].getNextValue(), panB = cabPans[1].getNextValue();
        const auto mix = wetMix.getNextValue(), out = outputGain.getNextValue(), active = bypassBlend.getNextValue();
        for (int ch = 0; ch < channels; ++ch) {
            const auto upper = buffer.getSample(ch, i);
            const auto panGain = [ch, channels](float pan) { return channels == 1 ? 1.f : (ch == 0 ? juce::jmin(1.f, 1.f-pan) : juce::jmin(1.f, 1.f+pan)); };
            const auto cabinetSum = bands.getSample(ch + 2, i) * gainA * panGain(panA) + cabB.getSample(ch, i) * gainB * panGain(panB);
            auto wet = bands.getSample(ch, i) + upper + cm * (cabinetSum - upper);
            auto filtered = wet;
            for (auto& eq : equalizer) filtered = eq.tick(filtered, ch);
            wet += em * (filtered - wet);
            const auto original = dry.getSample(ch, i);
            const auto clean = bands.getSample(ch + 4, i);
            const auto mixed = (clean + mix * (wet - clean)) * out;
            buffer.setSample(ch, i, original + active * (mixed - original));
        }
    }
}
}
