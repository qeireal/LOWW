#include "Editor.h"
#include "Tuner.h"
#include "Logo.h"
#include <complex>
#include <LOWWFonts.h>

namespace {
// Neumorphism: every surface shares one grey and is lifted or pressed by a white/slate shadow pair.
const juce::Colour background {0xff2c3038}, well {0xff262a31}, line {0xff3b414b};
const juce::Colour highlight {0xff3c424d}, shade {0xff131519};
const juce::Colour ink {0xffe6e9ee}, muted {0xff8b94a3}, onAccent {0xff0d1a1f};
const juce::Colour cyan {0xff22ecff}, cyanDeep {0xff00b3cc}, slate {0xffa3aebd}, clip {0xffff6b7a};
const juce::Colour lowTone {0xff0a93a8}; const juce::Colour midTone = cyan, highTone = slate;
// Open Runde, embedded. Fonts are built from these typefaces directly because JUCE resolves
// default fonts through the process-wide LookAndFeel, which a plugin shouldn't take over.
enum class Weight { regular, medium, semibold, bold };
juce::Typeface::Ptr typeface(Weight weight) {
    static const std::array<juce::Typeface::Ptr, 4> faces {
        juce::Typeface::createSystemTypefaceFor(LOWWFonts::OpenRundeRegular_ttf, LOWWFonts::OpenRundeRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(LOWWFonts::OpenRundeMedium_ttf, LOWWFonts::OpenRundeMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(LOWWFonts::OpenRundeSemibold_ttf, LOWWFonts::OpenRundeSemibold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(LOWWFonts::OpenRundeBold_ttf, LOWWFonts::OpenRundeBold_ttfSize)};
    return faces[(size_t) weight];
}
// Small caps-style labels read better with a little tracking.
juce::Font uiFont(float size, Weight weight = Weight::regular, float tracking = 0) {
    return juce::Font(juce::FontOptions(typeface(weight)).withHeight(size).withKerningFactor(tracking));
}
void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r,
          float size, juce::Colour colour, Weight weight = Weight::regular, float tracking = 0) {
    g.setColour(colour);
    g.setFont(uiFont(size, weight, tracking));
    g.drawText(s, r, juce::Justification::centredLeft);
}
// Layered translucent fills approximate a blurred shadow without per-frame image blurs.
// Bento grid: eight 120 px columns with 16 px gutters between 24 px margins, and 16 px between rows,
// so every panel edge lands on a shared column line.
namespace layout {
using R = juce::Rectangle<int>;
constexpr int column = 120, gutter = 16, margin = 24;
inline R span(int first, int count, int y, int h) {
    return {margin + first * (column + gutter), y, count * column + (count - 1) * gutter, h};
}
const R inputs = span(0,2,84,160), centre = span(2,4,84,160), outputs = span(6,2,84,160);
const R graphWell = span(0,8,260,124);
const R bands[3] {span(0,3,400,164), span(3,2,400,164), span(5,3,400,164)};
const R eq = span(0,4,580,124), cabinet = span(4,4,580,124);
const R cabs[2] {span(0,4,720,136), span(4,4,720,136)};
const juce::Rectangle<float> graph = graphWell.toFloat().reduced(12,4);
}
void softShadow(juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Point<float> offset,
                juce::Colour colour, float spread, bool inner) {
    constexpr int steps = 8;
    g.setColour(colour.withMultipliedAlpha(1.f / steps));
    for (int i = 0; i < steps; ++i) {
        const float e = spread * (float) (i + 1) / steps;
        if (!inner) { g.fillRoundedRectangle(r.translated(offset.x, offset.y).expanded(e * .6f), radius + e * .6f); continue; }
        juce::Path wall; wall.addRectangle(r.expanded(spread * 2 + std::abs(offset.x) + std::abs(offset.y)));
        const auto hole = r.translated(offset.x, offset.y).reduced(e * .5f);
        wall.addRoundedRectangle(hole, juce::jmax(0.f, radius - e * .5f)); wall.setUsingNonZeroWinding(false);
        g.fillPath(wall);
    }
}
void raised(juce::Graphics& g, juce::Rectangle<float> r, float radius, float depth, juce::Colour face = background) {
    softShadow(g, r, radius, {depth, depth}, shade.withAlpha(.7f), depth * 2.6f, false);
    softShadow(g, r, radius, {-depth, -depth}, highlight.withAlpha(.8f), depth * 2.6f, false);
    g.setGradientFill(juce::ColourGradient(face.brighter(.07f), r.getTopLeft(), face.darker(.06f), r.getBottomRight(), false));
    g.fillRoundedRectangle(r, radius);
}
void inset(juce::Graphics& g, juce::Rectangle<float> r, float radius, float depth, juce::Colour face = well) {
    g.setColour(face); g.fillRoundedRectangle(r, radius);
    juce::Graphics::ScopedSaveState saved(g);
    juce::Path shape; shape.addRoundedRectangle(r, radius); g.reduceClipRegion(shape);
    softShadow(g, r, radius, {depth, depth}, shade.withAlpha(.8f), depth * 2, true);
    softShadow(g, r, radius, {-depth, -depth}, highlight.withAlpha(.7f), depth * 2, true);
}
}
LOWWEditor::Theme::Theme() {
    setColourScheme(getDarkColourScheme());
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, cyan.withAlpha(.3f));
    setColour(juce::Slider::trackColourId, cyan);
    setColour(juce::Slider::backgroundColourId, line);
    setColour(juce::Label::textColourId, muted);
    setColour(juce::Label::textWhenEditingColourId, ink);
    setColour(juce::TextEditor::backgroundColourId, well);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::highlightColourId, cyan.withAlpha(.3f));
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, cyan);
    setColour(juce::CaretComponent::caretColourId, cyan);
    setColour(juce::ComboBox::backgroundColourId, well);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, muted);
    setColour(juce::PopupMenu::backgroundColourId, background);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::headerTextColourId, muted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, cyan);
    setColour(juce::PopupMenu::highlightedTextColourId, onAccent);
    setColour(juce::TextButton::buttonColourId, background);
    setColour(juce::TextButton::buttonOnColourId, background);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, cyan);
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, cyan);
    setColour(juce::ToggleButton::tickDisabledColourId, muted);
    setColour(juce::TooltipWindow::backgroundColourId, background);
    setColour(juce::TooltipWindow::textColourId, ink);
    setColour(juce::TooltipWindow::outlineColourId, line);
}
void LOWWEditor::Theme::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                            float pos, float start, float end, juce::Slider& slider) {
    const auto area = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(6);
    const auto radius = juce::jmin(area.getWidth(), area.getHeight()) * .5f;
    const auto centre = area.getCentre();
    const auto angle = start + pos * (end - start);
    const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    // Pressed well holding the value arc, with a raised knob sitting in it.
    inset(g, juce::Rectangle<float>(radius * 2, radius * 2).withCentre(centre), radius, 2.5f);
    const auto arcRadius = radius - 5.5f;
    juce::Path ring;
    ring.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0, start, end, true);
    g.setColour(line); g.strokePath(ring, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path active;
    active.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0, start, angle, true);
    g.setColour(colour.withAlpha(.22f)); g.strokePath(active, juce::PathStrokeType(7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour); g.strokePath(active, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto knob = radius - 11.f;
    raised(g, juce::Rectangle<float>(knob * 2, knob * 2).withCentre(centre), knob, 2.5f);
    const juce::Point<float> dot {centre.x + std::sin(angle) * knob * .62f, centre.y - std::cos(angle) * knob * .62f};
    g.setColour(colour.withAlpha(.3f)); g.fillEllipse(juce::Rectangle<float>(9, 9).withCentre(dot));
    g.setColour(colour); g.fillEllipse(juce::Rectangle<float>(5, 5).withCentre(dot));
}
void LOWWEditor::Theme::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                            juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (!slider.isVertical()) { LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, slider); return; }
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    const auto track = juce::Rectangle<float>(bounds.getCentreX() - 3.5f, bounds.getY() - 6, 7, bounds.getHeight() + 12);
    inset(g, track, 3.5f, 1.5f);
    const auto zero = juce::jlimit(track.getY(), track.getBottom(), (float) slider.getPositionOfValue(0.0));
    const auto from = juce::jmin(zero, pos), to = juce::jmax(zero, pos);
    const auto colour = slider.findColour(juce::Slider::trackColourId);
    g.setColour(colour); g.fillRoundedRectangle(track.reduced(1.5f, 0).withY(from).withBottom(to), 2);
    const auto thumb = juce::Rectangle<float>(13, 13).withCentre({bounds.getCentreX(), pos});
    raised(g, thumb, 6.5f, 1.5f);
    g.setColour(colour); g.fillEllipse(thumb.withSizeKeepingCentre(5, 5));
}
void LOWWEditor::Theme::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                bool highlighted, bool down) {
    const auto bounds = button.getLocalBounds().toFloat();
    const auto depth = juce::jlimit(1.5f, 3.f, bounds.getHeight() * .09f);
    const auto r = bounds.reduced(depth + 1);
    const auto radius = juce::jmin(10.f, r.getHeight() * .4f);
    if (down || button.getToggleState()) inset(g, r, radius, depth);
    else raised(g, r, radius, highlighted ? depth * 1.3f : depth);
}
void LOWWEditor::Theme::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) {
    const auto h = (float) button.getHeight();
    const auto size = juce::jmin(18.f, h * .75f);
    const auto box = juce::Rectangle<float>(4, (h - size) * .5f, size, size);
    if (button.getToggleState()) {
        raised(g, box, 5, 1.5f, cyan);
        juce::Path tick;
        tick.startNewSubPath(box.getX() + size * .26f, box.getCentreY());
        tick.lineTo(box.getX() + size * .44f, box.getY() + size * .7f);
        tick.lineTo(box.getRight() - size * .24f, box.getY() + size * .3f);
        g.setColour(onAccent);
        g.strokePath(tick, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        inset(g, box, 5, down ? 2.f : 1.5f);
        if (highlighted) { g.setColour(cyan.withAlpha(.5f)); g.drawRoundedRectangle(box, 5, 1); }
    }
    g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(button.isEnabled() ? 1.f : .5f));
    g.setFont(uiFont(14.f, Weight::medium));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(juce::roundToInt(size) + 10).withTrimmedRight(2),
                     juce::Justification::centredLeft, 1);
}
void LOWWEditor::Theme::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    const auto r = juce::Rectangle<float>((float) w, (float) h).reduced(1.5f);
    inset(g, r, juce::jmin(10.f, r.getHeight() * .4f), 2);
    const auto arrow = juce::Rectangle<float>(r.getRight() - 22, r.getCentreY() - 3, 10, 6);
    juce::Path chevron;
    chevron.startNewSubPath(arrow.getX(), arrow.getY()); chevron.lineTo(arrow.getCentreX(), arrow.getBottom()); chevron.lineTo(arrow.getRight(), arrow.getY());
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(box.isEnabled() ? 1.f : .4f));
    g.strokePath(chevron, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
juce::Font LOWWEditor::Theme::getTextButtonFont(juce::TextButton&, int height) {
    return uiFont(juce::jlimit(13.f, 15.f, (float) height * .5f), Weight::semibold);
}
juce::Font LOWWEditor::Theme::getComboBoxFont(juce::ComboBox&) { return uiFont(15.f, Weight::medium); }
juce::Font LOWWEditor::Theme::getPopupMenuFont() { return uiFont(15.f); }
juce::Label* LOWWEditor::Theme::createSliderTextBox(juce::Slider& slider) {
    auto* box = LookAndFeel_V4::createSliderTextBox(slider);
    box->setFont(uiFont(14.f, Weight::semibold));
    return box;
}
void LOWWEditor::Theme::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.fillAll(background);
    g.setColour(line); g.drawRect(0, 0, w, h);
}
LOWWEditor::Dial::Dial(LOWWProcessor& p, const char* id, const juce::String& title,
                          juce::Colour colour, bool vertical) {
    label.setText(title, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(uiFont(11.f, Weight::semibold, .08f));
    slider.setSliderStyle(vertical ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 22);
    slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour(juce::Slider::thumbColourId, colour);
    slider.setColour(juce::Slider::trackColourId, colour);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    auto* parameter = p.state.getParameter(id);
    const auto unit = parameter->getLabel();
    slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    slider.setTitle(parameter->getName(100));
    slider.setTooltip("Drag to adjust. Double-click to reset. Type a value. Right-click for MIDI learn.");
    slider.menu = [&p, parameterId = juce::String(id), safe = juce::Component::SafePointer<Dial>(this)] {
        juce::PopupMenu menu; menu.addItem(1, "Learn next MIDI CC"); menu.addItem(2, "Clear MIDI mapping");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(safe.getComponent()), [&p, parameterId, safe](int result) {
            if (!safe) return;
            if (result == 1) p.learnMidi(parameterId); else if (result == 2) p.clearMidi(parameterId);
        });
    };
    addAndMakeVisible(label); addAndMakeVisible(slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, id, slider);
    // Attachments install their own formatters; apply display units after attachment.
    slider.textFromValueFunction = [unit, vertical](double value) {
        if (std::abs(value) < .005) value = 0;
        const auto number = unit == "dB" ? juce::String(value, 1) : juce::String(juce::roundToInt(value));
        return number + (vertical || unit.isEmpty() ? "" : " " + unit);
    };
    slider.valueFromTextFunction = [](const juce::String& v) { return v.getDoubleValue(); };
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    // The text box is built before the Dial joins the themed editor, so give it the theme's ink directly.
    slider.setColour(juce::Slider::textBoxTextColourId, ink);
    label.setColour(juce::Label::textColourId, muted);
    slider.updateText();
}
void LOWWEditor::Dial::resized() {
    auto r = getLocalBounds(); label.setBounds(r.removeFromTop(19)); slider.setBounds(r);
}
LOWWEditor::LOWWEditor(LOWWProcessor& p) : AudioProcessorEditor(p), owner(p), seenHostRestores(p.hostRestoreCount.load()) {
    setLookAndFeel(&theme);
    const auto add = [&](const char* id, const char* label, juce::Colour colour, bool vertical = false) {
        auto dial = std::make_unique<Dial>(p,id,label,colour,vertical); addAndMakeVisible(*dial); dials.push_back(std::move(dial));
    };
    add("input","INPUT",cyan); add("gate","GATE",cyan); add("mix","WET / DRY",cyan); add("output","OUTPUT",cyan);
    add("compression","COMPRESS",lowTone); add("lowCut","LOW PASS",lowTone); add("lowLevel","LEVEL",lowTone);
    add("midDrive","DRIVE",midTone); add("midLevel","LEVEL",midTone);
    add("highDrive","DRIVE",highTone); add("highCut","CROSSOVER",highTone); add("highLevel","LEVEL",highTone);
    const char* labels[] {"80","250","500","1k","1.5k","5k"};
    for(int i=0;i<6;++i) add(("eq"+juce::String(i)).toRawUTF8(),labels[i],cyan,true);
    add("transpose","TRANSPOSE",cyan); add("parallelHigh","HIGH PASS",highTone);
    add("cab0Level","LEVEL",cyan); add("cab0Pan","PAN",cyan);
    add("cab1Level","LEVEL",highTone); add("cab1Pan","PAN",highTone);
    add("parallelLow","LOW PASS",lowTone);
    auto toggle = [&](const char* id,const char* title) {
        auto button = std::make_unique<juce::ToggleButton>(title); addAndMakeVisible(*button);
        switchAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,id,*button));
        switches.push_back(std::move(button));
    };
    toggle("gateOn","Gate"); toggle("bypass","Bypass"); toggle("lowOn","On"); toggle("midOn","On"); toggle("highOn","On");
    toggle("eqOn","EQ"); toggle("cabOn","Cabinet"); toggle("parallel","Parallel voicing"); toggle("monoLow","Mono lows");
    toggle("cab0Enabled","On"); toggle("cab0Phase","Invert phase"); toggle("cab1Enabled","On"); toggle("cab1Phase","Invert phase");
    switches[7]->setTooltip("Parallel: independent low/high filters, fixed 400 Hz mids, 3 ms / 600 ms / 4:1 compression, shelf EQ. Off: original LOWW crossovers and bell EQ.");
    presets.setTextWhenNothingSelected(owner.presetName()); refreshPresets();
    presets.onChange = [this] {
        const int id = presets.getSelectedId();
        if (id > 0 && id <= LOWWProcessor::presetNames().size()) {
            currentFactory = id-1; currentFile = juce::File(); owner.applyFactoryPreset(currentFactory);
            status.setText("Factory tone loaded; current cabinet settings retained",juce::dontSendNotification);
        } else if (id >= 100 && id-100 < userPresets.size()) {
            const auto file = userPresets[id-100];
            const auto result = owner.openPreset(file);
            if (result.wasOk()) { currentFile = file; currentFactory = -1; }
            status.setText(result.wasOk() ? "Preset opened" : result.getErrorMessage(),juce::dontSendNotification);
        }
    };
    previous.onClick = [this] { stepPreset(-1); }; next.onClick = [this] { stepPreset(1); };
    save.onClick = [this] { presetFile(true); }; load.onClick = [this] { presetFile(false); };
    overwrite.onClick = [this] {
        forgetFileAfterHostRestore();
        if (currentFile == juce::File()) { presetFile(true); return; }
        const auto result = owner.savePreset(currentFile, seenHostRestores);
        status.setText(result.wasOk() ? "Preset saved" : result.getErrorMessage(),juce::dontSendNotification);
    };
    folder.onClick = [this] {
        juce::PopupMenu menu; menu.addItem(1,"Refresh library"); menu.addItem(2,"Open preset folder"); menu.addItem(3,"Reset tone to Foundation");
        menu.addItem(4,"Move current user preset to Trash",currentFile.existsAsFile());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&folder),[safe=juce::Component::SafePointer<LOWWEditor>(this)](int choice) {
            if(!safe) return;
            if(choice==1) { safe->refreshPresets(); return; }
            if(choice==3) { safe->owner.applyFactoryPreset(0); safe->currentFactory=0; safe->currentFile=juce::File(); return; }
            if(choice==4) {
                if(safe->currentFile.moveToTrash()) { safe->currentFile=juce::File(); safe->refreshPresets(); safe->status.setText("Preset moved to Trash; current tone retained",juce::dontSendNotification); }
                else safe->status.setText("Could not move preset to Trash",juce::dontSendNotification);
                return;
            }
            if(choice!=2) return;
            const auto dir = LOWWProcessor::presetFolder(); const auto result = dir.createDirectory();
        if (result.wasOk()) dir.revealToUser(); else safe->status.setText(result.getErrorMessage(),juce::dontSendNotification);
        });
    };
    for (int i=0;i<2;++i) {
        loadCab[(size_t)i].setButtonText("Load IR..."); stockCab[(size_t)i].setButtonText("Stock");
        loadCab[(size_t)i].onClick = [this,i] { chooseIR(i); };
        stockCab[(size_t)i].onClick = [this,i] { showResult(owner.restoreStockCab(i)); };
        addAndMakeVisible(loadCab[(size_t)i]); addAndMakeVisible(stockCab[(size_t)i]);
    }
    for(auto* c : std::initializer_list<juce::Component*> {&presets,&save,&load,&previous,&next,&overwrite,&folder,&status,&analyser,&tuner,&tunerLabel,&presetInfo}) addAndMakeVisible(c);
    analyser.setToggleState(true,juce::dontSendNotification);
    tunerLabel.setText("Tuner off",juce::dontSendNotification); tunerLabel.setFont(uiFont(20.f, Weight::semibold));
    tuner.onClick = [this] { if (!tuner.getToggleState()) tunerLabel.setText("Tuner off",juce::dontSendNotification); };
    if (owner.standalone) {
        addAndMakeVisible(metronome); addAndMakeVisible(bpm); addAndMakeVisible(timeSignature); addAndMakeVisible(tap);
        tap.onClick = [this] {
            const auto now=juce::Time::getMillisecondCounterHiRes(); const auto interval=now-lastTap;
            if(lastTap>0 && interval>=200 && interval<=2000) bpm.setValue(60000/interval);
            lastTap=now;
        };
        metronome.setToggleState(owner.metronomeOn.load(),juce::dontSendNotification);
        metronome.onClick = [this] { owner.metronomeOn.store(metronome.getToggleState()); };
        bpm.setRange(30,300,1); bpm.setSliderStyle(juce::Slider::IncDecButtons); bpm.setTextBoxStyle(juce::Slider::TextBoxLeft,false,65,24);
        bpm.setValue(owner.tempo.load(),juce::dontSendNotification); bpm.setTextValueSuffix(" BPM"); bpm.onValueChange = [this] { owner.tempo.store((float)bpm.getValue()); };
        for(int i=1;i<=12;++i) timeSignature.addItem(juce::String(i)+"/4",i);
        timeSignature.setSelectedId(owner.beatsPerBar.load(),juce::dontSendNotification); timeSignature.onChange = [this] { owner.beatsPerBar.store(timeSignature.getSelectedId()); };
    }
    status.setText("Right-click a dial for MIDI learn. Drag L / M / H in the graph.",juce::dontSendNotification);
    status.setFont(uiFont(11.5f)); presetInfo.setFont(uiFont(11.5f));
    setResizable(true,true); setResizeLimits(840,675,1680,1350);
    setSize(1120,900); timerCallback(); startTimerHz(30);
}
LOWWEditor::~LOWWEditor() { stopTimer(); setLookAndFeel(nullptr); }
void LOWWEditor::refreshPresets() {
    presets.clear(juce::dontSendNotification);
    presets.addSectionHeading("LOWW originals");
    auto names = LOWWProcessor::presetNames();
    for(int i=0;i<names.size();++i) { if(i==4) presets.addSectionHeading("Rig studies / original approximations"); presets.addItem(names[i],i+1); }
    userPresets.clear();
    auto files = LOWWProcessor::presetFolder().findChildFiles(juce::File::findFiles,true,"*.loww");
    files.sort();
    if(!files.isEmpty()) presets.addSectionHeading("User library");
    for(const auto& f : files) { userPresets.add(f); presets.addItem(f.getRelativePathFrom(LOWWProcessor::presetFolder()).dropLastCharacters(5),99+userPresets.size()); }
    presets.setText(owner.presetName(),juce::dontSendNotification);
}
void LOWWEditor::stepPreset(int direction) {
    const int factoryCount = LOWWProcessor::presetNames().size();
    int index = currentFactory;
    if (currentFile != juce::File()) index = factoryCount + userPresets.indexOf(currentFile);
    const int total = factoryCount + userPresets.size();
    if (index < 0) index = direction > 0 ? -1 : 0;
    index = (index+direction+total)%total;
    presets.setSelectedId(index < factoryCount ? index+1 : 100+index-factoryCount,juce::sendNotificationSync);
}
void LOWWEditor::resized() {
    using namespace layout;
    uiScale = juce::jmin((float)getWidth()/1120.f,(float)getHeight()/900.f);
    for(auto* child : getChildren()) child->setTransform(juce::AffineTransform());
    // Header
    // Header: preset strip starts on the centre panel's column line and ends on the right margin.
    previous.setBounds(centre.getX(),16,36,36); next.setBounds(centre.getX()+40,16,36,36); presets.setBounds(centre.getX()+84,16,288,36);
    load.setBounds(680,16,68,36); overwrite.setBounds(756,16,68,36); save.setBounds(832,16,92,36); folder.setBounds(932,16,76,36);
    switches[1]->setBounds(1016,19,80,30); presetInfo.setBounds(centre.getX()+86,56,620,18);
    // Centres a row of equal-width controls inside a panel.
    const auto row=[](R panel,int y,int h,std::initializer_list<juce::Component*> items,int w=100,int gap=4) {
        int x=panel.getX()+(panel.getWidth()-(int)items.size()*w-((int)items.size()-1)*gap)/2;
        for(auto* c : items) { c->setBounds(x,y,w,h); x+=w+gap; }
    };
    row(inputs,92,120,{dials[0].get(),dials[1].get(),dials[18].get()},80,4);
    switches[0]->setBounds(dials[1]->getX()+8,214,70,22);
    row(outputs,92,120,{dials[2].get(),dials[3].get()},100,8);
    // Centre: voicing toggles, tuner, meters, transport, all inside 16 px padding.
    const int cx=centre.getX()+16, cr=centre.getRight()-16;
    switches[7]->setBounds(cx,96,160,26); switches[8]->setBounds(cx+184,96,120,26); analyser.setBounds(cx+328,96,120,26);
    tunerLabel.setBounds(cx,124,cr-cx,32);
    tuner.setBounds(cx,204,110,26); metronome.setBounds(cx+122,204,72,26);
    tap.setBounds(cr-42,204,42,26); timeSignature.setBounds(cr-122,204,72,26); bpm.setBounds(cr-262,204,130,26);
    // Bands: title row, then centred dials; the parallel/original filter dials share a slot.
    juce::Component* const low[] {dials[4].get(),dials[5].get(),dials[6].get()}, * const high[] {dials[9].get(),dials[10].get(),dials[11].get()};
    row(bands[0],bands[0].getY()+36,122,{low[0],low[1],low[2]},104,8); dials[24]->setBounds(dials[5]->getBounds());
    row(bands[1],bands[1].getY()+36,122,{dials[7].get(),dials[8].get()},104,8);
    row(bands[2],bands[2].getY()+36,122,{high[0],high[1],high[2]},104,8); dials[19]->setBounds(dials[10]->getBounds());
    for(int i=0;i<3;++i) switches[(size_t)i+2]->setBounds(bands[i].getRight()-76,bands[i].getY()+12,60,24);
    // EQ + cabinet
    switches[5]->setBounds(eq.getX()+16,eq.getCentreY()-12,64,24);
    for(int i=0;i<6;++i) dials[(size_t)i+12]->setBounds(eq.getRight()-16-58-(5-i)*72,eq.getY()+6,58,112);
    switches[6]->setBounds(cabinet.getX()+16,cabinet.getY()+12,120,24);
    // Cabinet slots
    for(size_t slot=0;slot<2;++slot) {
        const auto c=cabs[slot];
        switches[9+slot*2]->setBounds(c.getX()+16,c.getY()+12,60,24); switches[10+slot*2]->setBounds(c.getX()+88,c.getY()+12,130,24);
        loadCab[slot].setBounds(c.getX()+14,c.getY()+90,100,34); stockCab[slot].setBounds(c.getX()+120,c.getY()+90,76,34);
        dials[20+slot*2]->setBounds(c.getRight()-224,c.getY()+8,100,122); dials[21+slot*2]->setBounds(c.getRight()-116,c.getY()+8,100,122);
    }
    status.setBounds(24,866,1072,20);
    const auto transform = juce::AffineTransform::scale(uiScale);
    for(auto* child : getChildren()) {
        if (auto* corner = dynamic_cast<juce::ResizableCornerComponent*>(child)) { corner->setBounds(getWidth()-18,getHeight()-18,18,18); continue; }
        child->setTransform(transform);
    }
}
void LOWWEditor::drawMeter(juce::Graphics& g,juce::Rectangle<float> r,float gain,const juce::String& title) {
    const auto db=juce::Decibels::gainToDecibels(gain,-80.f);
    text(g,title+"  "+(db<=-79 ? "-inf" : juce::String(db,1))+" dB",r.removeFromTop(16),10.5f,muted,Weight::medium,.06f);
    inset(g,r,r.getHeight()*.5f,1.5f);
    const auto fill=r.reduced(1.5f).withWidth((r.getWidth()-3)*juce::jlimit(0.f,1.f,(db+60)/60));
    if(fill.getWidth()<1) return;
    g.setGradientFill(juce::ColourGradient(cyanDeep,fill.getTopLeft(),db>=0 ? clip : cyan,fill.getTopRight(),false));
    g.fillRoundedRectangle(fill,fill.getHeight()*.5f);
}
void LOWWEditor::paint(juce::Graphics& g) {
    using namespace layout;
    g.fillAll(background); g.addTransform(juce::AffineTransform::scale(uiScale));
    // Brand: the mark on a raised tile, wordmark beside it.
    const juce::Rectangle<float> mark {24,12,44,44};
    raised(g,mark,11,3); loww::drawLogoMark(g,mark,false);
    text(g,"LOWW",{80,11,190,30},28,ink,Weight::bold,.04f); text(g,"PARALLEL BASS",{81,39,190,16},9.5f,cyan,Weight::semibold,.2f);
    for(auto panel : {inputs,centre,outputs,eq,cabinet,cabs[0],cabs[1]}) raised(g,panel.toFloat(),16,5);
    const float meterWidth=((float)centre.getWidth()-48)*.5f, mx=(float)centre.getX()+16;
    drawMeter(g,{mx,162,meterWidth,24},owner.inputMeter.load(),"IN"); drawMeter(g,{mx+meterWidth+16,162,meterWidth,24},owner.outputMeter.load(),"OUT");
    drawGraph(g,graph);
    const juce::Colour colours[] {lowTone,midTone,highTone}; const char* titles[] {"LOW FOUNDATION","MID TEXTURE","HIGH EDGE"};
    for(int i=0;i<3;++i) {
        const auto b=bands[i].toFloat(); raised(g,b,16,5);
        g.setColour(colours[i]); g.fillEllipse(b.getX()+16,b.getY()+20,8,8);
        text(g,titles[i],{b.getX()+30,b.getY()+12,220,24},12,colours[i],Weight::bold,.1f);
    }
    const auto s=owner.settings();
    const float cx=(float)cabinet.getX()+18;
    text(g,s.parallel ? "100 Hz / 5 kHz shelves + four mid bands" : "Six bell filters / original voicing",{cx,(float)cabinet.getY()+52,480,20},13,ink,Weight::medium);
    text(g,"Compression reduction  "+juce::String(owner.compressionMeter.load(),1)+" dB",{cx,(float)cabinet.getY()+80,480,20},12,cyan,Weight::medium);
    for(size_t i=0;i<2;++i) {
        const auto c=cabs[i].toFloat();
        text(g,juce::String(i==0 ? "A  /  " : "B  /  ")+owner.cabinetName((int)i),{c.getX()+16,c.getY()+42,270,24},14.5f,ink,Weight::semibold);
        text(g,"Mid + high only",{c.getX()+16,c.getY()+64,270,18},11,muted);
    }
}
void LOWWEditor::drawGraph(juce::Graphics& g,juce::Rectangle<float> r) {
    const auto xFor=[&](float f){return r.getX()+std::log(f/20.f)/std::log(1000.f)*r.getWidth();};
    const auto s=owner.settings();
    inset(g,layout::graphWell.toFloat(),16,4);
    for(float f : {20.f,50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f,20000.f}) {
        const auto x=xFor(f); g.setColour(line); g.drawVerticalLine((int)x,r.getY(),r.getBottom()-18);
        text(g,f>=1000 ? juce::String(f/1000,0)+"k" : juce::String(f,0),{juce::jlimit(r.getX(),r.getRight()-40,x-16),r.getBottom()-18,40,18},10,muted,Weight::medium);
    }
    // Static band-filter response at the DSP's oversampled rate, including band levels.
    // Compression, saturation, cabinet and EQ are deliberately excluded from this guide.
    using Complex = std::complex<double>;
    using Coefficients = juce::dsp::IIR::ArrayCoefficients<double>;
    const double rate = owner.currentRate.load() * 4;
    const auto lowLP = Coefficients::makeLowPass(rate, s.lowCut, .7071067811865476);
    const auto lowHP = Coefficients::makeHighPass(rate, s.lowCut, .7071067811865476);
    const auto highLP = Coefficients::makeLowPass(rate, s.highCut, .7071067811865476);
    const auto highHP = Coefficients::makeHighPass(rate, s.highCut, .7071067811865476);
    const auto fixedMid = Coefficients::makeBandPass(rate, 400, .70710678);
    const auto response = [](const auto& c, Complex z) {
        return (c[0] + c[1]*z + c[2]*z*z) / (c[3] + c[4]*z + c[5]*z*z);
    };
    const double gains[] {juce::Decibels::decibelsToGain((double)s.lowLevel),
                          juce::Decibels::decibelsToGain((double)s.midLevel),
                          juce::Decibels::decibelsToGain((double)s.highLevel)};
    std::array<juce::Path, 3> curves;
    juce::Path sum;
    const auto yFor = [&](double magnitude) {
        const auto db = juce::Decibels::gainToDecibels(magnitude, -120.0);
        return juce::jlimit(r.getY()+3, r.getBottom()-20,
            r.getY()+14+(12-(float)db)/48*(r.getHeight()-48));
    };
    for (int px = 0; px <= (int)r.getWidth(); px += 2) {
        const double f = 20 * std::pow(1000.0, px / (double)r.getWidth());
        const auto z = std::polar(1.0, -juce::MathConstants<double>::twoPi*f/rate);
        const auto l = std::pow(response(lowLP,z),2), u = std::pow(response(lowHP,z),2);
        const auto m = std::pow(response(highLP,z),2), h = std::pow(response(highHP,z),2);
        const Complex bands[] {s.parallel ? l : l*(m+h),
                               s.parallel ? response(fixedMid,z) : u*m,
                               s.parallel ? h : u*h};
        Complex total {};
        const float x = r.getX()+(float)px;
        for (size_t band = 0; band < curves.size(); ++band) {
            const auto value = bands[band]*gains[band]; total += value;
            const auto y = yFor(std::abs(value));
            if (px == 0) curves[band].startNewSubPath(x,y); else curves[band].lineTo(x,y);
        }
        const auto y = yFor(std::abs(total));
        if (px == 0) sum.startNewSubPath(x,y); else sum.lineTo(x,y);
    }
    const juce::Colour curveColours[] {lowTone,midTone,highTone};
    for (size_t band = 0; band < curves.size(); ++band) {
        auto fill = curves[band];
        fill.lineTo(r.getRight(),r.getBottom()-20); fill.lineTo(r.getX(),r.getBottom()-20); fill.closeSubPath();
        g.setColour(curveColours[band].withAlpha(.1f)); g.fillPath(fill);
        g.setColour(curveColours[band].withAlpha(.9f)); g.strokePath(curves[band],juce::PathStrokeType(1.5f));
    }
    g.setColour(ink.withAlpha(.85f)); g.strokePath(sum,juce::PathStrokeType(2.f));
    text(g,"BAND FILTERS + LEVELS",{r.getX()+8,r.getY()+1,200,13},9,muted,Weight::semibold,.12f);
    if(analyser.getToggleState()) {
        juce::Path path;
        for(int px=0;px<(int)r.getWidth();++px) {
            const auto f=20.f*std::pow(1000.f,(float)px/r.getWidth());
            const auto bin=juce::jlimit(1,1023,juce::roundToInt(f*2048/owner.currentRate.load()));
            const auto db=juce::Decibels::gainToDecibels(spectrum[(size_t)bin],-90.f);
            const auto y=r.getBottom()-20-juce::jlimit(0.f,1.f,(db+90)/90)*(r.getHeight()-30);
            if(px==0) path.startNewSubPath(r.getX(),y); else path.lineTo(r.getX()+(float)px,y);
        }
        g.setColour(muted.withAlpha(.6f)); g.strokePath(path,juce::PathStrokeType(1.2f));
    }
    const float hz[] {s.lowCut,s.parallel ? 400.f : std::sqrt(s.lowCut*s.highCut),s.highCut};
    const float levels[] {s.lowLevel,s.midLevel,s.highLevel}; const juce::Colour colors[] {lowTone,midTone,highTone};
    for(int i=0;i<3;++i) {
        const float x=xFor(hz[i]), y=r.getY()+14+(12-levels[i])/48*(r.getHeight()-48);
        g.setColour(colors[i].withAlpha(.35f)); g.drawVerticalLine((int)x,r.getY(),r.getBottom()-20);
        raised(g,{x-11,y-11,22,22},11,2,colors[i]); g.setColour(onAccent); g.setFont(uiFont(12.f,Weight::bold));
        g.drawText(i==0 ? "L" : i==1 ? "M" : "H",(int)x-11,(int)y-11,22,22,juce::Justification::centred);
    }
}
void LOWWEditor::mouseDown(const juce::MouseEvent& e) {
    const auto point=e.position/uiScale; const auto r=layout::graph;
    if(!r.contains(point)) return;
    const auto s=owner.settings(); const float hz[] {s.lowCut,s.parallel ? 400.f : std::sqrt(s.lowCut*s.highCut),s.highCut};
    const float levels[] {s.lowLevel,s.midLevel,s.highLevel};
    float nearest=24;
    for(int i=0;i<3;++i) { const juce::Point<float> centre{r.getX()+std::log(hz[i]/20.f)/std::log(1000.f)*r.getWidth(),r.getY()+14+(12-levels[i])/48*(r.getHeight()-48)};
        const float distance=centre.getDistanceFrom(point); if(distance<nearest) { nearest=distance; draggedBand=i; } }
    if(draggedBand<0) return;
    draggedLevel=draggedBand==0 ? "lowLevel" : draggedBand==1 ? "midLevel" : "highLevel";
    draggedFrequency=draggedBand==0 ? (s.parallel ? "parallelLow" : "lowCut") : draggedBand==2 ? (s.parallel ? "parallelHigh" : "highCut") : "";
    owner.state.getParameter(draggedLevel)->beginChangeGesture();
    if(draggedFrequency.isNotEmpty()) owner.state.getParameter(draggedFrequency)->beginChangeGesture();
}
void LOWWEditor::mouseDrag(const juce::MouseEvent& e) {
    if(draggedBand<0) return;
    const auto point=e.position/uiScale;
    auto set=[this](const juce::String& id,float value) { auto* p=owner.state.getParameter(id); p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(p->getNormalisableRange().start,p->getNormalisableRange().end,value))); };
    const auto r=layout::graph;
    set(draggedLevel,12-(point.y-r.getY()-14)/(r.getHeight()-48)*48);
    if(draggedFrequency.isNotEmpty()) set(draggedFrequency,20*std::pow(1000.f,(point.x-r.getX())/r.getWidth()));
}
void LOWWEditor::mouseUp(const juce::MouseEvent&) {
    if(draggedBand<0) return;
    owner.state.getParameter(draggedLevel)->endChangeGesture();
    if(draggedFrequency.isNotEmpty()) owner.state.getParameter(draggedFrequency)->endChangeGesture();
    draggedBand=-1;
}
void LOWWEditor::updateTuner() {
    bool got=false; const int count=juce::jlimit(4096,16384,(int)(owner.currentRate.load()*.09));
    while(owner.readTuner(tunerSamples.data(),count)) got=true;
    if(!tuner.getToggleState() || !got) return;
    const auto reading=loww::detectPitch(tunerSamples.data(),count,owner.currentRate.load());
    if(reading.frequency<=0) { tunerLabel.setText("Play a single note",juce::dontSendNotification); return; }
    static const char* notes[] {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    tunerLabel.setText(juce::String(notes[(reading.midi%12+12)%12])+juce::String(reading.midi/12-1)+"   "+juce::String(reading.cents,1)+" cents   "+juce::String(reading.frequency,1)+" Hz",juce::dontSendNotification);
}
void LOWWEditor::timerCallback() {
    bool got=false; while(owner.readSpectrum(fftData.data(),2048)) got=true;
    if(got) {
        std::fill(fftData.begin()+2048,fftData.end(),0); window.multiplyWithWindowingTable(fftData.data(),2048); fft.performFrequencyOnlyForwardTransform(fftData.data());
        for(size_t i=0;i<spectrum.size();++i) spectrum[i]=juce::jmax(fftData[i]/512.f,spectrum[i]*.85f);
    } else for(auto& bin:spectrum) bin*=.9f;
    if(++tunerTicks%3==0) updateTuner();
    forgetFileAfterHostRestore();
    const auto s=owner.settings(); dials[5]->setVisible(!s.parallel); dials[24]->setVisible(s.parallel); dials[10]->setVisible(!s.parallel); dials[19]->setVisible(s.parallel);
    dials[12]->label.setText(s.parallel ? "100 LS" : "80",juce::dontSendNotification);
    dials[17]->label.setText(s.parallel ? "5k HS" : "5k",juce::dontSendNotification);
    presets.setText(owner.presetName()+(owner.presetDirty.load() ? " *" : ""),juce::dontSendNotification);
    const auto pitchInfo = std::abs(s.transpose) > .001f ? "  /  transpose latency " + juce::String(1000.0*owner.getLatencySamples()/owner.currentRate.load(),1) + " ms" : juce::String();
    presetInfo.setText(owner.presetInfo()+pitchInfo,juce::dontSendNotification);
    if(owner.midiLearn.load()>=0) status.setText("MIDI learn: move a controller. Right-click a dial and Clear to cancel.",juce::dontSendNotification);
    repaint();
}
void LOWWEditor::showResult(const juce::Result& result) { status.setText(result.wasOk() ? "Cabinet updated" : result.getErrorMessage(),juce::dontSendNotification); }
void LOWWEditor::chooseIR(int slot) {
    chooser=std::make_unique<juce::FileChooser>("Choose cabinet IR",juce::File{},"*.wav;*.aif;*.aiff");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<LOWWEditor>(this),slot](const juce::FileChooser& fc) {
        if(safe && fc.getResult().existsAsFile()) safe->showResult(safe->owner.loadIRFile(fc.getResult(),slot));
    });
}
void LOWWEditor::forgetFileAfterHostRestore() {
    // A host recall replaces the tone, so Save must not write it over the previously opened file.
    const int restores=owner.hostRestoreCount.load();
    if(restores==seenHostRestores) return;
    seenHostRestores=restores; currentFile=juce::File(); currentFactory=-1;
}
void LOWWEditor::savePresetAs(const juce::File& file) {
    const auto result=owner.savePreset(file);
    if(result.wasOk()) { currentFile=file; currentFactory=-1; refreshPresets(); }
    status.setText(result.wasOk() ? "Preset saved" : result.getErrorMessage(),juce::dontSendNotification);
}
void LOWWEditor::presetFile(bool saving) {
    const auto dir=LOWWProcessor::presetFolder();
    if(saving) { const auto result=dir.createDirectory(); if(result.failed()) { status.setText(result.getErrorMessage(),juce::dontSendNotification); return; } }
    chooser=std::make_unique<juce::FileChooser>(saving ? "Save LOWW preset" : "Open LOWW preset",saving ? dir.getChildFile(juce::File::createLegalFileName(owner.presetName())+".loww") : dir,saving ? "*.loww" : "*.loww;*.trifold");
    chooser->launchAsync((saving ? juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,
        [safe=juce::Component::SafePointer<LOWWEditor>(this),saving](const juce::FileChooser& fc) {
            if(!safe || fc.getResult()==juce::File()) return;
            if(!saving) {
                const auto file=fc.getResult(); const auto result=safe->owner.openPreset(file);
                if(result.wasOk()) { safe->currentFile=file; safe->currentFactory=-1; safe->refreshPresets(); }
                safe->status.setText(result.wasOk() ? "Preset opened" : result.getErrorMessage(),juce::dontSendNotification);
                return;
            }
            const auto file=fc.getResult().withFileExtension("loww");
            // The chooser only confirmed the name as typed; confirm again if forcing .loww hits an existing file.
            if(file==fc.getResult() || !file.exists()) { safe->savePresetAs(file); return; }
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,"Replace preset?",file.getFileName()+" already exists. Replace it?","Replace","Cancel",safe.getComponent(),
                juce::ModalCallbackFunction::create([safe,file](int replace) { if(safe && replace==1) safe->savePresetAs(file); }));
        });
}
