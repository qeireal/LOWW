#pragma once
#include "Processor.h"

class LOWWEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit LOWWEditor(LOWWProcessor&);
    ~LOWWEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Theme final : juce::LookAndFeel_V4 {
        Theme();
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
        void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                              juce::Slider::SliderStyle, juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
        void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
        void drawPopupMenuBackground(juce::Graphics&, int, int) override;
        juce::Font getTextButtonFont(juce::TextButton&, int) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Label* createSliderTextBox(juce::Slider&) override;
    } theme;
    struct Dial final : juce::Component {
        Dial(LOWWProcessor&, const char* id, const juce::String& title, juce::Colour, bool vertical = false);
        void resized() override;
        struct MidiSlider : juce::Slider {
            std::function<void()> menu;
            void mouseDown(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (menu) menu(); } else juce::Slider::mouseDown(e); }
            // Return opens value entry, so keyboard users can type a value as mouse users can.
            bool keyPressed(const juce::KeyPress& key) override {
                if (key == juce::KeyPress::returnKey) { showTextBox(); return true; }
                return juce::Slider::keyPressed(key);
            }
        };
        juce::Label label;
        MidiSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void timerCallback() override;
    void chooseIR(int slot);
    void refreshPresets();
    void stepPreset(int direction);
    void updateTuner();
    void presetFile(bool saving);
    void savePresetAs(const juce::File&);
    void forgetFileAfterHostRestore();
    void showResult(const juce::Result&);
    enum class Tone { hint, ok, active, error };
    void setStatus(const juce::String&, Tone = Tone::ok);
    void setStatus(const juce::Result&, const juce::String& done);
    void setTuner(const juce::String&, juce::Colour);
    void drawGraph(juce::Graphics&, juce::Rectangle<float>);
    void drawMeter(juce::Graphics&, juce::Rectangle<float>, float, const juce::String&);
    LOWWProcessor& owner;
    std::vector<std::unique_ptr<Dial>> dials;
    std::vector<std::unique_ptr<juce::ToggleButton>> switches;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> switchAttachments;
    juce::ComboBox presets;
    juce::TextButton save {"Save as..."}, load {"Open"}, previous {"<"}, next {">"}, overwrite {"Save"}, folder {"Folder"};
    std::array<juce::TextButton,2> loadCab, stockCab;
    juce::ToggleButton analyser {"Spectrum"}, tuner {"Live tuner"}, metronome {"Click"};
    Dial::MidiSlider bpm;
    juce::ComboBox timeSignature;
    juce::Label tunerLabel, presetInfo;
    juce::Array<juce::File> userPresets;
    juce::File currentFile;
    int currentFactory = -1;
    int seenHostRestores = 0;
    int draggedBand = -1, learningParameter = -1;
    juce::Point<float> grabOffset;
    juce::String draggedFrequency, draggedLevel;
    float uiScale = 1;
    juce::Point<float> uiOffset;
    double lastTap = 0;
    juce::TextButton tap {"Tap"};
    std::array<float,16384> tunerSamples {};
    int tunerTicks = 0;
    juce::TooltipWindow tooltips {this, 650};
    // JUCE's nested parts (slider value boxes, inc/dec buttons) still take focus on click. Any mouse press
    // outside an open text editor parks focus on the editor itself, which draws no ring, so rings only mark
    // keyboard navigation and Tab can still resume from the editor.
    struct ClickUnfocus final : juce::MouseListener {
        explicit ClickUnfocus(juce::Component& e) : editor(e) {}
        void mouseDown(const juce::MouseEvent&) override {
            if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) == nullptr)
                editor.grabKeyboardFocus();
        }
        juce::Component& editor;
    } clickUnfocus {*this};
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::dsp::FFT fft {11};
    juce::dsp::WindowingFunction<float> window {2048, juce::dsp::WindowingFunction<float>::hann};
    std::array<float, 4096> fftData {};
    std::array<float, 1024> spectrum {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LOWWEditor)
};
