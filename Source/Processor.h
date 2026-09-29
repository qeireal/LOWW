#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"

class LOWWProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener {
public:
    LOWWProcessor();
    ~LOWWProcessor() override;
    const juce::String getName() const override { return "LOWW"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::Result restoreState(const void*, int);
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState state;
    std::atomic<float> inputMeter {0}, outputMeter {0}, compressionMeter {0};
    std::atomic<bool> presetDirty {false}, metronomeOn {false};
    std::atomic<float> tempo {120};
    std::atomic<int> beatsPerBar {4};
    bool standalone = false;
    std::atomic<int> midiLearn {-1};
    std::array<std::atomic<int>, 128> midiMap;
    void learnMidi(const juce::String& parameter);
    void clearMidi(const juce::String& parameter);
    bool readTuner(float* destination, int count);
    juce::String presetName() const;
    juce::Result savePreset(const juce::File&);
    juce::Result openPreset(const juce::File&);
    static juce::File presetFolder();
    std::atomic<double> currentRate {48000};
    bool readSpectrum(float* destination, int count);
    juce::Result loadIRFile(const juce::File&, int slot = 0);
    juce::Result restoreStockCab(int slot = 0);
    juce::String cabinetName(int slot = 0);
    void applyFactoryPreset(int index);
    static juce::StringArray presetNames();
    static juce::String presetDescription(int index);
    loww::Settings settings() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    void render(juce::AudioBuffer<float>&, bool hostBypass);
    float value(const char*) const;
    juce::Result queueIR(juce::AudioBuffer<float>&&, double, int slot);
    void parameterChanged(const juce::String&, float) override { presetDirty.store(true); }
    void consumeIR();
    void reclaimIR();
    struct IRPacket { juce::AudioBuffer<float> audio; double rate = 48000; IRPacket* next = nullptr; };
    // Latest-wins mailbox. The audio thread moves the buffer to JUCE's worker;
    // retired packet storage is reclaimed only by control threads or destruction.
    std::array<std::atomic<IRPacket*>, 2> pendingIR {};
    std::atomic<IRPacket*> retiredIR {nullptr};
    juce::CriticalSection irProducerLock;
    juce::AbstractFifo spectrumQueue {8192};
    std::array<float, 8192> spectrumData {};
    juce::AbstractFifo tunerQueue {32768};
    std::array<float, 32768> tunerData {};
    double beatPhase = 0, clickPhase = 0;
    int beat = 0;
    float clickEnvelope = 0;
    bool wasMetronomeOn = false;
    loww::Engine engine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LOWWProcessor)
};
