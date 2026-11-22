#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tgb;

static const char* kGlobals[] = { "layer", "playmode", "clock", "swing", "transpose", "bassnote", "retrig", "arporder", "arpoct",
    "latch", "length", "countby", "pulses", "direction", "scalemode", "root", "scale", "chroll", "auxtype", "auxcc", "glide",
    "resetstop", "thru", "randtarget", "pool", "notes", "octjump", "evolve", "evotarget", "midich",
    "llen1", "llen2", "llen3", "llen4", "llen5", "cust0", "cust1", "cust2", "cust3", "cust4", "cust5", "cust6", "cust7",
    "cust8", "cust9", "cust10", "cust11" };

TgbProcessor::TgbProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TGB420State", ids::createLayout())
{
    for (int k = 0; k < ids::kNumStageKeys; ++k)
        for (int i = 0; i < NS; ++i)
            stageP[(size_t) k][(size_t) i] = apvts.getRawParameterValue (ids::stage (ids::stageKey (k), i));
    for (auto* n : kGlobals) gp[n] = apvts.getRawParameterValue (ids::id (n));
    for (auto& x : uiLayerPosA) x = -1;
    startTimerHz (30);
}

TgbProcessor::~TgbProcessor() { stopTimer(); }

bool TgbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void TgbProcessor::prepareToPlay (double sr, int) { engine.prepare (sr); }

int TgbProcessor::stageVal (const char* key, int i) const
{
    for (int k = 0; k < ids::kNumStageKeys; ++k)
        if (std::strcmp (ids::stageKey (k), key) == 0) return (int) std::lround (stageP[(size_t) k][(size_t) i]->load());
    return 0;
}
int TgbProcessor::param (const juce::String& name) const { return (int) std::lround (gp.at (name)->load()); }

Snapshot TgbProcessor::readSnapshot() const
{
    Snapshot s;
    std::array<std::array<int, NS>*, 9> dst { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.aux, &s.mode, &s.div, &s.len };
    for (int k = 0; k < 9; ++k)
        for (int i = 0; i < NS; ++i) (*dst[(size_t) k])[(size_t) i] = (int) std::lround (stageP[(size_t) k][(size_t) i]->load());
    s.length = param ("length"); s.direction = param ("direction"); s.transpose = param ("transpose");
    for (int l = 0; l < kNumLayers; ++l) s.llen[(size_t) l] = param ("llen" + juce::String (l + 1));
    return s;
}

int TgbProcessor::effLen (int l) const
{
    const int ll = param ("llen" + juce::String (l + 1));
    return ll > 0 ? juce::jlimit (1, NS, ll) : juce::jlimit (1, NS, param ("length"));
}

void TgbProcessor::fillParams (Params& p) const
{
    p.s = overrideActive.load (std::memory_order_acquire) ? pendingSlotData : readSnapshot();
    p.playmode = param ("playmode"); p.clock = param ("clock"); p.swing = param ("swing");
    p.bassnote = param ("bassnote"); p.retrig = param ("retrig"); p.arporder = param ("arporder");
    p.arpoct = param ("arpoct"); p.latch = param ("latch"); p.countby = param ("countby"); p.pulses = param ("pulses");
    p.scalemode = param ("scalemode"); p.root = param ("root"); p.scale = param ("scale"); p.chroll = param ("chroll");
    p.auxKind = param ("auxtype"); p.auxCC = param ("auxcc"); p.glide = param ("glide");
    p.resetstop = param ("resetstop"); p.thru = param ("thru"); p.evolve = param ("evolve");
    p.midiChannel = param ("midich") + 1;
}

// ---------------------------------------------------------------------------
void TgbProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    audio.clear();

    Params p;
    fillParams (p);

    if (p.playmode != lastPlaymode) { engine.playModeChanged(); lastPlaymode = p.playmode; }
    if (p.latch != lastLatch) { if (! p.latch) engine.latchOff(); lastLatch = p.latch; }
    if (learnArmChanged.exchange (false)) engine.armLearn (learnArm);

    // MIDI in: notes feed Bass/Arp, CCs feed Learn; Thru passes input on in Seq mode
    juce::MidiBuffer thru;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOnOrOff())
        {
            const int n = m.getNoteNumber();
            engine.noteIn (n, m.isNoteOn() ? m.getVelocity() : 0, p);
            auto& mask = heldMask[(size_t) (n / 64)];
            const uint64_t bit = 1ull << (n % 64);
            if (m.isNoteOn()) mask.fetch_or (bit); else mask.fetch_and (~bit);
        }
        else if (m.isController()) engine.ccIn (m.getControllerNumber());
        if (p.thru && p.playmode == Seq) thru.addEvent (m, meta.samplePosition);
    }
    midi.swapWith (thru);

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
        if (e.size == 2) midi.addEvent (juce::MidiMessage (e.b0, e.b1), e.sampleOffset);
        else             midi.addEvent (juce::MidiMessage (e.b0, e.b1, e.b2), e.sampleOffset);
    });

    if (engine.slotFired)
    {
        overrideActive.store (true, std::memory_order_release);
        pendingSlotSet = false;
        slotFiredA = true;
    }
    if (engine.evolveRequested) evolveA = true;
    if (engine.learnedCC) { engine.learnedCC = false; learnedCC = engine.learnedCCNumber; }

    uiStageA = engine.uiStage; uiPulseA = engine.uiPulse;
    for (int l = 0; l < kNumLayers; ++l) uiLayerPosA[(size_t) l] = engine.uiLayerPos[(size_t) l];
    uiHitStageA = engine.uiHitStage; uiHitCount = engine.hitCounter;
}

// ---------------------------------------------------------------------------
void TgbProcessor::timerCallback()
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
        const int st = editor.evolve (editContext(), s);
        writeSnapshot (s);
        activeSlot = -1;
        setStatus ("Evolved stage " + juce::String (st + 1));
    }
    const int cc = learnedCC.exchange (-1);
    if (cc >= 0)
    {
        setP (ids::id ("auxtype"), 0); setP (ids::id ("auxcc"), (float) cc);
        learnArm = false;
        setStatus ("Learned CC " + juce::String (cc));
    }
}

void TgbProcessor::setP (const juce::String& pid, float plain)
{
    if (auto* prm = apvts.getParameter (pid))
    {
        const float v = prm->convertTo0to1 (plain);
        if (std::abs (prm->getValue() - v) < 1.0e-6f) return;
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (v);
        prm->endChangeGesture();
    }
}

void TgbProcessor::writeSnapshot (const Snapshot& s)
{
    const std::array<const std::array<int, NS>*, 9> src { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.aux, &s.mode, &s.div, &s.len };
    for (int k = 0; k < 9; ++k)
        for (int i = 0; i < NS; ++i) setP (ids::stage (ids::stageKey (k), i), (float) (*src[(size_t) k])[(size_t) i]);
    setP (ids::id ("length"), (float) s.length);
    setP (ids::id ("direction"), (float) s.direction);
    setP (ids::id ("transpose"), (float) s.transpose);
    for (int l = 0; l < kNumLayers; ++l) setP (ids::llen (l), (float) s.llen[(size_t) l]);
    if (s.length <= PAGE && effLen (param ("layer")) <= PAGE) page = 0;
}

EditContext TgbProcessor::editContext() const
{
    EditContext c;
    c.layer = param ("layer"); c.playmode = param ("playmode"); c.randtarget = param ("randtarget");
    c.pool = param ("pool"); c.notes = param ("notes"); c.octjump = param ("octjump"); c.evotarget = param ("evotarget");
    c.scalemode = param ("scalemode"); c.root = param ("root"); c.scale = param ("scale");
    for (int i = 0; i < 12; ++i) c.custom[(size_t) i] = param ("cust" + juce::String (i));
    return c;
}

// ---------------------------------------------------------------------------
void TgbProcessor::pushHistory()
{
    Hist h; h.seq = readSnapshot();
    hist.push_back (h);
    if (hist.size() > 16) hist.erase (hist.begin());
    future.clear();
}

void TgbProcessor::randomize()
{
    pushHistory();
    auto s = readSnapshot();
    const auto c = editContext();
    editor.randomize (c, s);
    writeSnapshot (s);
    activeSlot = -1;
    static const char* what[] = { "pitch", "velocity", "gate", "chance", "aux" };
    setStatus (c.randtarget == 0 ? juce::String ("Rolled ") + what[c.layer] : (c.randtarget == 1 ? "Rolled rhythm" : "Rolled all dials"));
    const double now = juce::Time::getMillisecondCounterHiRes();
    rollTimes.push_back (now);
    rollTimes.erase (std::remove_if (rollTimes.begin(), rollTimes.end(), [now] (double t) { return now - t > 5000; }), rollTimes.end());
}

int TgbProcessor::rollsInWindow() const
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    int n = 0; for (double t : rollTimes) if (now - t <= 5000) ++n;
    return n;
}

void TgbProcessor::goBack()
{
    if (hist.empty()) { setStatus ("Nothing to undo"); return; }
    Hist e = hist.back(); hist.pop_back();
    if (e.isSlot)
    {
        slots[(size_t) e.slotN] = e.before; future.push_back (e);
        setStatus ("Slot " + juce::String (e.slotN + 1) + " restored");
        return;
    }
    Hist f; f.seq = readSnapshot(); future.push_back (f);
    writeSnapshot (e.seq);
    setStatus ("Back - " + juce::String ((int) hist.size()) + " left");
}

void TgbProcessor::goForward()
{
    if (future.empty()) { setStatus ("Nothing to redo"); return; }
    Hist e = future.back(); future.pop_back();
    if (e.isSlot) { slots[(size_t) e.slotN] = e.after; hist.push_back (e); setStatus ("Forward"); return; }
    Hist h; h.seq = readSnapshot(); hist.push_back (h);
    writeSnapshot (e.seq);
    setStatus ("Forward");
}

void TgbProcessor::nudge (int d)
{
    auto s = readSnapshot();
    if (s.length < 2) return;
    pushHistory();
    Editor::nudge (s, d);
    writeSnapshot (s);
    activeSlot = -1;
    setStatus (d > 0 ? "Nudged right" : "Nudged left");
}

void TgbProcessor::seqReset()
{
    pushHistory();
    auto s = readSnapshot();
    Editor::seqReset (s);
    writeSnapshot (s);
    setP (ids::id ("countby"), 0); setP (ids::id ("pulses"), 16);
    page = 0; activeSlot = -1;
    setStatus ("Sequence reset");
}

void TgbProcessor::slotClick (int n, int mod)
{
    n = juce::jlimit (0, 7, n);
    if (mod == 1 || saveArm)
    {
        Hist h; h.isSlot = true; h.slotN = n; h.before = slots[(size_t) n]; h.after = readSnapshot();
        hist.push_back (h); if (hist.size() > 16) hist.erase (hist.begin()); future.clear();
        slots[(size_t) n] = readSnapshot();
        activeSlot = n; saveArm = false;
        setStatus ("Saved to slot " + juce::String (n + 1));
    }
    else if (mod == 2)
    {
        if (slots[(size_t) n])
        {
            Hist h; h.isSlot = true; h.slotN = n; h.before = slots[(size_t) n];
            hist.push_back (h); if (hist.size() > 16) hist.erase (hist.begin()); future.clear();
            slots[(size_t) n].reset();
            setStatus ("Cleared slot " + juce::String (n + 1));
        }
        if (activeSlot == n) activeSlot = -1;
    }
    else
    {
        if (! slots[(size_t) n]) { setStatus ("Slot " + juce::String (n + 1) + " is empty"); return; }
        if (playingA.load())
        {
            if (overrideActive.load()) return;               // a switch is still being committed
            pushHistory();
            pendingSlotData = *slots[(size_t) n];
            pendingSlot = n;
            pendingSlotSet.store (true, std::memory_order_release);
            setStatus ("Slot " + juce::String (n + 1) + " queued");
        }
        else
        {
            pushHistory();
            writeSnapshot (*slots[(size_t) n]);
            activeSlot = n;
            setStatus ("Slot " + juce::String (n + 1) + " loaded");
        }
    }
}

void TgbProcessor::copySlot (int a, int b)
{
    if (a == b || ! slots[(size_t) a]) return;
    Hist h; h.isSlot = true; h.slotN = b; h.before = slots[(size_t) b]; h.after = slots[(size_t) a];
    hist.push_back (h); if (hist.size() > 16) hist.erase (hist.begin()); future.clear();
    slots[(size_t) b] = slots[(size_t) a];
    setStatus ("Copied " + juce::String (a + 1) + " > " + juce::String (b + 1));
}

// MIDI Clip: Live's version writes a clip into the Set; a plug-in can't, so this writes a
// Standard MIDI File you can drag into Cubase (or anywhere).
juce::File TgbProcessor::exportClip()
{
    Params p; fillParams (p);
    Engine tmp; tmp.prepare (getSampleRate() > 0 ? getSampleRate() : 48000.0);
    for (int w = 0; w < 2; ++w)
        for (int b = 0; b < 64; ++b)
            if ((heldMask[(size_t) w].load() >> b) & 1) tmp.noteIn (w * 64 + b, 100, p);
    if (p.playmode == Arp && ! tmp.hasHeldNotes (p)) { setStatus ("Hold keys to export"); return {}; }
    const auto clip = tmp.renderClip (p);
    if (clip.notes.empty()) { setStatus ("All stages are silent"); return {}; }

    const int ppqRes = 960;
    juce::MidiMessageSequence seq;
    const int ch = p.midiChannel;
    for (auto& n : clip.notes)
    {
        const double on = n.start * ppqRes, off = (n.start + n.dur) * ppqRes;
        seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, (juce::uint8) n.vel), on);
        seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), off);
    }
    seq.updateMatchedPairs();
    juce::MidiMessage end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp (clip.lengthQ * ppqRes);
    seq.addEvent (end);

    juce::MidiFile mf; mf.setTicksPerQuarterNote (ppqRes); mf.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("TGB-420");
    dir.createDirectory();
    int num = 1;
    while (dir.getChildFile ("Seq " + juce::String (num).paddedLeft ('0', 2) + ".mid").exists()) ++num;
    auto f = dir.getChildFile ("Seq " + juce::String (num).paddedLeft ('0', 2) + ".mid");
    juce::FileOutputStream os (f);
    if (! os.openedOk() || ! mf.writeTo (os)) { setStatus ("Clip export failed"); return {}; }
    setStatus (f.getFileNameWithoutExtension() + ": " + juce::String ((int) (clip.lengthQ / 4)) + " bar" + (clip.lengthQ > 4 ? "s" : "") + (clip.capped ? " (max)" : ""));
    return f;
}

// ---------------------------------------------------------------------------
static juce::String encodeSnap (const Snapshot& s)
{
    juce::StringArray a;
    for (auto* arr : { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.aux, &s.mode, &s.div, &s.len })
        for (int v : *arr) a.add (juce::String (v));
    a.add (juce::String (s.length)); a.add (juce::String (s.direction)); a.add (juce::String (s.transpose));
    for (int v : s.llen) a.add (juce::String (v));
    return a.joinIntoString (" ");
}
static std::optional<Snapshot> decodeSnap (const juce::String& t)
{
    auto a = juce::StringArray::fromTokens (t, " ", "");
    if (a.size() < 9 * NS + 8) return std::nullopt;
    Snapshot s; int p = 0;
    for (auto* arr : { &s.pitch, &s.oct, &s.vel, &s.gate, &s.chance, &s.aux, &s.mode, &s.div, &s.len })
        for (auto& v : *arr) v = a[p++].getIntValue();
    s.length = a[p++].getIntValue(); s.direction = a[p++].getIntValue(); s.transpose = a[p++].getIntValue();
    for (auto& v : s.llen) v = a[p++].getIntValue();
    return s;
}

void TgbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree sl ("SLOTS");
    for (int n = 0; n < 8; ++n)
        if (slots[(size_t) n]) sl.setProperty ("s" + juce::String (n), encodeSnap (*slots[(size_t) n]), nullptr);
    state.removeChild (state.getChildWithName ("SLOTS"), nullptr);
    state.appendChild (sl, nullptr);
    state.setProperty ("page", page, nullptr);
    state.setProperty ("follow", follow, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TgbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto sl = tree.getChildWithName ("SLOTS");
            int nf = 0;
            for (int n = 0; n < 8; ++n)
            {
                slots[(size_t) n] = sl.isValid() && sl.hasProperty ("s" + juce::String (n)) ? decodeSnap (sl["s" + juce::String (n)].toString()) : std::nullopt;
                if (slots[(size_t) n]) ++nf;
            }
            page = (int) tree.getProperty ("page", 0); follow = (bool) tree.getProperty ("follow", false);
            apvts.replaceState (tree);
            if (nf) setStatus (juce::String (nf) + " pattern" + (nf > 1 ? "s" : "") + " loaded");
        }
}

juce::AudioProcessorEditor* TgbProcessor::createEditor() { return new TgbEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TgbProcessor(); }
