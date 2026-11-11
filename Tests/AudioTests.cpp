#include "Processor.h"
#include "Tuner.h"
#include "Logo.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include "MonitoringTests.h"
#if JUCE_MAC
#include <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#endif

// Counts operator new calls made while a test marks the current thread as real-time.
// JUCE's HeapBlock calls malloc directly, so this guards C++ allocations, not every heap use.
static thread_local bool countAllocations = false;
static std::atomic<int> realtimeAllocations {0};
void* operator new(std::size_t size) {
    if (countAllocations) ++realtimeAllocations;
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
loww::Settings neutral() {
    loww::Settings s;
    s.output = 0; s.compression = s.midDrive = s.highDrive = 0;
    s.lowLevel = s.midLevel = s.highLevel = 0;
    s.cabOn = false;
    return s;
}
std::vector<float> render(loww::Settings s, double frequency, float amplitude,
                           double rate = 48000, int block = 256) {
    loww::Engine engine;
    engine.useStockCab(); engine.prepare(rate, block);
    std::vector<float> result;
    const auto total = (int) rate;
    for (int offset = 0; offset < total; offset += block) {
        const auto n = juce::jmin(block, total-offset);
        juce::AudioBuffer<float> audio(2, n);
        for (int i = 0; i < n; ++i) {
            const auto sample = amplitude * (float) std::sin(juce::MathConstants<double>::twoPi * frequency * (offset+i) / rate);
            audio.setSample(0, i, sample); audio.setSample(1, i, sample);
        }
        engine.process(audio, s);
        if (offset > total / 2) for (int i = 0; i < n; ++i) result.push_back(audio.getSample(0, i));
    }
    return result;
}
double rms(const std::vector<float>& samples) {
    double sum = 0;
    for (auto sample : samples) { require(std::isfinite(sample), "Non-finite output"); sum += sample * sample; }
    return std::sqrt(sum / (double) samples.size());
}
double harmonic(const std::vector<float>& samples, double hz) {
    double a = 0, b = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto phase = juce::MathConstants<double>::twoPi * hz * (double) i / 48000;
        a += samples[i] * std::cos(phase); b += samples[i] * std::sin(phase);
    }
    return 2 * std::sqrt(a*a+b*b) / (double) samples.size();
}
void testNeutralResponse() {
    for (const double sr : {44100, 48000, 96000})
        for (const double hz : {40, 100, 200, 440, 800, 2000, 6000}) {
            const auto db = 20 * std::log10(rms(render(neutral(), hz, .1f, sr)) / (.1/std::sqrt(2.0)));
            require(std::abs(db) < .25, "Neutral crossover sum is not flat within 0.25 dB");
        }
    std::cout << "PASS crossover reconstruction: 21 rate/frequency combinations\n";
}
void testBypassAndChannels() {
    loww::Engine engine;
    engine.prepare(48000, 64);
    auto s = neutral(); s.bypass = true;
    juce::AudioBuffer<float> silence(2, 2048); silence.clear(); engine.process(silence, s);
    juce::AudioBuffer<float> impulse(2, 1024); impulse.clear(); impulse.setSample(0, 0, .5f);
    engine.process(impulse, s);
    for (int i = 0; i < 1024; ++i) {
        require(std::abs(impulse.getSample(0,i) - (i == engine.latency() ? .5f : 0)) < 1e-6, "Bypass latency mismatch");
        require(std::abs(impulse.getSample(1,i)) < 1e-9f, "Stereo crosstalk");
    }
    for (const float mix : {0.0f, .5f, 1.0f}) {
        auto blend = neutral(); blend.mix = mix;
        const auto level = rms(render(blend,200,.1f));
        require(std::abs(level / (.1/std::sqrt(2.0)) - 1) < .02, "Dry/wet blend has crossover cancellation");
    }
    std::cout << "PASS latency-compensated bypass, phase-aligned dry mix, stereo isolation, oversized blocks\n";
}
void testTone() {
    auto clean = neutral();
    auto dirty = clean; dirty.highDrive = 1; dirty.midDrive = 1;
    const auto a = render(clean, 1500, .3f), b = render(dirty, 1500, .3f);
    require(harmonic(b, 4500) > harmonic(a, 4500) * 5, "Distortion adds no third harmonic");
    auto compressed = clean; compressed.compression = 1;
    const auto small = rms(render(compressed, 50, .01f)) / .01;
    const auto large = rms(render(compressed, 50, .5f)) / .5;
    require(large < small * .5, "Compressor does not reduce loud-signal gain");
    auto boosted = clean; boosted.eq[3] = 6;
    const auto eqDb = 20 * std::log10(rms(render(boosted,1000,.1f))/rms(render(clean,1000,.1f)));
    require(std::abs(eqDb - 6) < .15, "EQ centre gain mismatch");
    std::cout << "PASS distortion harmonics, compressor dynamics, 6 dB EQ response\n";
}
void testCabAndGate() {
    auto dry = neutral(); dry.lowLevel = -120;
    auto cab = dry; cab.cabOn = true;
    const auto attenuation = rms(render(cab,10000,.1f)) / rms(render(dry,10000,.1f));
    require(attenuation < .15, "Cabinet fails to attenuate high frequencies");
    auto low = neutral(); low.midLevel = low.highLevel = -120;
    const auto without = rms(render(low,60,.1f)); low.cabOn = true;
    require(std::abs(rms(render(low,60,.1f))/without - 1) < .001, "Cabinet alters low branch");
    auto gate = neutral(); gate.gateOn = true; gate.gate = -40;
    require(rms(render(gate,440,.0001f)) < .000001, "Gate fails to close");
    std::cout << "PASS cabinet filtering, direct low branch, gate closure\n";
}
void testStateAndStress() {
    LOWWProcessor p;
    p.applyFactoryPreset(3);
    juce::MemoryBlock state; p.getStateInformation(state);
    LOWWProcessor restored; restored.setStateInformation(state.getData(), (int) state.getSize());
    require(std::abs(restored.settings().midDrive - .85f) < .001, "Preset drive was not restored");
    require(std::abs(restored.settings().eq[3] - 3) < .001, "Preset EQ was not restored");
    const char garbage[] = "invalid";
    restored.setStateInformation(garbage, sizeof(garbage));
    require(std::abs(restored.settings().midDrive - .85f) < .001, "Invalid state changed parameters");
    juce::Random random(42);
    for (const double sr : {44100, 48000, 96000}) {
        restored.prepareToPlay(sr, 128);
        juce::MidiBuffer midi;
        for (int iteration = 0; iteration < 120; ++iteration) {
            const int n = iteration % 3 == 0 ? 513 : iteration % 3 == 1 ? 1 : 128;
            juce::AudioBuffer<float> buffer(2,n);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i)
                buffer.setSample(ch,i, iteration % 4 == 0 ? 0 : (random.nextFloat()-.5f) * .5f);
            restored.processBlock(buffer,midi);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i)
                require(std::isfinite(buffer.getSample(ch,i)) && std::abs(buffer.getSample(ch,i)) < 8,
                        "Stress test produced unstable samples");
        }
    }
    juce::AudioBuffer<float> empty(2,0); juce::MidiBuffer midi; restored.processBlock(empty,midi);
    std::cout << "PASS preset recall, malformed state, silence, variable blocks, sample-rate changes\n";
}
void testEmbeddedIR() {
    LOWWProcessor source;
    auto tree = source.state.copyState();
    juce::AudioBuffer<float> impulse(1,128); impulse.clear(); impulse.setSample(0,0,1);
    juce::MemoryBlock ir(impulse.getReadPointer(0),128*sizeof(float));
    tree.setProperty("irData",ir.toBase64Encoding(),nullptr);
    tree.setProperty("irChannels",1,nullptr); tree.setProperty("irSamples",128,nullptr);
    tree.setProperty("irRate",48000,nullptr); tree.setProperty("cabinet","Embedded test IR",nullptr);
    juce::MemoryBlock saved; juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),saved);
    LOWWProcessor restored;
    // Repeated state changes while transport is stopped must always keep the latest.
    for (int i=0;i<12;++i)
        require(restored.restoreState(saved.getData(),(int)saved.getSize()).wasOk(),"Repeated IR state restore failed");
    restored.prepareToPlay(48000,128);
    juce::AudioBuffer<float> audio(2,128); audio.clear(); juce::MidiBuffer midi;
    restored.processBlock(audio,midi);
    require(restored.cabinetName()=="Embedded test IR","Embedded IR metadata lost");
    juce::MemoryBlock recalled; restored.getStateInformation(recalled);
    auto xml=juce::AudioProcessor::getXmlFromBinary(recalled.getData(),(int)recalled.getSize());
    require(xml && xml->getStringAttribute("irData")==ir.toBase64Encoding(),"IR samples not preserved in state");
    tree.setProperty("irSamples",9999999,nullptr);
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),saved);
    require(restored.restoreState(saved.getData(),(int)saved.getSize()).failed(),"Malformed IR accepted");
    require(restored.cabinetName()=="Embedded test IR","Malformed IR changed existing state");
    std::cout << "PASS embedded IR recall, repeated stopped-transport loads, invalid IR rejection\n";
}
void setParameter(LOWWProcessor& p, const char* id, float value) {
    auto* parameter = p.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void testNewVoicing() {
    auto base = neutral(); base.parallel = true;
    auto shelf = base; shelf.eq[0] = 6;
    const auto lowGain = 20 * std::log10(rms(render(shelf,30,.1f))/rms(render(base,30,.1f)));
    const auto midGain = 20 * std::log10(rms(render(shelf,1000,.1f))/rms(render(base,1000,.1f)));
    require(lowGain > 5.7 && std::abs(midGain) < .15, "Low shelf does not separate lows and mids");
    shelf = base; shelf.eq[5] = -6;
    require(20*std::log10(rms(render(shelf,15000,.1f))/rms(render(base,15000,.1f))) < -5.7, "High shelf is not a shelf");
    auto mid = base; mid.lowLevel = mid.highLevel = -120;
    const auto centre = rms(render(mid,400,.1f));
    mid.lowCut = 70; mid.highCut = 2000;
    require(std::abs(rms(render(mid,400,.1f))/centre-1) < .001, "Fixed mid band moves with outer filters");
    auto compression=base; compression.compression=1; compression.midLevel=compression.highLevel=-120;
    const auto slope=20*std::log10(rms(render(compression,50,.4f))/rms(render(compression,50,.2f)));
    require(std::abs(slope-1.505)<.1,"Parallel compressor ratio is not 4:1");
    loww::Engine engine; engine.prepare(48000,256);
    auto mono = neutral(); mono.monoLow = true; mono.midLevel = mono.highLevel = -120;
    double left = 0, right = 0;
    for (int block = 0; block < 150; ++block) {
        juce::AudioBuffer<float> audio(2,256); audio.clear();
        for (int i=0;i<256;++i) audio.setSample(0,i,.1f*(float)std::sin(2*juce::MathConstants<double>::pi*50*(block*256+i)/48000));
        engine.process(audio,mono);
        if (block > 100) for(int i=0;i<256;++i) { left += std::pow(audio.getSample(0,i),2); right += std::pow(audio.getSample(1,i),2); }
    }
    require(left > .01 && std::abs(left-right) < left*.001, "Mono low band is not centred");
    std::cout << "PASS parallel shelf EQ, fixed mid band, mono lows\n";
}
void testDualCab() {
    auto a = neutral(); a.cabOn = true; a.lowLevel = -120;
    const auto signal = rms(render(a,1500,.1f));
    // Test helpers load the stock IR only in A; explicitly load both here.
    loww::Engine engine; engine.useStockCab(0); engine.useStockCab(1); engine.prepare(48000,256);
    auto both = a; both.cabEnabled[1] = true; both.cabLevel[1] = 0; both.cabPhase[1] = true;
    double energy=0;
    for(int block=0;block<180;++block) {
        juce::AudioBuffer<float> audio(2,256);
        for(int ch=0;ch<2;++ch) for(int i=0;i<256;++i) audio.setSample(ch,i,.1f*(float)std::sin(2*juce::MathConstants<double>::pi*1500*(block*256+i)/48000));
        engine.process(audio,both);
        if(block>120) for(int i=0;i<256;++i) energy+=audio.getSample(0,i)*audio.getSample(0,i);
    }
    require(signal > .001 && std::sqrt(energy/(59*256)) < signal*.002, "Dual IR phase inversion does not cancel equal IRs");
    LOWWProcessor p; setParameter(p,"cab1Enabled",1); setParameter(p,"cab1Pan",45); setParameter(p,"cabOn",0);
    p.applyFactoryPreset(4);
    require(p.settings().cabEnabled[1] && std::abs(p.settings().cabPan[1]-.45f)<.001 && !p.settings().cabOn,"Factory preset changed cabinet mix");
    auto tree=p.state.copyState();
    const float impulse[] {1,0,0,0}; juce::MemoryBlock samples(impulse,sizeof(impulse));
    tree.setProperty("irDataB",samples.toBase64Encoding(),nullptr); tree.setProperty("irChannelsB",1,nullptr); tree.setProperty("irSamplesB",4,nullptr); tree.setProperty("irRateB",48000,nullptr); tree.setProperty("cabinetB","Second slot",nullptr);
    juce::MemoryBlock state; juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),state);
    require(p.restoreState(state.getData(),(int)state.getSize()).wasOk(),"Second IR restore failed");
    juce::MemoryBlock roundtrip; p.getStateInformation(roundtrip); LOWWProcessor restored;
    require(restored.restoreState(roundtrip.getData(),(int)roundtrip.getSize()).wasOk() && restored.cabinetName(1)=="Second slot","Second IR lost in roundtrip");
    tree.setProperty("irRateB",0,nullptr); juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),state);
    require(p.restoreState(state.getData(),(int)state.getSize()).failed() && p.cabinetName(1)=="Second slot","Invalid second IR changed state");
    std::cout << "PASS dual-cab polarity, portable second IR, factory cabinet preservation\n";
}
void testTransposeAndTuner() {
    for (const float semitones : {-12.f, 7.f, 12.f}) {
        auto settings=neutral(); settings.transpose=semitones;
        const auto audio=render(settings,110,.15f);
        const double expected=110*std::pow(2.0,semitones/12.0);
        require(harmonic(audio,expected) > .06,"Transposer fails target pitch");
    }
    for(const double sr : {44100.,48000.,96000.,192000.}) for(const double hz : {30.87,41.203,55.,110.,440.}) {
        const int count=juce::jlimit(4096,16384,(int)(sr*.09)); std::vector<float> audio((size_t)count);
        for(int i=0;i<count;++i) audio[(size_t)i]=.1f*(float)(std::sin(2*juce::MathConstants<double>::pi*hz*i/sr)+.4*std::sin(4*juce::MathConstants<double>::pi*hz*i/sr));
        const auto result=loww::detectPitch(audio.data(),count,sr);
        require(result.frequency>0 && std::abs(1200*std::log2(result.frequency/hz))<4,"Tuner pitch error exceeds four cents");
    }
    float silence[4096] {}; require(loww::detectPitch(silence,4096,48000).frequency<=0,"Tuner invents a note for silence");
    loww::Engine engine; engine.prepare(48000,128); auto settings=neutral(); settings.transpose=12; settings.bypass=true;
    juce::AudioBuffer<float> audio(2,10000); audio.clear(); engine.process(audio,settings);
    audio.clear(); audio.setSample(0,0,.5f); engine.process(audio,settings);
    for(int i=0;i<audio.getNumSamples();++i) require(std::abs(audio.getSample(0,i)-(i==engine.latency() ? .5f : 0))<1e-6,"Transposed bypass latency mismatch");
    LOWWProcessor host;
    setParameter(host,"transpose",12); host.prepareToPlay(48000,256);
    require(host.getLatencySamples()>1000,"Transposed preset does not announce latency before playback");
    setParameter(host,"transpose",0); host.prepareToPlay(48000,256);
    require(host.getLatencySamples()<1000,"Zero transpose adds pitch latency");
    std::cout << "PASS octave/semitone transpose, latency-aligned bypass, bass tuner at four sample rates\n";
}
void testPresetMigrationAndMidi() {
    LOWWProcessor p;
    auto legacy=p.state.copyState(); legacy.setProperty("schema",1,nullptr);
    for(const auto* id : {"parallel","monoLow","transpose","cab1Enabled"}) legacy.removeChild(legacy.getChildWithProperty("id",id),nullptr);
    juce::MemoryBlock data; juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),data);
    require(p.restoreState(data.getData(),(int)data.getSize()).wasOk() && !p.settings().parallel && !p.settings().monoLow && std::abs(p.settings().transpose)<.001f,"Legacy preset inherited new processing");
    p.applyFactoryPreset(4); require(!p.presetDirty.load() && p.presetName()=="Deep Edge","Preset identity missing");
    setParameter(p,"midDrive",25); require(p.presetDirty.load(),"Preset edits not marked");
    p.prepareToPlay(48000,256); p.learnMidi("midDrive");
    juce::AudioBuffer<float> audio(2,256); audio.clear(); juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1,21,127),0); p.processBlock(audio,midi);
    require(p.settings().midDrive>.999f && p.midiLearn.load()==-1,"MIDI learn did not apply CC");
    p.getStateInformation(data); LOWWProcessor restored;
    require(restored.restoreState(data.getData(),(int)data.getSize()).wasOk() && restored.midiMap[21].load()>=0,"MIDI host recall failed");
    p.clearMidi("midDrive"); p.getStateInformation(data); restored.restoreState(data.getData(),(int)data.getSize());
    require(restored.midiMap[21].load()==-1,"Cleared MIDI mapping returned");
    auto file=juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("loww-preset-test",".loww");
    require(p.savePreset(file).wasOk(),"Preset file save failed"); setParameter(p,"midDrive",5);
    require(p.openPreset(file).wasOk() && p.settings().midDrive>.999f && !p.presetDirty.load(),"Preset file recall failed"); file.deleteFile();
    for(int i=0;i<LOWWProcessor::presetNames().size();++i) {
        p.applyFactoryPreset(i); audio.clear(); midi.clear(); p.processBlock(audio,midi);
        for(int ch=0;ch<2;++ch) for(int n=0;n<256;++n) require(std::isfinite(audio.getSample(ch,n)),"Factory preset is unstable");
    }
    LOWWProcessor metronome; metronome.standalone=true; metronome.prepareToPlay(48000,256); metronome.metronomeOn.store(true);
    audio.clear(); metronome.processBlock(audio,midi); require(audio.getMagnitude(0,256)>.01,"Metronome is silent");
    std::cout << "PASS legacy migration, preset identity/file recall, MIDI learn/clear/recall, factory bank, metronome\n";
}
// Renders one second of a 110 Hz burst (0.1-0.4 s) and returns channel 0.
std::vector<float> renderBurst(const loww::Settings& s, int& latency) {
    loww::Engine engine; engine.useStockCab(); engine.prepare(48000, 256, s.transpose);
    std::vector<float> out;
    for (int offset = 0; offset < 48000; offset += 256) {
        juce::AudioBuffer<float> audio(2, 256);
        for (int i = 0; i < 256; ++i) {
            const auto n = offset + i;
            const auto v = n >= 4800 && n < 19200 ? .1f * (float) std::sin(juce::MathConstants<double>::twoPi * 110 * n / 48000) : 0.f;
            audio.setSample(0, i, v); audio.setSample(1, i, v);
        }
        engine.process(audio, s);
        for (int i = 0; i < 256; ++i) out.push_back(audio.getSample(0, i));
    }
    latency = engine.latency();
    return out;
}
void testGateTransposeMonoAndPriming() {
    // The gate must track the pitched signal it gates, not the earlier raw input.
    auto open = neutral(); open.transpose = 12;
    auto gated = open; gated.gateOn = true; gated.gate = -26;
    int latency = 0;
    const auto reference = renderBurst(open, latency), withGate = renderBurst(gated, latency);
    const auto tail = [&](const std::vector<float>& x) {
        return rms(std::vector<float>(x.begin() + 16320 + latency, x.begin() + 19200 + latency));
    };
    require(20 * std::log10(tail(withGate) / tail(reference)) > -1, "Gate cuts transposed note tails");
    // A bypassed engine must start bypassed after prepare, not fade in from processed audio.
    loww::Engine engine; engine.useStockCab(); engine.prepare(48000, 512);
    loww::Settings bypassed; bypassed.bypass = true; bypassed.midDrive = 1;
    juce::AudioBuffer<float> audio(2, 512), input(2, 512);
    for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) input.setSample(ch, i, .4f * (float) std::sin(.05 * i));
    audio.makeCopyOf(input); engine.process(audio, bypassed);
    for (int i = engine.latency(); i < 512; ++i)
        require(std::abs(audio.getSample(0, i) - input.getSample(0, i - engine.latency())) < 1e-5f, "Bypass fades in after prepare");
    // Mono hosts use the single-channel engine path.
    loww::Engine mono; mono.prepare(48000, 256);
    std::vector<float> monoOut;
    for (int offset = 0; offset < 48000; offset += 256) {
        juce::AudioBuffer<float> buffer(1, 256);
        for (int i = 0; i < 256; ++i) buffer.setSample(0, i, .1f * (float) std::sin(juce::MathConstants<double>::twoPi * 440 * (offset + i) / 48000));
        mono.process(buffer, neutral());
        if (offset > 24000) for (int i = 0; i < 256; ++i) monoOut.push_back(buffer.getSample(0, i));
    }
    require(std::abs(20 * std::log10(rms(monoOut) / (.1 / std::sqrt(2.0)))) < .25, "Mono engine path is not neutral");
    // Transpose crossing zero during playback updates the reported latency.
    LOWWProcessor p; p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> block(2, 256); juce::MidiBuffer midi;
    const auto base = p.getLatencySamples();
    for (const float semitones : {12.f, 0.f}) {
        setParameter(p, "transpose", semitones);
        for (int b = 0; b < 20; ++b) {
            for (int i = 0; i < 256; ++i) for (int ch = 0; ch < 2; ++ch) block.setSample(ch, i, .1f * (float) std::sin(.03 * (b * 256 + i)));
            p.processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) require(std::isfinite(block.getSample(ch, i)), "Transpose switch is unstable");
        }
        require((semitones != 0) == (p.getLatencySamples() > base), "Reported latency ignores live transpose");
    }
    std::cout << "PASS transposed gate timing, bypass after prepare, mono engine, live transpose latency\n";
}
void testHostRecallAndRealtime() {
    LOWWProcessor p; p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> audio(2,256); juce::MidiBuffer midi;
    auto block=[&](int b) {
        for(int ch=0;ch<2;++ch) for(int i=0;i<256;++i) audio.setSample(ch,i,.2f*(float)std::sin(juce::MathConstants<double>::twoPi*55*(b*256+i)/48000));
        p.processBlock(audio,midi);
    };
    for(int b=0;b<200;++b) block(b);
    countAllocations=true; for(int b=200;b<400;++b) block(b); countAllocations=false;
    require(realtimeAllocations.load()==0,"Steady-state processing called operator new on the audio thread");
    auto file=juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("loww-recall-test",".loww");
    require(p.savePreset(file).wasOk() && p.openPreset(file).wasOk() && p.hostRestoreCount.load()==0,"Opening a user preset counted as host recall");
    file.deleteFile();
    juce::MemoryBlock data; p.applyFactoryPreset(4); p.getStateInformation(data);
    p.setStateInformation(data.getData(),(int)data.getSize());
    require(p.hostRestoreCount.load()==1 && p.presetName()=="Deep Edge","Host recall did not invalidate the editor's save target");
    p.setStateInformation("junk",4); require(p.hostRestoreCount.load()==1,"Rejected host state counted as recall");
    // Host restores may arrive off the message thread while the editor reads preset metadata.
    std::atomic<bool> done {false};
    std::thread host([&] { for(int i=0;i<200;++i) p.setStateInformation(data.getData(),(int)data.getSize()); done=true; });
    bool consistent=true;
    while(!done.load()) consistent=consistent && p.presetName()=="Deep Edge" && p.presetInfo().isNotEmpty() && p.cabinetName().isNotEmpty();
    host.join(); require(consistent,"Metadata read raced host restore");
    const int restores=p.hostRestoreCount.load();
    auto stale=juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("loww-stale-test",".loww");
    require(p.savePreset(stale,restores-1).failed() && !stale.exists(),"Save overwrote a file after host recall");
    require(p.savePreset(stale,restores).wasOk(),"Save with a current host generation failed"); stale.deleteFile();
    std::cout << "PASS no operator new in steady-state processing, host recall tracking, concurrent metadata reads\n";
}
void writeWave(const juce::File& file,const juce::AudioBuffer<float>& buffer) {
    juce::WavAudioFormat format;
    auto stream=file.createOutputStream();
    require(stream != nullptr,"Couldn't create demo file");
    stream->setPosition(0); stream->truncate();
    std::unique_ptr<juce::OutputStream> output = std::move(stream);
    auto writer=format.createWriterFor(output,juce::AudioFormatWriterOptions{}
        .withSampleRate(48000).withNumChannels(buffer.getNumChannels()).withBitsPerSample(24));
    require(writer != nullptr && writer->writeFromAudioSampleBuffer(buffer,0,buffer.getNumSamples()),"Demo WAV write failed");
}
void renderDemo(const juce::File& folder) {
    require(folder.createDirectory().wasOk(),"Couldn't create demo folder");
    constexpr int sr=48000, length=sr*8;
    juce::AudioBuffer<float> dry(2,length), wet(2,length);
    const std::array<double,16> notes {41.203,41.203,82.407,61.735,41.203,55,61.735,73.416,
                                     41.203,82.407,73.416,61.735,55,55,61.735,41.203};
    for(int i=0;i<length;++i) {
        const int note=i/(sr/2); const double time=(double)(i%(sr/2))/sr;
        const auto envelope=(1-std::exp(-time*1500))*std::exp(-time*5);
        double sample=0;
        for(int partial=1;partial<=12;++partial)
            sample+=std::sin(2*juce::MathConstants<double>::pi*notes[(size_t)note]*partial*time)
                /std::pow((double)partial,1.3)*std::exp(-time*partial*2);
        for(int ch=0;ch<2;++ch) dry.setSample(ch,i,(float)(sample*envelope*.3));
    }
    wet.makeCopyOf(dry);
    loww::Engine engine; engine.useStockCab(); engine.prepare(sr,256);
    loww::Settings settings; settings.midDrive=.7f; settings.highDrive=.5f;
    for(int offset=0;offset<length;offset+=256) {
        juce::AudioBuffer<float> part(wet.getArrayOfWritePointers(),2,offset,juce::jmin(256,length-offset));
        engine.process(part,settings);
    }
    writeWave(folder.getChildFile("01-dry-synthetic-bass.wav"),dry);
    writeWave(folder.getChildFile("02-loww-processed.wav"),wet);
    std::cout<<"Rendered synthetic bass comparison to "<<folder.getFullPathName()<<'\n';
}
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        if(argc==3 && juce::String(argv[1])=="--export-presets") {
            juce::File folder(argv[2]); require(folder.createDirectory().wasOk(),"Cannot create preset output folder");
            for(int i=0;i<LOWWProcessor::presetNames().size();++i) {
                LOWWProcessor processor; processor.applyFactoryPreset(i);
                require(processor.savePreset(folder.getChildFile(LOWWProcessor::presetNames()[i]+".loww")).wasOk(),"Preset export failed");
            }
            std::cout << "Exported 11 original presets\n"; return 0;
        }
        if(argc==5 && juce::String(argv[1])=="--snapshot") {
            LOWWProcessor processor; processor.standalone=true; processor.applyFactoryPreset(4);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            editor->setSize(juce::String(argv[3]).getIntValue(),juce::String(argv[4]).getIntValue());
            auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds());
            juce::File file(argv[2]); auto stream=file.createOutputStream();
            require(stream!=nullptr,"Cannot create snapshot"); stream->setPosition(0); stream->truncate();
            juce::PNGImageFormat format; require(format.writeImageToStream(snapshot,*stream),"Cannot render snapshot");
            std::cout << "Rendered editor snapshot\n"; return 0;
        }
        if(argc==4 && juce::String(argv[1])=="--logo") {
            // Renders the vector mark (Source/Logo.h) as a transparent PNG, e.g. Resources/Icon.png at 1024.
            const int size=juce::String(argv[3]).getIntValue(); require(size>=16,"Logo size too small");
            juce::Image image(juce::Image::ARGB,size,size,true);
            { juce::Graphics g(image); loww::drawLogoMark(g,juce::Rectangle<float>((float)size,(float)size).reduced((float)size*.1f),true); }
            juce::File file(argv[2]); auto stream=file.createOutputStream();
            require(stream!=nullptr,"Cannot create logo"); stream->setPosition(0); stream->truncate();
            juce::PNGImageFormat format; require(format.writeImageToStream(image,*stream),"Cannot render logo");
            std::cout << "Rendered logo\n"; return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--monitor") { testMonitoring(); return 0; }
        if(argc==3 && juce::String(argv[1])=="--demo") { renderDemo(juce::File(argv[2])); return 0; }
        if (argc == 3 && juce::String(argv[1]) == "--host") {
#if JUCE_MAC
            if (juce::String(argv[2]).endsWith(".component")) {
                // Register only inside this test process; no system installation required.
                const auto executable = juce::File(argv[2]).getChildFile("Contents/MacOS/LOWW");
                auto* module = dlopen(executable.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
                require(module != nullptr, "Couldn't load AU executable");
                const auto factory = reinterpret_cast<AudioComponentFactoryFunction>(dlsym(module,"LOWWAUFactory"));
                require(factory != nullptr,"AU factory export missing");
                const AudioComponentDescription description {0x61756678,0x54726662,0x5472666c,0,0};
                require(AudioComponentRegister(&description,CFSTR("LOWW Audio: LOWW"),0x100,factory) != nullptr,
                        "Process-local AU registration failed");
            }
#endif
            juce::AudioPluginFormatManager manager;
            juce::addDefaultFormatsToManager(manager);
            juce::OwnedArray<juce::PluginDescription> descriptions;
            for (auto* format : manager.getFormats()) format->findAllTypesForFile(descriptions, argv[2]);
            require(!descriptions.isEmpty(), "Host couldn't discover plugin bundle");
            juce::String error;
            auto plugin = manager.createPluginInstance(*descriptions[0], 48000, 256, error);
            if (!plugin) throw std::runtime_error(error.toStdString());
            require(plugin->getParameters().size() >= 25, "Host parameters missing");
            plugin->prepareToPlay(48000,256);
            juce::AudioBuffer<float> buffer(2,256);
            juce::MidiBuffer midi;
            float peak = 0;
            for (int b = 0; b < 100; ++b) {
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i)
                    buffer.setSample(ch,i,.1f * (float) std::sin(2 * juce::MathConstants<double>::pi * 110 * (b*256+i)/48000));
                plugin->processBlock(buffer,midi);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i)
                    require(std::isfinite(buffer.getSample(ch,i)), "Hosted plugin generated non-finite audio");
                peak = juce::jmax(peak,buffer.getMagnitude(0,256));
            }
            require(peak > .001f && peak < 2, "Hosted plugin output level invalid");
            juce::MemoryBlock state; plugin->getStateInformation(state);
            require(state.getSize() > 0, "Host state serialization is empty");
            plugin->setStateInformation(state.getData(), (int) state.getSize());
            std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
            require(editor != nullptr && editor->getWidth() == 1120, "Plugin editor failed to instantiate");
            editor.reset();
            plugin->releaseResources();
            std::cout << "PASS " << descriptions[0]->pluginFormatName << " bundle discovery, instantiation, parameters, audio, state and editor\n";
            return 0;
        }
        if(argc==2 && juce::String(argv[1])=="--pitch") { testTransposeAndTuner(); return 0; }
        testNeutralResponse(); testBypassAndChannels(); testTone(); testCabAndGate(); testStateAndStress(); testEmbeddedIR(); testNewVoicing(); testDualCab(); testTransposeAndTuner(); testPresetMigrationAndMidi(); testHostRecallAndRealtime(); testGateTransposeMonoAndPriming();
        std::cout << "All audio checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
