#include "SnakeModule.h"

using namespace snake;

SnakeModule::SnakeModule (juce::AudioProcessorValueTreeState& s, const s420::GlobalState& g) : apvts (s), global (g)
{
    for (int l = 0; l < NumLanes; ++l)
    {
        auto& r = laneRefs[(size_t) l];
        r.dir   = apvts.getRawParameterValue (ids::laneParam (l, "dir"));
        r.speed = apvts.getRawParameterValue (ids::laneParam (l, "speed"));
        r.len   = apvts.getRawParameterValue (ids::laneParam (l, "len"));
        r.shift = apvts.getRawParameterValue (ids::laneParam (l, "shift"));
        r.shape = apvts.getRawParameterValue (ids::laneParam (l, "shape"));
        for (int st = 0; st < kNumSteps; ++st) r.steps[(size_t) st] = apvts.getRawParameterValue (ids::stepParam (l, st));
    }
    pRoot = apvts.getRawParameterValue (ids::root);
    pScale = apvts.getRawParameterValue (ids::scale);
    pSwing = apvts.getRawParameterValue (ids::swing);
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

SnakeModule::~SnakeModule() { stopTimer(); }

void SnakeModule::fillParams (EngineParams& p) const
{
    for (int l = 0; l < NumLanes; ++l)
    {
        const auto& r = laneRefs[(size_t) l];
        auto& ls = p.lanes[(size_t) l];
        ls.direction = (int) r.dir->load(); ls.speed = (int) r.speed->load(); ls.length = (int) r.len->load();
        ls.shift = r.shift->load(); ls.shape = (int) r.shape->load();
    }
    for (int s = 0; s < kNumSteps; ++s)
    {
        p.notes[(size_t) s]      = laneRefs[Notes].steps[(size_t) s]->load();
        p.gates[(size_t) s]      = laneRefs[Gates].steps[(size_t) s]->load() > 0.5f;
        p.velocities[(size_t) s] = (int) laneRefs[Velocities].steps[(size_t) s]->load();
        for (int c = 0; c < 3; ++c)
            p.custom[(size_t) c][(size_t) s] = (int) laneRefs[(size_t) (Custom1 + c)].steps[(size_t) s]->load();
    }
    p.swing = pSwing->load();
    p.midiChannel = global.channelV();
    if (global.snakeGlobal())
    {
        p.root = global.rootV();
        int pcs[12];
        p.numPcsOverride = s420::globalScalePcs (global.scaleV(), pcs);
        for (int i = 0; i < p.numPcsOverride; ++i) p.pcsOverride[(size_t) i] = pcs[i];
    }
    else
    {
        p.root = (int) pRoot->load();
        p.scaleIndex = (int) pScale->load();
    }
    if (pMidiChord->load() > 0.5f) p.userMask = midiChordMask.load();
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

// Snake's MIDI remote: C4-B4 root, C2 random all, D2 random notes, C3-B3 chord -> user scale
void SnakeModule::handleMidiInput (const juce::MidiBuffer& midi)
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
            if (n >= 60 && n <= 71 && heldChordNotes > 0 && --heldChordNotes == 0) newChordArmed = true;
        }
    }
}

template <typename Fn>
void SnakeModule::emitTo (juce::MidiBuffer& out, bool sendNotes, Fn&& run)
{
    run ([&] (const MidiEvent& e)
    {
        const int st = e.status & 0xF0;
        if (st == 0x90) { uiPitch = e.data1; ++uiNoteCounter; }
        if (! sendNotes && (st == 0x90 || st == 0x80)) return;
        out.addEvent (juce::MidiMessage (e.status, e.data1, e.data2), e.sampleOffset);
    });
    for (int l = 0; l < NumLanes; ++l) uiCell[(size_t) l] = engine.currentCell[(size_t) l];
}

void SnakeModule::process (const Transport& t, int numSamples, juce::MidiBuffer& out, bool sendNotes)
{
    EngineParams p; fillParams (p);
    emitTo (out, sendNotes, [&] (auto&& emit) { engine.process (p, t, numSamples, emit); });
}

void SnakeModule::processTriggers (const std::vector<Engine::Trigger>& trig, bool playing, double ppq, double bpm,
                                   int numSamples, juce::MidiBuffer& out, bool sendNotes)
{
    EngineParams p; fillParams (p);
    emitTo (out, sendNotes, [&] (auto&& emit) { engine.processTriggers (p, trig, playing, ppq, bpm, numSamples, emit); });
}

// ---------------------------------------------------------------------------
void SnakeModule::timerCallback()
{
    const int r = pendingRoot.exchange (-1);
    if (r >= 0) setParam (global.snakeGlobal() ? s420::ids::root : ids::root, (float) r);
    if (pendingRandomAll.exchange (false))   randomizeAll();
    if (pendingRandomNotes.exchange (false)) randomizeLane (Notes);
}

void SnakeModule::setParam (const juce::String& id, float plainValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
        prm->endChangeGesture();
    }
}

void SnakeModule::randomizeLane (int lane)
{
    auto& rnd = juce::Random::getSystemRandom();
    if (lane == Gates)
    {
        for (int s = 0; s < kNumSteps; ++s) setParam (ids::stepParam (lane, s), (float) rnd.nextInt (2));
        return;
    }
    float lo = apvts.getRawParameterValue (ids::laneParam (lane, "rmin"))->load();
    float hi = apvts.getRawParameterValue (ids::laneParam (lane, "rmax"))->load();
    if (hi < lo) std::swap (lo, hi);
    for (int s = 0; s < kNumSteps; ++s)
    {
        const float v = lo + (float) rnd.nextInt (127) / 127.0f * (hi - lo);
        setParam (ids::stepParam (lane, s), lane == Notes ? v : std::round (v));
    }
}

void SnakeModule::resetLane (int lane)
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

int SnakeModule::quantizedNoteForStep (int step) const { return quantizeValue (laneRefs[Notes].steps[(size_t) step]->load()); }

int SnakeModule::quantizeValue (float dialValue) const
{
    EngineParams p; fillParams (p);
    int pcs[12], n;
    if (p.numPcsOverride > 0) { n = p.numPcsOverride; for (int i = 0; i < n; ++i) pcs[i] = p.pcsOverride[(size_t) i]; }
    else if (p.scaleIndex >= kUserScale) n = pcsFromMask (p.userMask, pcs);
    else { const auto& s = scales()[(size_t) p.scaleIndex].pcs; std::copy (s.begin(), s.end(), pcs); n = (int) s.size(); }
    return quantizeNote (dialValue, pcs, n, p.root);
}

void SnakeModule::saveState (juce::ValueTree& t) const
{
    t.setProperty ("snakeMidiChordMask", (int) midiChordMask.load(), nullptr);
    t.setProperty ("snakeSelectedLane", selectedLane, nullptr);
}

void SnakeModule::loadState (const juce::ValueTree& t)
{
    midiChordMask = (uint16_t) (int) t.getProperty ("snakeMidiChordMask", 1);
    selectedLane = juce::jlimit (0, NumLanes - 1, (int) t.getProperty ("snakeSelectedLane", 0));
}
