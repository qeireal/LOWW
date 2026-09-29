#pragma once
#include "InstrumentPlayer.h"

class FixtureDevice final : public juce::AudioIODevice {
public:
    FixtureDevice(bool mono = false, int channel = 0) : AudioIODevice("Interface fixture", "Test"), monoInput(mono), physicalChannel(channel) {}
    juce::StringArray getOutputChannelNames() override { return {"Left", "Right"}; }
    juce::StringArray getInputChannelNames() override { return {"Input 1", "Input 2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {48000}; }
    juce::Array<int> getAvailableBufferSizes() override { return {128}; }
    int getDefaultBufferSize() override { return 128; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 128; }
    double getCurrentSampleRate() override { return 48000; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return juce::BigInteger(3); }
    juce::BigInteger getActiveInputChannels() const override { juce::BigInteger active; active.setBit(physicalChannel); return monoInput ? active : juce::BigInteger(3); }
    bool monoInput;
    int physicalChannel;
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

inline void testMonitoring() {
    const auto run = [](InstrumentPlayer::Mode mode, int guitarChannel, bool muted) {
        LOWWProcessor processor;
        const bool mono = mode == InstrumentPlayer::Mode::mono;
        FixtureDevice device(mono, guitarChannel);
        InstrumentPlayer player;
        player.setProcessor(&processor);
        player.setMode(mode);
        player.setMuted(muted);
        player.audioDeviceAboutToStart(&device);
        juce::AudioBuffer<float> input(mono ? 1 : 2,128), output(2,128);
        std::array<double,2> energy {};
        double difference = 0;
        for (int block = 0; block < 200; ++block) {
            input.clear(); output.clear();
            for (int i = 0; i < 128; ++i)
                input.setSample(mono ? 0 : guitarChannel,i,.1f * (float) std::sin(2*juce::MathConstants<double>::pi*110*(block*128+i)/48000));
            player.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(),input.getNumChannels(),
                output.getArrayOfWritePointers(),2,128,{});
            if (block > 100) for (int i = 0; i < 128; ++i) {
                for (int ch = 0; ch < 2; ++ch) {
                    const auto sample = output.getSample(ch,i);
                    if (!std::isfinite(sample)) throw std::runtime_error("Non-finite monitoring sample");
                    energy[(size_t) ch] += sample * sample;
                }
                difference = std::max(difference,(double)std::abs(output.getSample(0,i)-output.getSample(1,i)));
            }
        }
        player.audioDeviceStopped(); player.setProcessor(nullptr);
        std::cout << "Monitor mode=" << (int)mode << " input=" << guitarChannel+1
                  << " muted=" << muted << " left=" << energy[0] << " right=" << energy[1] << '\n';
        if (muted) {
            if (energy[0]+energy[1] > 1e-9) throw std::runtime_error("Muted monitor produces sound");
        } else if (mode != InstrumentPlayer::Mode::stereo) {
            if (energy[0] < .01 || energy[1] < .01 || difference > 1e-6)
                throw std::runtime_error("Mono guitar must produce equal, non-silent output in both ears");
        } else if (energy[(size_t)guitarChannel] < .01 || energy[(size_t)(1-guitarChannel)] > 1e-9)
            throw std::runtime_error("Stereo monitoring changed channel isolation");
    };
    run(InstrumentPlayer::Mode::mono,0,false);
    run(InstrumentPlayer::Mode::mono,1,false);
    run(InstrumentPlayer::Mode::stereo,0,false);
    run(InstrumentPlayer::Mode::stereo,1,false);
    run(InstrumentPlayer::Mode::mono,0,true);
    std::cout << "PASS live monitoring callback: selected physical mono input 1/2, true stereo, mute\n";
}
