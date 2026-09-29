#include "Processor.h"
#include "InstrumentPlayer.h"

// Device access is opt-in. Opening the editor must not depend on a working
// default CoreAudio device or a combined built-in input/output device.
class StandaloneContent final : public juce::Component,
                                private juce::AudioIODeviceCallback,
                                private juce::ChangeListener {
public:
    StandaloneContent() {
        processor.standalone = true;
        editor.reset(processor.createEditor());
        addAndMakeVisible(*editor);
        addAndMakeVisible(settings); addAndMakeVisible(monitor); addAndMakeVisible(deviceStatus);
        addAndMakeVisible(inputMode);
        inputMode.addItem("Mono",1);
        inputMode.addItem("Stereo",2);
        inputMode.setSelectedId(1,juce::dontSendNotification);
        inputMode.setTitle("Instrument input mode");
        inputMode.setTooltip("Mono sends the input chosen in Audio settings to both ears. Stereo keeps two inputs separate.");
        inputMode.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff20292a));
        inputMode.setColour(juce::ComboBox::textColourId,juce::Colour(0xffeeeae2));
        inputMode.onChange = [this] {
            changeInputMode();
        };
        monitor.setButtonText("Monitor input");
        monitor.onClick = [this] { player.setMuted(!monitor.getToggleState()); };
        settings.setButtonText("Audio settings...");
        settings.onClick = [this] { openSettings(); };
        deviceStatus.setText("Audio disconnected - choose your instrument interface in Audio settings", juce::dontSendNotification);
        deviceStatus.setColour(juce::Label::textColourId,juce::Colour(0xffa5b6b2));
        devices.addChangeListener(this);
        player.setProcessor(&processor);
        devices.addMidiInputDeviceCallback({}, &player.midiCollector());
        setSize(1008,858);
    }
    ~StandaloneContent() override {
        if (deviceDialog != nullptr) delete deviceDialog.getComponent();
        devices.removeMidiInputDeviceCallback({}, &player.midiCollector());
        devices.removeChangeListener(this);
        devices.removeAudioCallback(this);
        devices.closeAudioDevice();
        player.setProcessor(nullptr);
        editor.reset();
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff20292a)); }
    void resized() override {
        const auto scale = (float)getWidth()/1120.f;
        deviceStatus.setBounds(18,8,530,32);
        inputMode.setBounds(555,8,205,32);
        monitor.setBounds(778,8,144,32);
        settings.setBounds(938,8,164,32);
        for (auto* c : std::initializer_list<juce::Component*> {&deviceStatus,&inputMode,&monitor,&settings}) c->setTransform(juce::AffineTransform::scale(scale,1.f));
        editor->setBounds(0,48,getWidth(),getHeight()-48);
    }
private:
    void changeInputMode() {
        const bool requestedStereo = inputMode.getSelectedId() == 2;
        if (requestedStereo == stereoInput) return;
        if (auto* device = devices.getCurrentAudioDevice()) {
            auto setup = devices.getAudioDeviceSetup();
            const int available = device->getInputChannelNames().size();
            if (available > 0) {
                int first = setup.inputChannels.findNextSetBit(0);
                if (first < 0 || first >= available) first = 0;
                setup.inputChannels.clear();
                setup.inputChannels.setBit(first);
                if (requestedStereo && available > 1)
                    setup.inputChannels.setBit(first + 1 < available ? first + 1 : first - 1);
                setup.useDefaultInputChannels = false;
                const auto error = devices.setAudioDeviceSetup(setup, true);
                if (error.isNotEmpty()) {
                    inputMode.setSelectedId(stereoInput ? 2 : 1, juce::dontSendNotification);
                    deviceStatus.setText(error, juce::dontSendNotification);
                    return;
                }
            }
        }
        stereoInput = requestedStereo;
        player.setMode(stereoInput ? InstrumentPlayer::Mode::stereo : InstrumentPlayer::Mode::mono);
        // Recreate the selector with the appropriate physical-input limit next time.
        if (deviceDialog != nullptr) delete deviceDialog.getComponent();
    }
    void openSettings() {
        if (deviceDialog != nullptr) { deviceDialog->toFront(true); return; }
        if (!devicesInitialised) {
            // Empty device names are intentional: enumerate without opening hardware.
            juce::XmlElement disconnected("DEVICESETUP");
            const auto error=devices.initialise(stereoInput ? 2 : 1,2,&disconnected,false);
            if (error.isNotEmpty()) { deviceStatus.setText(error,juce::dontSendNotification); return; }
            devices.addAudioCallback(this);
            devicesInitialised=true;
        }
        auto selector=std::make_unique<juce::AudioDeviceSelectorComponent>(devices,1,stereoInput ? 2 : 1,2,2,true,false,false,false);
        selector->setSize(560,440);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector.release());
        options.dialogTitle="LOWW Audio Settings";
        options.dialogBackgroundColour=juce::Colour(0xff20292a);
        options.escapeKeyTriggersCloseButton=true;
        options.useNativeTitleBar=true;
        options.resizable=false;
        options.componentToCentreAround=this;
        deviceDialog=options.launchAsync();
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        if (auto* device=devices.getCurrentAudioDevice())
            deviceStatus.setText(device->getName()+" / "+juce::String((int)device->getCurrentSampleRate())
                +" Hz / "+juce::String(device->getCurrentBufferSizeSamples())+" samples",juce::dontSendNotification);
        else deviceStatus.setText("Audio disconnected - choose your instrument interface in Audio settings",juce::dontSendNotification);
    }
    void audioDeviceIOCallbackWithContext(const float* const* input,int numInputs,
        float* const* output,int numOutputs,int count,const juce::AudioIODeviceCallbackContext& context) override {
        player.audioDeviceIOCallbackWithContext(input,numInputs,
            output,numOutputs,count,context);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override { player.audioDeviceAboutToStart(device); }
    void audioDeviceStopped() override { player.audioDeviceStopped(); }
    LOWWProcessor processor;
    juce::AudioDeviceManager devices;
    InstrumentPlayer player;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::TextButton settings;
    juce::ComboBox inputMode;
    juce::ToggleButton monitor;
    juce::Label deviceStatus;
    juce::Component::SafePointer<juce::DialogWindow> deviceDialog;
    bool devicesInitialised=false;
    bool stereoInput=false;
};

class LOWWApp final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "LOWW"; }
    const juce::String getApplicationVersion() override { return "0.2.1"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { window=std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override { if(window) window->toFront(true); }
private:
    class Window final : public juce::DocumentWindow {
    public:
        Window() : DocumentWindow("LOWW",juce::Colour(0xff111516),DocumentWindow::closeButton|DocumentWindow::minimiseButton) {
            setUsingNativeTitleBar(true);
            setContentOwned(new StandaloneContent(),true);
            setResizable(true,false); setResizeLimits(840,723,1680,1398);
            centreWithSize(getWidth(),getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
JUCE_CREATE_APPLICATION_DEFINE(LOWWApp)
