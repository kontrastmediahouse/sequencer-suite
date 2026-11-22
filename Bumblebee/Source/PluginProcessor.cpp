#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace bee;

BeeProcessor::BeeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BumblebeeState", ids::createLayout())
{
    for (int k = 0; k < ids::kNumCellKeys; ++k)
        for (int c = 0; c < NS; ++c) cellP[(size_t) k][(size_t) c] = apvts.getRawParameterValue (ids::cell (ids::cellKey (k), c));
    for (auto& x : uiLayerCellA) x = -1;
    startTimerHz (30);
}

BeeProcessor::~BeeProcessor() { stopTimer(); }

bool BeeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void BeeProcessor::prepareToPlay (double sr, int) { engine.prepare (sr); }

int BeeProcessor::cellVal (const char* key, int c) const
{
    for (int k = 0; k < ids::kNumCellKeys; ++k)
        if (std::strcmp (ids::cellKey (k), key) == 0) return (int) std::lround (cellP[(size_t) k][(size_t) c]->load());
    return 0;
}

Snapshot BeeProcessor::readSnapshot() const
{
    Snapshot s;
    std::array<int, NS>* dst[] = { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.mode, &s.div, &s.len, &s.mod[0], &s.mod[1], &s.mod[2], &s.mod[3] };
    for (int k = 0; k < ids::kNumCellKeys; ++k)
        for (int c = 0; c < NS; ++c) (*dst[k])[(size_t) c] = (int) std::lround (cellP[(size_t) k][(size_t) c]->load());
    s.length = param ("length"); s.direction = param ("direction"); s.transpose = param ("transpose");
    s.path = param ("path"); s.shift = param ("shift");
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& c = s.layers[(size_t) l];
        const juce::String k = "L" + juce::String (ids::layerKey (l)) + "_";
        c.steps = param (k + "steps"); c.path = param (k + "path") - 1; c.shift = param (k + "shift");
        c.dir = param (k + "dir") - 1; c.rate = param (k + "rate");
    }
    return s;
}

void BeeProcessor::fillParams (Params& p) const
{
    p.s = overrideActive.load (std::memory_order_acquire) ? pendingSlotData : readSnapshot();
    p.playmode = param ("playmode"); p.clock = param ("clock"); p.swing = param ("swing"); p.bassnote = param ("bassnote");
    p.retrig = param ("retrig"); p.arporder = param ("arporder"); p.arpoct = param ("arpoct"); p.latch = param ("latch");
    p.countby = param ("countby"); p.pulses = param ("pulses");
    p.scale = param ("scale"); p.root = param ("root"); p.pitchMode = param ("pitchmode"); p.chroll = param ("chroll");
    if (param ("chord")) p.userMask = chordMaskA.load();
    else { uint16_t m = 0; for (int k = 0; k < 12; ++k) if (param ("user" + juce::String (k))) m |= (uint16_t) (1u << k); p.userMask = m; }
    for (int m = 0; m < kNumMods; ++m)
    {
        const juce::String k = juce::String ("mod") + juce::String::charToString ((juce::juce_wchar) ('a' + m)) + "_";
        auto& mo = p.mods[(size_t) m];
        mo.on = param (k + "on") > 0; mo.kind = param (k + "kind"); mo.cc = param (k + "cc");
        mo.lo = param (k + "lo"); mo.hi = param (k + "hi"); mo.glide = param (k + "glide");
    }
    p.resetstop = param ("resetstop"); p.evolve = param ("evolve"); p.midiChannel = param ("midich") + 1;
}

// Snake's MIDI remote: C4-B4 root, C2 random all, D2 random pitch, C3-B3 chord -> user scale
void BeeProcessor::handleRemote (const juce::MidiMessage& m)
{
    const bool chord = apvts.getRawParameterValue (ids::id ("chord"))->load() > 0.5f;
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        if (n >= 72 && n <= 83) remoteRoot = n - 72;
        if (n == 48) remoteRandomAll = true;
        if (n == 50) remoteRandomPitch = true;
        if (chord && n >= 60 && n <= 71)
        {
            uint16_t mask = newChordArmed ? 0 : chordMaskA.load();
            newChordArmed = false;
            chordMaskA = (uint16_t) (mask | (1u << (n - 60)));
            ++heldChord;
        }
    }
    else if (m.isNoteOff() && chord)
    {
        const int n = m.getNoteNumber();
        if (n >= 60 && n <= 71 && heldChord > 0 && --heldChord == 0) newChordArmed = true;
    }
}

void BeeProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    audio.clear();
    Params p; fillParams (p);
    if (p.playmode != lastPlaymode) { engine.playModeChanged(); lastPlaymode = p.playmode; }
    if (p.latch != lastLatch) { if (! p.latch) engine.latchOff(); lastLatch = p.latch; }
    if (learnArmChanged.exchange (false)) engine.armLearn (learnArm);

    const bool remote = param ("midiin") == 1, thru = param ("thru") > 0;
    juce::MidiBuffer out;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOnOrOff())
        {
            if (remote) handleRemote (m);
            else
            {
                const int n = m.getNoteNumber();
                engine.noteIn (n, m.isNoteOn() ? m.getVelocity() : 0, p);
                auto& mask = heldMask[(size_t) (n / 64)];
                const uint64_t bit = 1ull << (n % 64);
                if (m.isNoteOn()) mask.fetch_or (bit); else mask.fetch_and (~bit);
            }
        }
        else if (m.isController()) engine.ccIn (m.getControllerNumber());
        if (thru && p.playmode == Seq && ! remote) out.addEvent (m, meta.samplePosition);
    }

    bool playing = false; double ppq = 0, bpm = 120;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) ppq = *q; else playing = false;
            if (auto b = pos->getBpm()) bpm = *b;
        }
    playingA = playing;

    if (pendingSlotSet.load (std::memory_order_acquire) && ! overrideActive.load()) engine.setPendingSlot (&pendingSlotData);
    else engine.setPendingSlot (nullptr);

    engine.process (p, playing, ppq, bpm, audio.getNumSamples(), [&] (const MidiOut& e)
    {
        if ((e.b0 & 0xF0) == 0x90) uiLastPitch = e.b1;
        if (e.size == 2) out.addEvent (juce::MidiMessage (e.b0, e.b1), e.sampleOffset);
        else             out.addEvent (juce::MidiMessage (e.b0, e.b1, e.b2), e.sampleOffset);
    });
    midi.swapWith (out);

    if (engine.slotFired) { overrideActive.store (true, std::memory_order_release); pendingSlotSet = false; slotFiredA = true; }
    if (engine.evolveRequested) evolveA = true;
    if (engine.learnedCC) { engine.learnedCC = false; learnedCC = engine.learnedCCNumber; }
    uiCellA = engine.uiCell; uiPulseA = engine.uiPulse;
    for (int l = 0; l < kNumLayers; ++l) uiLayerCellA[(size_t) l] = engine.uiLayerCell[(size_t) l];
    uiHitCellA = engine.uiHitCell; uiHitCount = engine.hitCounter;
}

// ---------------------------------------------------------------------------
void BeeProcessor::timerCallback()
{
    if (slotFiredA.exchange (false))
    {
        writeSnapshot (pendingSlotData);
        overrideActive.store (false, std::memory_order_release);
        activeSlot = pendingSlot; pendingSlot = -1;
        setStatus ("Slot " + juce::String (activeSlot + 1) + " playing");
    }
    if (evolveA.exchange (false))
    {
        pushHistory();
        auto s = readSnapshot();
        const int cell = editor.evolve (editContext(), s);
        writeSnapshot (s);
        activeSlot = -1;
        setStatus ("Evolved stage " + juce::String (cell + 1));
    }
    const int cc = learnedCC.exchange (-1);
    if (cc >= 0)
    {
        const int l = param ("layer"), m = l >= LModA ? l - LModA : 0;
        setP (ids::mod (m, "kind"), 0); setP (ids::mod (m, "cc"), (float) cc); setP (ids::mod (m, "on"), 1);
        learnArm = false;
        setStatus ("MOD " + juce::String::charToString ((juce::juce_wchar) ('A' + m)) + " learned CC " + juce::String (cc));
    }
    const int r = remoteRoot.exchange (-1);
    if (r >= 0) { setP (ids::id ("root"), (float) r); setStatus ("Root " + ids::noteNames()[r]); }
    if (remoteRandomAll.exchange (false))
    {
        pushHistory(); auto s = readSnapshot(); auto c = editContext(); c.randtarget = 2; editor.randomize (c, s); writeSnapshot (s);
        setStatus ("Remote: rolled all");
    }
    if (remoteRandomPitch.exchange (false)) randomizeLayer (LPitch);
}

void BeeProcessor::setP (const juce::String& fullId, float plain)
{
    if (auto* prm = apvts.getParameter (fullId))
    {
        const float v = prm->convertTo0to1 (plain);
        if (std::abs (prm->getValue() - v) < 1.0e-6f) return;
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (v);
        prm->endChangeGesture();
    }
}

void BeeProcessor::writeSnapshot (const Snapshot& s)
{
    const std::array<int, NS>* src[] = { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.mode, &s.div, &s.len, &s.mod[0], &s.mod[1], &s.mod[2], &s.mod[3] };
    for (int k = 0; k < ids::kNumCellKeys; ++k)
        for (int c = 0; c < NS; ++c) setP (ids::cell (ids::cellKey (k), c), (float) (*src[k])[(size_t) c]);
    setP (ids::id ("length"), (float) s.length); setP (ids::id ("direction"), (float) s.direction);
    setP (ids::id ("transpose"), (float) s.transpose); setP (ids::id ("path"), (float) s.path); setP (ids::id ("shift"), (float) s.shift);
    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto& c = s.layers[(size_t) l];
        setP (ids::layer (l, "steps"), (float) c.steps); setP (ids::layer (l, "path"), (float) (c.path + 1));
        setP (ids::layer (l, "shift"), (float) c.shift); setP (ids::layer (l, "dir"), (float) (c.dir + 1));
        setP (ids::layer (l, "rate"), (float) c.rate);
    }
}

EditContext BeeProcessor::editContext() const
{
    EditContext c;
    c.layer = param ("layer"); c.playmode = param ("playmode"); c.randtarget = param ("randtarget"); c.pool = param ("pool");
    c.notes = param ("notes"); c.octjump = param ("octjump"); c.evotarget = param ("evotarget");
    c.scale = param ("scale"); c.root = param ("root"); c.pitchMode = param ("pitchmode");
    c.userMask = currentParams().userMask;
    for (int i = 0; i < 12; ++i) c.custom[(size_t) i] = param ("cust" + juce::String (i));
    for (int l = 0; l < kNumLayers; ++l)
    {
        const juce::String k = "L" + juce::String (ids::layerKey (l)) + "_";
        c.rlo[(size_t) l] = param (k + "rlo"); c.rhi[(size_t) l] = param (k + "rhi");
    }
    return c;
}

// ---------------------------------------------------------------------------
void BeeProcessor::pushHistory()
{
    Hist h; h.seq = readSnapshot();
    hist.push_back (h);
    if (hist.size() > 16) hist.erase (hist.begin());
    future.clear();
}

void BeeProcessor::randomize()
{
    pushHistory();
    auto s = readSnapshot();
    const auto c = editContext();
    editor.randomize (c, s);
    writeSnapshot (s);
    activeSlot = -1;
    static const char* what[] = { "Rolled layer", "Rolled rhythm", "Rolled all dials", "Rolled paths" };
    setStatus (c.randtarget == 0 ? "Rolled " + ids::layerName (c.layer).toLowerCase() : juce::String (what[c.randtarget]));
    const double now = juce::Time::getMillisecondCounterHiRes();
    rollTimes.push_back (now);
    rollTimes.erase (std::remove_if (rollTimes.begin(), rollTimes.end(), [now] (double t) { return now - t > 5000; }), rollTimes.end());
}

void BeeProcessor::randomizeLayer (int l)
{
    pushHistory();
    auto s = readSnapshot(); auto c = editContext(); c.layer = l; c.randtarget = 0;
    editor.randomize (c, s);
    writeSnapshot (s);
    setStatus ("Rolled " + ids::layerName (l).toLowerCase());
}

void BeeProcessor::resetLayer (int l)
{
    pushHistory();
    auto s = readSnapshot();
    Editor::resetLayer (s, l);
    writeSnapshot (s);
    setStatus ("Reset " + ids::layerName (l).toLowerCase());
}

int BeeProcessor::rollsInWindow() const
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    int n = 0; for (double t : rollTimes) if (now - t <= 5000) ++n;
    return n;
}

void BeeProcessor::goBack()
{
    if (hist.empty()) { setStatus ("Nothing to undo"); return; }
    Hist e = hist.back(); hist.pop_back();
    if (e.isSlot) { slots[(size_t) e.slotN] = e.before; future.push_back (e); setStatus ("Slot " + juce::String (e.slotN + 1) + " restored"); return; }
    Hist f; f.seq = readSnapshot(); future.push_back (f);
    writeSnapshot (e.seq);
    setStatus ("Back - " + juce::String ((int) hist.size()) + " left");
}

void BeeProcessor::goForward()
{
    if (future.empty()) { setStatus ("Nothing to redo"); return; }
    Hist e = future.back(); future.pop_back();
    if (e.isSlot) { slots[(size_t) e.slotN] = e.after; hist.push_back (e); setStatus ("Forward"); return; }
    Hist h; h.seq = readSnapshot(); hist.push_back (h);
    writeSnapshot (e.seq);
    setStatus ("Forward");
}

void BeeProcessor::nudge (int d)
{
    auto s = readSnapshot();
    if (s.length < 2) return;
    pushHistory();
    Editor::nudge (s, d);
    writeSnapshot (s);
    activeSlot = -1;
    setStatus (d > 0 ? "Nudged along the path" : "Nudged back along the path");
}

void BeeProcessor::seqReset()
{
    pushHistory();
    auto s = readSnapshot();
    Editor::seqReset (s);
    writeSnapshot (s);
    setP (ids::id ("countby"), 0); setP (ids::id ("pulses"), 16);
    activeSlot = -1;
    setStatus ("Sequence reset");
}

void BeeProcessor::armLearn (bool on)
{
    learnArm = on; learnArmChanged = true;
    setStatus (on ? "Turn a knob to learn" : "Learn off");
}

void BeeProcessor::slotClick (int n, int mod)
{
    n = juce::jlimit (0, 7, n);
    auto pushSlot = [this] (int slot, std::optional<Snapshot> before, std::optional<Snapshot> after)
    {
        Hist h; h.isSlot = true; h.slotN = slot; h.before = before; h.after = after;
        hist.push_back (h); if (hist.size() > 16) hist.erase (hist.begin()); future.clear();
    };
    if (mod == 1 || saveArm)
    {
        pushSlot (n, slots[(size_t) n], readSnapshot());
        slots[(size_t) n] = readSnapshot(); activeSlot = n; saveArm = false;
        setStatus ("Saved to slot " + juce::String (n + 1));
    }
    else if (mod == 2)
    {
        if (slots[(size_t) n]) { pushSlot (n, slots[(size_t) n], std::nullopt); slots[(size_t) n].reset(); setStatus ("Cleared slot " + juce::String (n + 1)); }
        if (activeSlot == n) activeSlot = -1;
    }
    else
    {
        if (! slots[(size_t) n]) { setStatus ("Slot " + juce::String (n + 1) + " is empty"); return; }
        if (playingA.load())
        {
            if (overrideActive.load()) return;
            pushHistory();
            pendingSlotData = *slots[(size_t) n]; pendingSlot = n;
            pendingSlotSet.store (true, std::memory_order_release);
            setStatus ("Slot " + juce::String (n + 1) + " queued");
        }
        else { pushHistory(); writeSnapshot (*slots[(size_t) n]); activeSlot = n; setStatus ("Slot " + juce::String (n + 1) + " loaded"); }
    }
}

void BeeProcessor::copySlot (int a, int b)
{
    if (a == b || ! slots[(size_t) a]) return;
    Hist h; h.isSlot = true; h.slotN = b; h.before = slots[(size_t) b]; h.after = slots[(size_t) a];
    hist.push_back (h); if (hist.size() > 16) hist.erase (hist.begin()); future.clear();
    slots[(size_t) b] = slots[(size_t) a];
    setStatus ("Copied " + juce::String (a + 1) + " > " + juce::String (b + 1));
}

juce::File BeeProcessor::exportClip()
{
    Params p; fillParams (p);
    Engine tmp; tmp.prepare (getSampleRate() > 0 ? getSampleRate() : 48000.0);
    for (int w = 0; w < 2; ++w)
        for (int b = 0; b < 64; ++b)
            if ((heldMask[(size_t) w].load() >> b) & 1) tmp.noteIn (w * 64 + b, 100, p);
    if (p.playmode == Arp && ! tmp.hasHeldNotes (p)) { setStatus ("Hold keys to export"); return {}; }
    const auto clip = tmp.renderClip (p);
    if (clip.notes.empty()) { setStatus ("All stages are silent"); return {}; }
    const int res = 960;
    juce::MidiMessageSequence seq;
    for (auto& n : clip.notes)
    {
        seq.addEvent (juce::MidiMessage::noteOn (p.midiChannel, n.pitch, (juce::uint8) n.vel), n.start * res);
        seq.addEvent (juce::MidiMessage::noteOff (p.midiChannel, n.pitch), (n.start + n.dur) * res);
    }
    seq.updateMatchedPairs();
    auto end = juce::MidiMessage::endOfTrack(); end.setTimeStamp (clip.lengthQ * res); seq.addEvent (end);
    juce::MidiFile mf; mf.setTicksPerQuarterNote (res); mf.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Bumblebee");
    dir.createDirectory();
    int num = 1;
    while (dir.getChildFile ("Bee " + juce::String (num).paddedLeft ('0', 2) + ".mid").exists()) ++num;
    auto f = dir.getChildFile ("Bee " + juce::String (num).paddedLeft ('0', 2) + ".mid");
    juce::FileOutputStream os (f);
    if (! os.openedOk() || ! mf.writeTo (os)) { setStatus ("Clip export failed"); return {}; }
    setStatus (f.getFileNameWithoutExtension() + ": " + juce::String ((int) (clip.lengthQ / 4)) + " bar" + (clip.lengthQ > 4 ? "s" : "") + (clip.capped ? " (max)" : ""));
    return f;
}

// ---------------------------------------------------------------------------
static juce::String encodeSnap (const Snapshot& s)
{
    juce::StringArray a;
    for (auto* arr : { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.mode, &s.div, &s.len, &s.mod[0], &s.mod[1], &s.mod[2], &s.mod[3] })
        for (int v : *arr) a.add (juce::String (v));
    for (int v : { s.length, s.direction, s.transpose, s.path, s.shift }) a.add (juce::String (v));
    for (auto& c : s.layers) for (int v : { c.steps, c.path, c.shift, c.dir, c.rate }) a.add (juce::String (v));
    return a.joinIntoString (" ");
}
static std::optional<Snapshot> decodeSnap (const juce::String& t)
{
    auto a = juce::StringArray::fromTokens (t, " ", "");
    if (a.size() < 12 * NS + 5 + 5 * kNumLayers) return std::nullopt;
    Snapshot s; int p = 0;
    for (auto* arr : { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.mode, &s.div, &s.len, &s.mod[0], &s.mod[1], &s.mod[2], &s.mod[3] })
        for (auto& v : *arr) v = a[p++].getIntValue();
    for (int* v : { &s.length, &s.direction, &s.transpose, &s.path, &s.shift }) *v = a[p++].getIntValue();
    for (auto& c : s.layers) for (int* v : { &c.steps, &c.path, &c.shift, &c.dir, &c.rate }) *v = a[p++].getIntValue();
    return s;
}

void BeeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree sl ("SLOTS");
    for (int n = 0; n < 8; ++n) if (slots[(size_t) n]) sl.setProperty ("s" + juce::String (n), encodeSnap (*slots[(size_t) n]), nullptr);
    state.removeChild (state.getChildWithName ("SLOTS"), nullptr);
    state.appendChild (sl, nullptr);
    state.setProperty ("chordMask", (int) chordMaskA.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void BeeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto sl = tree.getChildWithName ("SLOTS");
            for (int n = 0; n < 8; ++n)
                slots[(size_t) n] = sl.isValid() && sl.hasProperty ("s" + juce::String (n)) ? decodeSnap (sl["s" + juce::String (n)].toString()) : std::nullopt;
            chordMaskA = (uint16_t) (int) tree.getProperty ("chordMask", 1);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* BeeProcessor::createEditor() { return new BeeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BeeProcessor(); }
