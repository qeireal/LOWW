#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// Shared by the standalone callback and its device-level regression test.
class InstrumentPlayer final : public juce::AudioIODeviceCallback {
public:
    enum class Mode { mono, stereo };
    void setProcessor(juce::AudioProcessor* processor) { player.setProcessor(processor); }
    juce::MidiMessageCollector& midiCollector() { return player.getMidiMessageCollector(); }
    void setMuted(bool value) noexcept { muted.store(value); }
    void setMode(Mode value) noexcept { mode.store(value); }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override { player.audioDeviceAboutToStart(device); }
    void audioDeviceStopped() override { player.audioDeviceStopped(); }
    void audioDeviceIOCallbackWithContext(const float* const* input, int numInputs,
        float* const* output, int numOutputs, int count, const juce::AudioIODeviceCallbackContext& context) override {
        const auto isMuted = muted.load();
        const auto selectedMode = mode.load();
        if (isMuted || input == nullptr || numInputs == 0) {
            player.audioDeviceIOCallbackWithContext(nullptr, 0, output, numOutputs, count, context);
        } else if (selectedMode == Mode::stereo) {
            player.audioDeviceIOCallbackWithContext(input, numInputs, output, numOutputs, count, context);
        } else {
            // Device settings select the physical channel; JUCE packs active inputs.
            const auto* mono = input[0];
            // Duplicate the selected instrument BEFORE DSP, preserving equal gain
            // and phase in both ears without mixing noise from an unused input.
            const float* channels[] {mono, mono};
            player.audioDeviceIOCallbackWithContext(mono != nullptr ? channels : nullptr,
                mono != nullptr ? 2 : 0, output, numOutputs, count, context);
        }
    }
private:
    juce::AudioProcessorPlayer player;
    std::atomic<bool> muted {true};
    std::atomic<Mode> mode {Mode::mono};
};
