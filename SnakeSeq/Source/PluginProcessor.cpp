#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace snake;

SnakeProcessor::SnakeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SnakeState", ids::createLayout())
{
    for (int l = 0; l < NumLanes; ++l)
    {
        auto& r = laneRefs[(size_t) l];
        r.dir   = apvts.getRawParameterValue (ids::laneParam (l, "dir"));
        r.speed = apvts.getRawParameterValue (ids::laneParam (l, "speed"));
        r.len   = apvts.getRawParameterValue (ids::laneParam (l, "len"));
        r.shift = apvts.getRawParameterValue (ids::laneParam (l, "shift"));
        r.shape = apvts.getRawParameterValue (ids::laneParam (l, "shape"));
        for (int s = 0; s < kNumSteps; ++s)
            r.steps[(size_t) s] = apvts.getRawParameterValue (ids::stepParam (l, s));
    }
    pRoot      = apvts.getRawParameterValue (ids::root);
    pScale     = apvts.getRawParameterValue (ids::scale);
    pSwing     = apvts.getRawParameterValue (ids::swing);
    pMidiCh    = apvts.getRawParameterValue (ids::midiCh);
    pMidiChord = apvts.getRawParameterValue (ids::midiChord);
    for (int k = 0; k < 12; ++k) pUserKeys[(size_t) k] = apvts.getRawParameterValue (ids::userKey (k));
    for (int c = 0; c < 3; ++c)
    {
        pCcOn[(size_t) c]  = apvts.getRawParameterValue (ids::ccParam (c, "on"));
        pCcNum[(size_t) c] = apvts.getRawParameterValue (ids::ccParam (c, "num"));
        pCcMin[(size_t) c] = apvts.getRawParameterValue (ids::ccParam (c, "min"));
        pCcMax[(size_t) c] = apvts.getRawParameterValue (ids::ccParam (c, "max"));
    }
    for (auto& c : uiCell) c = -1;
    startTimerHz (30);
}

SnakeProcessor::~SnakeProcessor() { stopTimer(); }

bool SnakeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SnakeProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

void SnakeProcessor::fillParams (EngineParams& p) const
{
    for (int l = 0; l < NumLanes; ++l)
    {
        const auto& r = laneRefs[(size_t) l];
        auto& ls = p.lanes[(size_t) l];
        ls.direction = (int) r.dir->load();
        ls.speed     = (int) r.speed->load();
        ls.length    = (int) r.len->load();
        ls.shift     = r.shift->load();
        ls.shape     = (int) r.shape->load();
    }
    for (int s = 0; s < kNumSteps; ++s)
    {
        p.notes[(size_t) s]      = laneRefs[Notes].steps[(size_t) s]->load();
        p.gates[(size_t) s]      = laneRefs[Gates].steps[(size_t) s]->load() > 0.5f;
        p.velocities[(size_t) s] = (int) laneRefs[Velocities].steps[(size_t) s]->load();
        for (int c = 0; c < 3; ++c)
            p.custom[(size_t) c][(size_t) s] = (int) laneRefs[(size_t) (Custom1 + c)].steps[(size_t) s]->load();
    }
    p.root        = (int) pRoot->load();
    p.scaleIndex  = (int) pScale->load();
    p.swing       = pSwing->load();
    p.midiChannel = (int) pMidiCh->load() + 1;

    if (pMidiChord->load() > 0.5f)
        p.userMask = midiChordMask.load();
    else
    {
        uint16_t m = 0;
        for (int k = 0; k < 12; ++k) if (pUserKeys[(size_t) k]->load() > 0.5f) m |= (uint16_t) (1u << k);
        p.userMask = m;
    }
    for (int c = 0; c < 3; ++c)
    {
        p.ccEnabled[(size_t) c] = pCcOn[(size_t) c]->load() > 0.5f;
        p.ccNumber[(size_t) c]  = (int) pCcNum[(size_t) c]->load();
        p.ccMin[(size_t) c]     = (int) pCcMin[(size_t) c]->load();
        p.ccMax[(size_t) c]     = (int) pCcMax[(size_t) c]->load();
    }
}

// Incoming MIDI (the device's hidden performance features):
//   C4..B4 (72-83) -> Root note
//   C2 (48)        -> Random All          D2 (50) -> Random Notes lane
//   C3..B3 (60-71) -> latch a chord as the User scale (when "MIDI" is on in the scale panel)
void SnakeProcessor::handleMidiInput (const juce::MidiBuffer& midi)
{
    const bool chordMode = pMidiChord->load() > 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (n >= 72 && n <= 83) pendingRoot = n - 72;
            if (n == 48) pendingRandomAll = true;
            if (n == 50) pendingRandomNotes = true;
            if (chordMode && n >= 60 && n <= 71)
            {
                uint16_t mask = newChordArmed ? 0 : midiChordMask.load();
                newChordArmed = false;
                mask |= (uint16_t) (1u << (n - 60));
                midiChordMask = mask;
                ++heldChordNotes;
            }
        }
        else if (m.isNoteOff() && chordMode)
        {
            const int n = m.getNoteNumber();
            if (n >= 60 && n <= 71 && heldChordNotes > 0 && --heldChordNotes == 0)
                newChordArmed = true;
        }
    }
}

void SnakeProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    handleMidiInput (midi);
    midi.clear(); // the device does not pass input notes through

    EngineParams p;
    fillParams (p);

    Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.isPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) t.ppq = *ppq; else t.isPlaying = false;
            if (auto bpm = pos->getBpm())         t.bpm = *bpm;
        }

    engine.process (p, t, audio.getNumSamples(), [&] (const MidiEvent& e)
    {
        midi.addEvent (juce::MidiMessage (e.status, e.data1, e.data2), e.sampleOffset);
        if ((e.status & 0xF0) == 0x90) { uiPitch = e.data1; ++uiNoteCounter; }
    });

    for (int l = 0; l < NumLanes; ++l)
        uiCell[(size_t) l] = engine.currentCell[(size_t) l];
}

// ---------------------------------------------------------------------------
void SnakeProcessor::timerCallback()
{
    const int r = pendingRoot.exchange (-1);
    if (r >= 0) setParam (ids::root, (float) r);
    if (pendingRandomAll.exchange (false))   randomizeAll();
    if (pendingRandomNotes.exchange (false)) randomizeLane (Notes);
}

void SnakeProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
        prm->endChangeGesture();
    }
}

void SnakeProcessor::randomizeLane (int lane)
{
    auto& rnd = juce::Random::getSystemRandom();
    if (lane == Gates)
    {
        for (int s = 0; s < kNumSteps; ++s) setParam (ids::stepParam (lane, s), (float) rnd.nextInt (2)); // device: [random 2]
        return;
    }
    float lo = apvts.getRawParameterValue (ids::laneParam (lane, "rmin"))->load();
    float hi = apvts.getRawParameterValue (ids::laneParam (lane, "rmax"))->load();
    if (hi < lo) std::swap (lo, hi);
    for (int s = 0; s < kNumSteps; ++s)
    {
        // device: [random 127] -> [scale 0 127 min max]
        const float v = lo + (float) rnd.nextInt (127) / 127.0f * (hi - lo);
        setParam (ids::stepParam (lane, s), lane == Notes ? v : std::round (v));
    }
}

void SnakeProcessor::resetLane (int lane)
{
    for (int s = 0; s < kNumSteps; ++s)
    {
        float v = 0.0f;
        if (lane == Notes)           v = ids::noteDefaults[s];
        else if (lane == Gates)      v = 1.0f;
        else if (lane == Velocities) v = 64.0f;
        setParam (ids::stepParam (lane, s), v);
    }
}

int SnakeProcessor::quantizedNoteForStep (int step) const
{
    return quantizeValue (laneRefs[Notes].steps[(size_t) step]->load());
}

int SnakeProcessor::quantizeValue (float dialValue) const
{
    EngineParams p;
    fillParams (p);
    int pcs[12];
    int n;
    if (p.scaleIndex >= kUserScale) n = pcsFromMask (p.userMask, pcs);
    else
    {
        const auto& s = scales()[(size_t) p.scaleIndex].pcs;
        std::copy (s.begin(), s.end(), pcs);
        n = (int) s.size();
    }
    return quantizeNote (dialValue, pcs, n, p.root);
}

// ---------------------------------------------------------------------------
void SnakeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("midiChordMask", (int) midiChordMask.load(), nullptr);
    state.setProperty ("selectedLane", selectedLane, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SnakeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            midiChordMask = (uint16_t) (int) tree.getProperty ("midiChordMask", 1);
            selectedLane  = juce::jlimit (0, NumLanes - 1, (int) tree.getProperty ("selectedLane", 0));
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* SnakeProcessor::createEditor() { return new SnakeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SnakeProcessor(); }
