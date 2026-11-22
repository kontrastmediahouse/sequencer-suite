#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MachineProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    s420::addGlobalParams (layout);
    snake::ids::addParams (layout);
    tgb::ids::addParams (layout);
    return layout;
}

static const s420::GlobalState& attachGlobal (s420::GlobalState& g, juce::AudioProcessorValueTreeState& s) { g.attach (s); return g; }

MachineProcessor::MachineProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SNAKE420State", createLayout()),
      snake (apvts, attachGlobal (global, apvts)),
      tgb (apvts, global, sampleRateValue)
{
    trig.reserve (256);
}

bool MachineProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void MachineProcessor::prepareToPlay (double sr, int)
{
    sampleRateValue = sr;
    snake.prepare (sr);
    tgb.prepare (sr);
}

void MachineProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    audio.clear();
    const int n = audio.getNumSamples();

    bool playing = false; double ppq = 0, bpm = 120;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) ppq = *q; else playing = false;
            if (auto b = pos->getBpm()) bpm = *b;
        }

    // MIDI in: TGB-420 keys (Bass / Arp / Learn / Thru), Snake's remote, or both
    const int inRoute = (int) apvts.getRawParameterValue (s420::ids::midiIn)->load();
    static const juce::MidiBuffer empty;
    const juce::MidiBuffer& tgbIn   = (inRoute == 0 || inRoute == 2) ? midi : empty;
    const juce::MidiBuffer& snakeIn = (inRoute == 1 || inRoute == 2) ? midi : empty;
    snake.handleMidiInput (snakeIn);

    const bool snakeOut = apvts.getRawParameterValue (s420::ids::snakeOut)->load() > 0.5f;
    const bool tgbOut   = apvts.getRawParameterValue (s420::ids::tgbOut)->load() > 0.5f;
    const int link = global.linkV();

    juce::MidiBuffer out;
    if (link != lastLink)
    {
        // switching the routing: close everything and let Snake start clean in its new mode
        if (lastLink >= 0) out.addEvent (juce::MidiMessage::allNotesOff (global.channelV()), 0);
        snake.engine.resetState();
        lastLink = link;
    }

    if (link == s420::LinkTgbClockToSnake)
    {
        // TGB-420's notes become Snake's clock; TGB's own notes are not sent (its Aux still is)
        tgb.process (tgbIn, playing, ppq, bpm, n, out, false, nullptr);
        trig.clear();
        for (const auto& t : tgb.triggers()) trig.push_back ({ t.ppq, t.durQ, t.vel });
        snake.processTriggers (trig, playing, ppq, bpm, n, out, snakeOut);
    }
    else
    {
        snake.process ({ playing, ppq, bpm }, n, out, snakeOut);   // first: its pitch log feeds TGB
        tgb.process (tgbIn, playing, ppq, bpm, n, out, tgbOut,
                     link == s420::LinkSnakePitchToTgb ? &snake.engine : nullptr);
    }

    for (const auto m : out)
        if (m.getMessage().isNoteOn()) { lastPitch = m.getMessage().getNoteNumber(); ++noteCounter; }
    midi.swapWith (out);
}

void MachineProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    snake.saveState (state);
    tgb.saveState (state);
    state.setProperty ("selectedModule", selectedModule, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void MachineProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            snake.loadState (tree);
            tgb.loadState (tree);
            selectedModule = juce::jlimit (0, 1, (int) tree.getProperty ("selectedModule", 1));
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* MachineProcessor::createEditor() { return new MachineEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MachineProcessor(); }
