#include "Processor.h"
#include "Editor.h"

juce::AudioProcessorValueTreeState::ParameterLayout LOWWProcessor::makeParameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&](const char* id, const char* name, float lo, float hi, float initial,
                   const char* unit = "", float skew = 1.0f) {
        auto range = juce::NormalisableRange<float>(lo, hi, juce::String(id) == "transpose" ? 1.f : .01f, skew);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name,
            range, initial, juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    auto toggle = [&](const char* id, const char* name, bool initial) {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, initial));
    };
    add("input", "Input", -24, 24, 0, "dB");
    add("output", "Output", -36, 12, -6, "dB");
    add("mix", "Mix", 0, 100, 100, "%");
    add("gate", "Gate threshold", -90, -20, -65, "dB");
    add("lowCut", "Low crossover", 60, 400, 200, "Hz", .5f);
    add("highCut", "High crossover", 500, 3000, 800, "Hz", .5f);
    add("compression", "Low compression", 0, 100, 50, "%");
    add("midDrive", "Mid drive", 0, 100, 35, "%");
    add("highDrive", "High drive", 0, 100, 25, "%");
    add("lowLevel", "Low level", -36, 12, 0, "dB");
    add("midLevel", "Mid level", -36, 12, -6, "dB");
    add("highLevel", "High level", -36, 12, -9, "dB");
    for (int i = 0; i < 6; ++i) {
        const auto id = "eq" + juce::String(i);
        const auto name = "EQ " + juce::String(i + 1);
        add(id.toRawUTF8(), name.toRawUTF8(), -12, 12, 0, "dB");
    }
    toggle("gateOn", "Gate enabled", false);
    toggle("lowOn", "Compressor enabled", true);
    toggle("midOn", "Mid distortion enabled", true);
    toggle("highOn", "High distortion enabled", true);
    toggle("eqOn", "EQ enabled", true);
    toggle("cabOn", "Cabinet enabled", true);
    toggle("bypass", "Bypass", false);
    add("parallelLow", "Parallel low pass", 70, 400, 200, "Hz", .5f);
    add("parallelHigh", "Parallel high pass", 100, 2000, 800, "Hz", .5f);
    toggle("parallel", "Parallel voicing", true);
    toggle("monoLow", "Mono low band", false);
    add("transpose", "Transpose", -12, 12, 0, "st");
    for (int i = 0; i < 2; ++i) {
        const auto prefix = "cab" + juce::String(i);
        add((prefix + "Level").toRawUTF8(), ("Cab " + juce::String(i+1) + " level").toRawUTF8(), -36, 6, i == 0 ? 0 : -6, "dB");
        add((prefix + "Pan").toRawUTF8(), ("Cab " + juce::String(i+1) + " pan").toRawUTF8(), -100, 100, 0, "%");
        toggle((prefix + "Enabled").toRawUTF8(), ("Cab " + juce::String(i+1) + " enabled").toRawUTF8(), i == 0);
        toggle((prefix + "Phase").toRawUTF8(), ("Cab " + juce::String(i+1) + " polarity").toRawUTF8(), false);
    }
    return layout;
}
LOWWProcessor::LOWWProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TRIFOLD", makeParameters()) {
    engine.useStockCab(); engine.useStockCab(1);
    for (auto& mapping : midiMap) mapping.store(-1);
    for (auto* parameter : getParameters())
        if (auto* id = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)) state.addParameterListener(id->paramID, this);
    state.state.setProperty("schema", 2, nullptr);
    state.state.setProperty("presetName", "Parallel Foundation", nullptr);
    state.state.setProperty("cabinet", "Studio 4x10 / synthetic", nullptr);
}
LOWWProcessor::~LOWWProcessor() {
    for (auto* parameter : getParameters())
        if (auto* id = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)) state.removeParameterListener(id->paramID, this);
    for (auto& packet : pendingIR) delete packet.load();
    reclaimIR();
}
bool LOWWProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto output = l.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && l.getMainInputChannelSet() == output;
}
void LOWWProcessor::prepareToPlay(double sr, int block) {
    currentRate.store(sr);
    reclaimIR();
    consumeIR();
    engine.prepare(sr, block, settings().transpose);
    setLatencySamples(engine.latency());
}
// getRawParameterValue takes a StringRef and searches a StringRef-keyed map, so this never allocates.
float LOWWProcessor::value(const char* id) const { return state.getRawParameterValue(id)->load(); }
loww::Settings LOWWProcessor::settings() const {
    loww::Settings s;
    s.input = value("input"); s.output = value("output"); s.mix = value("mix") * .01f;
    s.gate = value("gate"); s.lowCut = value("lowCut"); s.highCut = value("highCut");
    s.compression = value("compression") * .01f;
    s.midDrive = value("midDrive") * .01f; s.highDrive = value("highDrive") * .01f;
    s.lowLevel = value("lowLevel"); s.midLevel = value("midLevel"); s.highLevel = value("highLevel");
    s.gateOn = value("gateOn") > .5f; s.lowOn = value("lowOn") > .5f;
    s.midOn = value("midOn") > .5f; s.highOn = value("highOn") > .5f;
    s.eqOn = value("eqOn") > .5f; s.cabOn = value("cabOn") > .5f; s.bypass = value("bypass") > .5f;
    constexpr const char* ids[] {"eq0", "eq1", "eq2", "eq3", "eq4", "eq5"};
    for (size_t i = 0; i < 6; ++i) s.eq[i] = value(ids[i]);
    s.parallel = value("parallel") > .5f; s.monoLow = value("monoLow") > .5f;
    s.transpose = std::round(value("transpose"));
    if (s.parallel) { s.lowCut = value("parallelLow"); s.highCut = value("parallelHigh"); }
    constexpr const char* levels[] {"cab0Level", "cab1Level"}, *pans[] {"cab0Pan", "cab1Pan"};
    constexpr const char* enables[] {"cab0Enabled", "cab1Enabled"}, *phases[] {"cab0Phase", "cab1Phase"};
    for (size_t i = 0; i < 2; ++i) {
        s.cabLevel[i] = value(levels[i]); s.cabPan[i] = value(pans[i]) * .01f;
        s.cabEnabled[i] = value(enables[i]) > .5f; s.cabPhase[i] = value(phases[i]) > .5f;
    }
    return s;
}
void LOWWProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) {
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        if (!message.isController()) continue;
        const auto cc = (size_t)message.getControllerNumber();
        const int learning = midiLearn.exchange(-1);
        if (learning >= 0) midiMap[cc].store(learning);
        const int index = midiMap[cc].load();
        if (index >= 0 && index < getParameters().size())
            getParameters()[index]->setValueNotifyingHost((float)message.getControllerValue() / 127.f);
    }
    midi.clear(); render(b, false);
}
void LOWWProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) {
    midi.clear(); render(b, true);
}
void LOWWProcessor::render(juce::AudioBuffer<float>& b, bool hostBypass) {
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < b.getNumChannels(); ++ch) b.clear(ch, 0, b.getNumSamples());
    int start1, size1, start2, size2;
    consumeIR();
    float inPeak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        inPeak = juce::jmax(inPeak, b.getMagnitude(ch, 0, b.getNumSamples()));
    inputMeter.store(juce::jmax(inPeak, inputMeter.load() * .94f));
    auto s = settings();
    s.bypass = s.bypass || hostBypass;
    int ta, tn, tb, tm;
    tunerQueue.prepareToWrite(b.getNumSamples(), ta, tn, tb, tm);
    for (int i = 0; i < tn; ++i) tunerData[(size_t)(ta+i)] = b.getSample(0, i);
    for (int i = 0; i < tm; ++i) tunerData[(size_t)(tb+i)] = b.getSample(0, tn+i);
    tunerQueue.finishedWrite(tn+tm);
    engine.process(b, s);
    if (getLatencySamples() != engine.latency()) setLatencySamples(engine.latency());
    compressionMeter.store(engine.reductionDb());
    const bool ticking = standalone && metronomeOn.load() && !s.bypass;
    if (ticking) {
        const auto sr = currentRate.load();
        const auto step = juce::jlimit(30.f, 300.f, tempo.load()) / (60.0 * sr);
        const int bar = juce::jlimit(1, 12, beatsPerBar.load());
        if (!wasMetronomeOn) { beatPhase = 1; beat = bar - 1; }
        for (int i = 0; i < b.getNumSamples(); ++i) {
            if (beatPhase >= 1) { beatPhase -= 1; beat = (beat+1)%bar; clickEnvelope = .12f; clickPhase = 0; }
            const auto click = clickEnvelope * (float)std::sin(clickPhase);
            clickPhase += juce::MathConstants<double>::twoPi * (beat == 0 ? 1600 : 1000) / sr;
            clickEnvelope *= (float)std::exp(-1.0 / (.012 * sr));
            beatPhase += step;
            for (int ch = 0; ch < b.getNumChannels(); ++ch) b.addSample(ch, i, click);
        }
    }
    wasMetronomeOn = ticking;
    float outPeak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        outPeak = juce::jmax(outPeak, b.getMagnitude(ch, 0, b.getNumSamples()));
    outputMeter.store(juce::jmax(outPeak, outputMeter.load() * .94f));
    spectrumQueue.prepareToWrite(b.getNumSamples(), start1, size1, start2, size2);
    const auto copy = [&](int start, int size, int offset) {
        for (int i = 0; i < size; ++i) {
            float sum = 0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch) sum += b.getSample(ch, offset + i);
            spectrumData[(size_t) (start + i)] = sum / (float) juce::jmax(1, b.getNumChannels());
        }
    };
    copy(start1, size1, 0); copy(start2, size2, size1);
    spectrumQueue.finishedWrite(size1 + size2);
}
bool LOWWProcessor::readSpectrum(float* dest, int count) {
    if (spectrumQueue.getNumReady() < count) return false;
    int a, n, b, m;
    spectrumQueue.prepareToRead(count, a, n, b, m);
    std::copy_n(spectrumData.data() + a, n, dest);
    std::copy_n(spectrumData.data() + b, m, dest + n);
    spectrumQueue.finishedRead(n + m);
    return true;
}
juce::AudioProcessorParameter* LOWWProcessor::getBypassParameter() const { return state.getParameter("bypass"); }
juce::AudioProcessorEditor* LOWWProcessor::createEditor() { return new LOWWEditor(*this); }
juce::Result LOWWProcessor::queueIR(juce::AudioBuffer<float>&& buffer, double sr, int slot) {
    const juce::ScopedLock lock(irProducerLock);
    reclaimIR();
    auto packet = std::make_unique<IRPacket>();
    packet->audio = std::move(buffer);
    packet->rate = sr;
    delete pendingIR[(size_t)slot].exchange(packet.release());
    return juce::Result::ok();
}
void LOWWProcessor::consumeIR() {
    for (int slot = 0; slot < 2; ++slot) if (auto* packet = pendingIR[(size_t)slot].exchange(nullptr)) {
        engine.loadIR(std::move(packet->audio), packet->rate, slot);
        packet->next = retiredIR.load();
        while (!retiredIR.compare_exchange_weak(packet->next, packet)) {}
    }
}
void LOWWProcessor::reclaimIR() {
    auto* packet = retiredIR.exchange(nullptr);
    while (packet != nullptr) {
        auto* next = packet->next;
        delete packet;
        packet = next;
    }
}
juce::Result LOWWProcessor::loadIRFile(const juce::File& file, int slot) {
    if (slot < 0 || slot > 1) return juce::Result::fail("Invalid cabinet slot");
    const juce::String suffix = slot == 0 ? "" : "B";
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader) return juce::Result::fail("Couldn't read this audio file. Choose a WAV or AIFF impulse response.");
    if (reader->lengthInSamples < 1 || (double) reader->lengthInSamples > reader->sampleRate * 2
        || reader->sampleRate < 8000 || reader->sampleRate > 192000 || reader->numChannels > 2)
        return juce::Result::fail("Use a mono or stereo IR up to 2 seconds, at 8-192 kHz.");
    juce::AudioBuffer<float> ir((int) reader->numChannels, (int) reader->lengthInSamples);
    if (!reader->read(&ir, 0, ir.getNumSamples(), 0, true, true))
        return juce::Result::fail("Couldn't decode the impulse response.");
    juce::MemoryBlock data;
    for (int ch = 0; ch < ir.getNumChannels(); ++ch) {
        for (int i = 0; i < ir.getNumSamples(); ++i)
            if (!std::isfinite(ir.getSample(ch, i))) return juce::Result::fail("The IR contains invalid samples.");
        data.append(ir.getReadPointer(ch), (size_t) ir.getNumSamples() * sizeof(float));
    }
    const auto channels = ir.getNumChannels(), samples = ir.getNumSamples();
    // Queue and describe the IR in one transaction so a concurrent host restore can't interleave.
    const juce::ScopedLock metadata(metadataLock);
    auto result = queueIR(std::move(ir), reader->sampleRate, slot);
    if (result.failed()) return result;
    state.state.setProperty("irData" + suffix, data.toBase64Encoding(), nullptr);
    state.state.setProperty("irChannels" + suffix, channels, nullptr);
    state.state.setProperty("irSamples" + suffix, samples, nullptr);
    state.state.setProperty("irRate" + suffix, reader->sampleRate, nullptr);
    state.state.setProperty("cabinet" + suffix, file.getFileNameWithoutExtension(), nullptr);
    presetDirty.store(true);
    return result;
}
juce::Result LOWWProcessor::restoreStockCab(int slot) {
    if (slot < 0 || slot > 1) return juce::Result::fail("Invalid cabinet slot");
    const juce::String suffix = slot == 0 ? "" : "B";
    const juce::ScopedLock metadata(metadataLock);
    auto result = queueIR(loww::Engine::makeStockIR(), 48000, slot);
    if (result.wasOk()) {
        state.state.removeProperty("irData" + suffix, nullptr);
        state.state.setProperty("cabinet" + suffix, "Studio / synthetic", nullptr);
        presetDirty.store(true);
    }
    return result;
}
juce::String LOWWProcessor::cabinetName(int slot) {
    const juce::String suffix = slot == 0 ? "" : "B";
    const juce::ScopedLock metadata(metadataLock);
    return state.state.hasProperty("irData" + suffix) ? state.state.getProperty("cabinet" + suffix).toString() : "Studio / synthetic";
}
void LOWWProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto tree = [this] { const juce::ScopedLock metadata(metadataLock); return state.copyState(); }();
    tree.setProperty("presetDirty", presetDirty.load(), nullptr);
    for (int cc = 0; cc < 128; ++cc) {
        tree.removeProperty("midiCC" + juce::String(cc), nullptr);
        const int index = midiMap[(size_t)cc].load();
        if (index >= 0 && index < getParameters().size())
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(getParameters()[index]))
                tree.setProperty("midiCC" + juce::String(cc), p->paramID, nullptr);
    }
    if (auto xml = tree.createXml()) copyXmlToBinary(*xml, block);
}
void LOWWProcessor::setStateInformation(const void* data, int bytes) {
    const juce::ScopedLock metadata(metadataLock);
    if (restoreState(data, bytes).wasOk()) ++hostRestoreCount;
}
juce::Result LOWWProcessor::restoreState(const void* data, int bytes) {
    if (bytes < 1 || bytes > 16 * 1024 * 1024) return juce::Result::fail("Invalid preset size");
    auto xml = getXmlFromBinary(data, bytes);
    if (!xml || !xml->hasTagName("TRIFOLD")) return juce::Result::fail("Not a LOWW preset");
    auto tree = juce::ValueTree::fromXml(*xml);
    const int schema = tree.getProperty("schema", 1);
    if (schema < 1 || schema > 2) return juce::Result::fail("Unsupported preset version");
    // Fill missing parameters explicitly: old presets must never inherit a new tone.
    for (auto* parameter : getParameters()) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (!p || tree.getChildWithProperty("id", p->paramID).isValid()) continue;
        juce::ValueTree child("PARAM"); child.setProperty("id", p->paramID, nullptr);
        const auto initial = schema == 1 && (p->paramID == "parallel" || p->paramID == "monoLow") ? 0.f : p->convertFrom0to1(p->getDefaultValue());
        child.setProperty("value", initial, nullptr); tree.addChild(child, -1, nullptr);
    }
    tree.setProperty("schema", 2, nullptr);
    for (const auto& child : tree) {
        const auto id = child.getProperty("id").toString();
        if (auto* param = state.getParameter(id)) {
            const auto v = (float) child.getProperty("value");
            const auto& range = param->getNormalisableRange();
            if (!std::isfinite(v) || v < range.start || v > range.end)
                return juce::Result::fail("Preset contains an invalid parameter");
        }
    }
    std::array<juce::AudioBuffer<float>, 2> decoded;
    std::array<double, 2> rates {48000,48000};
    for (int slot = 0; slot < 2; ++slot) {
        const juce::String suffix = slot == 0 ? "" : "B";
        if (!tree.hasProperty("irData" + suffix)) { decoded[(size_t)slot] = loww::Engine::makeStockIR(); continue; }
        juce::MemoryBlock memory;
        const int channels = tree.getProperty("irChannels" + suffix, 0), samples = tree.getProperty("irSamples" + suffix, 0);
        const double sr = tree.getProperty("irRate" + suffix, 48000);
        if (channels < 1 || channels > 2 || samples < 1 || !std::isfinite(sr) || sr < 8000 || sr > 192000 || samples > sr * 2
            || !memory.fromBase64Encoding(tree.getProperty("irData" + suffix).toString())
            || memory.getSize() != (size_t)channels * (size_t)samples * sizeof(float))
            return juce::Result::fail("Invalid embedded cabinet IR");
        auto& ir = decoded[(size_t)slot]; ir.setSize(channels, samples); rates[(size_t)slot] = sr;
        for (int ch = 0; ch < channels; ++ch) {
            std::memcpy(ir.getWritePointer(ch), (const char*)memory.getData() + (size_t)ch * (size_t)samples * sizeof(float), (size_t)samples * sizeof(float));
            for (int i = 0; i < samples; ++i) if (!std::isfinite(ir.getSample(ch,i))) return juce::Result::fail("Invalid IR samples");
        }
    }
    const juce::ScopedLock metadata(metadataLock);
    for (int slot = 0; slot < 2; ++slot) queueIR(std::move(decoded[(size_t)slot]), rates[(size_t)slot], slot);
    state.replaceState(tree);
    for (int cc = 0; cc < 128; ++cc) {
        const auto id = tree.getProperty("midiCC" + juce::String(cc)).toString();
        auto* p = state.getParameter(id);
        midiMap[(size_t)cc].store(p ? p->getParameterIndex() : -1);
    }
    presetDirty.store((bool)tree.getProperty("presetDirty", false));
    return juce::Result::ok();
}
juce::StringArray LOWWProcessor::presetNames() {
    return {"Foundation", "Clean weight", "Pick attack", "Split fury", "Deep Edge", "Progressive Grind", "Mid Focus", "Dual Amp Growl", "Modern Articulation", "Clean Funk", "Midrange Roar"};
}
juce::String LOWWProcessor::presetDescription(int index) {
    static const char* descriptions[] {
        "Original LOWW crossover tone", "Original LOWW clean tone", "Original LOWW picked tone", "Original LOWW aggressive tone",
        "Geddy Lee inspired clean/crunch blend", "Chris Squire inspired blended amplification",
        "Justin Chancellor inspired midrange foundation", "Tim Commerford inspired clean/dirty blend",
        "Nolly inspired compressed lows and driven highs", "Flea inspired clean funk foundation",
        "Lemmy inspired midrange drive"
    };
    return juce::isPositiveAndBelow(index, 11) ? juce::String(descriptions[index]) : juce::String();
}
void LOWWProcessor::applyFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, presetNames().size())) return;
    // A factory tone keeps both selected cabinet files and every cabinet mix setting.
    for (auto* parameter : getParameters()) {
        auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        if (p && p->paramID.startsWith("cab")) continue;
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->getDefaultValue()); parameter->endChangeGesture();
    }
    auto set = [&](const char* id, float v) {
        auto* p = state.getParameter(id);
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    };
    set("parallel", index >= 4 ? 1.f : 0.f); set("monoLow", index >= 4 ? 1.f : 0.f);
    if (index == 1) { set("midDrive", 0); set("highDrive", 0); set("compression", 35); set("midLevel", -2); set("highLevel", -4); }
    if (index == 2) { set("midDrive", 55); set("highDrive", 45); set("compression", 65); set("eq2", -3); set("eq4", 3); }
    if (index == 3) { set("midDrive", 85); set("highDrive", 75); set("compression", 80); set("eq1", -2); set("eq3", 3); set("output", -9); }
    if (index >= 4) {
        // Original recipes inferred from documented rig architectures, never copied artist settings.
        struct Recipe { float low, high, comp, mid, top, l, m, h; std::array<float,6> eq; };
        static constexpr Recipe recipes[] {
            {180,1500,55,45,30,0,-5,-8,{2,-2,0,1,3,-1}},
            {180,1700,45,60,35,-1,-3,-7,{0,-2,1,3,2,-2}},
            {220,1800,50,40,25,0,-4,-9,{0,1,2,2,1,-3}},
            {200,1200,55,45,15,1,-4,-10,{1,2,1,0,-1,-4}},
            {180,1800,75,65,55,0,-5,-8,{1,-3,-2,1,3,-2}},
            {180,1500,35,0,0,0,0,-2,{2,-1,-1,0,1,1}},
            {140,1200,35,80,50,-5,-2,-7,{-3,1,3,4,1,-4}}
        };
        const auto& r = recipes[index-4];
        set("lowCut", r.low); set("parallelLow", r.low); set("parallelHigh", r.high); set("compression", r.comp);
        set("midDrive", r.mid); set("highDrive", r.top); set("lowLevel", r.l); set("midLevel", r.m); set("highLevel", r.h); set("output", -9);
        if (index == 9) { set("midOn",0); set("highOn",0); }
        for (int i = 0; i < 6; ++i) set(("eq"+juce::String(i)).toRawUTF8(), r.eq[(size_t)i]);
    }
    const juce::ScopedLock metadata(metadataLock);
    state.state.setProperty("presetName", presetNames()[index], nullptr);
    state.state.setProperty("presetDescription", presetDescription(index), nullptr);
    presetDirty.store(false);
}
juce::String LOWWProcessor::presetName() const {
    const juce::ScopedLock metadata(metadataLock);
    return state.state.getProperty("presetName", "Untitled").toString();
}
juce::String LOWWProcessor::presetInfo() const {
    const juce::ScopedLock metadata(metadataLock);
    return state.state.getProperty("presetDescription").toString();
}
juce::File LOWWProcessor::presetFolder() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LOWW Presets");
}
juce::Result LOWWProcessor::savePreset(const juce::File& file, int expectedHostRestores) {
    const juce::ScopedLock metadata(metadataLock);
    if (expectedHostRestores >= 0 && expectedHostRestores != hostRestoreCount.load())
        return juce::Result::fail("The host replaced this tone; use Save as to choose a file");
    auto tree = state.copyState();
    tree.setProperty("presetName", file.getFileNameWithoutExtension(), nullptr);
    tree.setProperty("presetDirty", false, nullptr);
    // MIDI mappings belong to host state, not interchangeable tone presets.
    for (int cc = 0; cc < 128; ++cc) tree.removeProperty("midiCC"+juce::String(cc), nullptr);
    juce::MemoryBlock block; copyXmlToBinary(*tree.createXml(), block);
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithData(block.getData(), block.getSize()) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Couldn't save preset");
    state.state.setProperty("presetName", file.getFileNameWithoutExtension(), nullptr); presetDirty.store(false);
    return juce::Result::ok();
}
juce::Result LOWWProcessor::openPreset(const juce::File& file) {
    // One transaction, so a concurrent host restore can't interleave with the MIDI map and name updates.
    const juce::ScopedLock metadata(metadataLock);
    if (file.getSize() > 16 * 1024 * 1024) return juce::Result::fail("Preset is too large");
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block)) return juce::Result::fail("Couldn't read preset");
    std::array<int,128> mapping;
    for (size_t i = 0; i < mapping.size(); ++i) mapping[i] = midiMap[i].load();
    auto result = restoreState(block.getData(), (int)block.getSize());
    for (size_t i = 0; i < mapping.size(); ++i) midiMap[i].store(mapping[i]);
    if (result.wasOk()) { state.state.setProperty("presetName", file.getFileNameWithoutExtension(), nullptr); presetDirty.store(false); }
    return result;
}
void LOWWProcessor::learnMidi(const juce::String& id) {
    if (auto* p = state.getParameter(id)) midiLearn.store(p->getParameterIndex());
}
void LOWWProcessor::clearMidi(const juce::String& id) {
    if (auto* p = state.getParameter(id)) for (auto& mapping : midiMap) if (mapping.load() == p->getParameterIndex()) mapping.store(-1);
    midiLearn.store(-1);
}
bool LOWWProcessor::readTuner(float* dest, int count) {
    if (tunerQueue.getNumReady() < count) return false;
    int a,n,b,m; tunerQueue.prepareToRead(count,a,n,b,m);
    std::copy_n(tunerData.data()+a,n,dest); std::copy_n(tunerData.data()+b,m,dest+n); tunerQueue.finishedRead(n+m); return true;
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LOWWProcessor(); }
