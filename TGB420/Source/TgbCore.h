// TgbCore.h - framework-free port of the TGB-420 Sequencer engine (tgb420.js v1.0)
// The Max device computed pulses ahead into [coll]s; here every pulse is computed
// sample-accurately inside the host's audio block. Function names follow the JS.
#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>

namespace tgb
{
constexpr int NS = 16;            // stages
constexpr int PAGE = 8;
constexpr int TPQ = 480;
constexpr int kNumLayers = 5;     // pitch(+oct), velocity, gate, chance, aux
enum Layer { LPitch = 0, LVel, LGate, LChance, LAux };
enum PlayMode { Seq = 0, Bass, Arp };
enum Direction { Fwd = 0, Rev, Ping, Rand };

inline int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- tables
struct ScaleDef { const char* name; std::vector<int> iv; };
inline const std::vector<ScaleDef>& scales()
{
    static const std::vector<ScaleDef> s {
        { "Major", {0,2,4,5,7,9,11} }, { "Minor", {0,2,3,5,7,8,10} }, { "Dorian", {0,2,3,5,7,9,10} },
        { "Phrygian", {0,1,3,5,7,8,10} }, { "Lydian", {0,2,4,6,7,9,11} }, { "Mixolydian", {0,2,4,5,7,9,10} },
        { "Locrian", {0,1,3,5,6,8,10} }, { "Harmonic Minor", {0,2,3,5,7,8,11} }, { "Melodic Minor", {0,2,3,5,7,9,11} },
        { "Major Pentatonic", {0,2,4,7,9} }, { "Minor Pentatonic", {0,3,5,7,10} }, { "Blues", {0,3,5,6,7,10} },
        { "Phrygian Dominant", {0,1,4,5,7,8,10} }, { "Hungarian Minor", {0,2,3,6,7,8,11} },
        { "Whole Tone", {0,2,4,6,8,10} }, { "Diminished", {0,2,3,5,6,8,9,11} } };
    return s;
}
inline const char* poolName (int i)
{
    static const char* n[] = { "Classic", "One Note", "Power", "Tension", "Hypnotic", "Dark", "Scale", "Custom" };
    return n[clampi (i, 0, 7)];
}
inline const char* arpName (int i)
{
    static const char* n[] = { "Up", "Down", "UpDown", "DownUp", "Up & Down", "Down & Up", "Converge", "Diverge",
                               "Con & Diverge", "Pinky Up", "Pinky UpDown", "Thumb Up", "Thumb UpDown",
                               "Play Order", "Random", "Random Other", "Random Once" };
    return n[clampi (i, 0, 16)];
}
inline std::string noteName (int n)
{
    static const char* nn[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return std::string (nn[((n % 12) + 12) % 12]) + std::to_string ((int) std::floor (n / 12.0) - 2);
}
inline double clockQuarters (int clock) { static const double q[] = { 0.125, 0.25, 0.5, 1.0 }; return q[clampi (clock, 0, 3)]; }
inline int clockTicks (int clock)       { static const int t[] = { 60, 120, 240, 480 }; return t[clampi (clock, 0, 3)]; }

// ---------------------------------------------------------------- data
// Everything a pattern slot / undo step holds (the JS "snapshot")
struct Snapshot
{
    std::array<int, NS> pitch {}, oct {}, vel {}, gate {}, chance {}, aux {}, mode {}, div {}, len {};
    int length = 8, direction = Fwd, transpose = 0;
    std::array<int, kNumLayers> llen {};   // 0 = linked to Stages

    Snapshot() { resetStages(); }
    void resetStages()
    {
        pitch.fill (0); oct.fill (0); vel.fill (100); gate.fill (50); chance.fill (100);
        aux.fill (0); mode.fill (1); div.fill (0); len.fill (1);
    }
    bool operator== (const Snapshot& o) const
    {
        return pitch == o.pitch && oct == o.oct && vel == o.vel && gate == o.gate && chance == o.chance && aux == o.aux
            && mode == o.mode && div == o.div && len == o.len && length == o.length && direction == o.direction
            && transpose == o.transpose && llen == o.llen;
    }
};

enum AuxKind { AuxCC = 0, AuxPitchBend, AuxAftertouch };

struct Params
{
    Snapshot s;
    int playmode = Seq, clock = 1, swing = 0, bassnote = 36, retrig = 1, arporder = 0, arpoct = 1, latch = 0;
    int countby = 0, pulses = 16;
    int scalemode = 0, root = 0, scale = 1;   // scalemode: 0 Off, 1 Custom (Live's scale isn't available to VST3)
    int chroll = 0;                           // 0 per pulse, 1 per stage
    int auxKind = AuxCC, auxCC = 74, glide = 0;
    int resetstop = 1, thru = 0, evolve = 0;
    int midiChannel = 1;
};

struct MidiOut { int sampleOffset; uint8_t b0, b1, b2; int size; };

// ---------------------------------------------------------------- scale helpers
struct ActiveScale { bool on = false; int root = 0; const std::vector<int>* iv = nullptr; };
inline ActiveScale activeScale (const Params& p)
{
    ActiveScale a;
    if (p.scalemode == 1) { a.on = true; a.root = p.root; a.iv = &scales()[(size_t) clampi (p.scale, 0, 15)].iv; }
    return a;
}
inline bool inScale (const ActiveScale& sc, int n)
{
    const int pc = ((n - sc.root) % 12 + 12) % 12;
    return std::find (sc.iv->begin(), sc.iv->end(), pc) != sc.iv->end();
}
// nearest scale note, ties go down
inline int snap (const Params& p, int n)
{
    auto sc = activeScale (p);
    if (! sc.on) return n;
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (sc, n - d)) return n - d;
        if (d > 0 && inScale (sc, n + d)) return n + d;
    }
    return n;
}
inline int poolRoot (const Params& p) { return p.scalemode == 1 ? p.root : 0; }

// ---------------------------------------------------------------- arp helpers
inline std::vector<int> upDown (const std::vector<int>& a)
{
    if (a.size() <= 2) return a;
    auto o = a;
    for (size_t i = a.size() - 2; i >= 1; --i) o.push_back (a[i]);
    return o;
}
inline std::vector<int> converge (const std::vector<int>& a)
{
    std::vector<int> o; int lo = 0, hi = (int) a.size() - 1;
    while (lo <= hi) { o.push_back (a[(size_t) lo++]); if (lo <= hi) o.push_back (a[(size_t) hi--]); }
    return o;
}
// Park-Miller, as in the JS (exact in doubles; exact in int64 here)
struct ParkMiller
{
    int64_t t;
    explicit ParkMiller (double seed)
    {
        t = (int64_t) std::fmod (std::abs (std::floor (seed)), 2147483646.0) + 1;
        for (int w = 0; w < 3; ++w) t = (t * 16807) % 2147483647;
    }
    double operator()() { t = (t * 16807) % 2147483647; return (double) (t - 1) / 2147483646.0; }
};
inline std::vector<int> seededShuffle (std::vector<int> b, double seed)
{
    ParkMiller r (seed);
    for (int i = (int) b.size() - 1; i > 0; --i) { int j = (int) std::floor (r() * (i + 1)); std::swap (b[(size_t) i], b[(size_t) j]); }
    return b;
}

// ===========================================================================
class Engine
{
public:
    // ---- public read-outs for the UI
    int uiStage = -1, uiPulse = 0;
    std::array<int, kNumLayers> uiLayerPos { -1, -1, -1, -1, -1 };
    int uiHitStage = -1, uiHitVel = 0;   // last stage that played a note
    bool evolveRequested = false;        // processor applies the mutation on the message thread
    bool slotFired = false;              // a queued slot took over at a cycle start this block
    bool learnedCC = false; int learnedCCNumber = 0;

    void prepare (double sr) { sampleRate = sr; stoppedReset (true); }
    void seedRandom (uint32_t s) { rng.seed (s); }

    // MIDI input (call before process for each incoming message)
    void noteIn (int pitch, int vel, const Params& p)
    {
        if (vel > 0)
        {
            const bool wasEmpty = held.empty();
            if (std::find (held.begin(), held.end(), pitch) == held.end()) held.push_back (pitch);
            if (p.playmode == Bass) bassOverride = pitch;
            if (p.playmode == Arp && p.latch && wasEmpty) latched.clear();
            if (p.playmode == Arp && p.latch && std::find (latched.begin(), latched.end(), pitch) == latched.end()) latched.push_back (pitch);
            if (wasEmpty && p.retrig && p.playmode > 0) restartPending = true;
        }
        else
        {
            auto it = std::find (held.begin(), held.end(), pitch);
            if (it != held.end()) held.erase (it);
        }
        if (! (p.latch && vel == 0)) arpSeed = (double) std::uniform_int_distribution<int> (1, 2147483640) (rng);
    }
    void armLearn (bool on) { learnArmed = on; }
    void setArpSeed (double s) { arpSeed = s; }   // for tests
    void ccIn (int num) { if (learnArmed) { learnArmed = false; learnedCC = true; learnedCCNumber = num; } }
    void playModeChanged() { bassOverride = -1; held.clear(); latched.clear(); }
    void latchOff() { latched.clear(); }

    // pending pattern slot (switches at the next cycle start); nullptr = none
    void setPendingSlot (const Snapshot* s) { pendingSlot = s; }

    // ---- main entry --------------------------------------------------------
    template <typename Emit>
    void process (Params& p, bool playing, double ppqStart, double bpm, int numSamples, Emit&& emit)
    {
        evolveRequested = false; slotFired = false;
        if (! playing || bpm <= 0.0)
        {
            if (wasPlaying) { stopped (p); allNotesOff (0, p, emit); }
            wasPlaying = false;
            return;
        }
        tempo = bpm;
        const double spq = sampleRate * 60.0 / bpm;
        const double ppqEnd = ppqStart + numSamples / spq;
        const double pq = clockQuarters (p.clock);
        constexpr double eps = 1e-9;
        auto toSample = [&] (double q) { return clampi ((int) std::floor ((q - ppqStart) * spq + 0.5), 0, numSamples - 1); };

        if (! wasPlaying) lastGrid = INT64_MIN;
        else if (std::abs (ppqStart - expectedPpq) > 1e-3)
        {
            // cycle jump / relocation: like Live's looping transport, the sequence keeps
            // running; only notes and ramps scheduled at the old position are dropped
            allNotesOff (0, p, emit);
            lastGrid = INT64_MIN;
        }
        wasPlaying = true;
        expectedPpq = ppqEnd;

        // pulses on the transport grid (Live's metro @quantize): grid index k at k * pq
        int64_t k = (int64_t) std::ceil ((ppqStart - eps) / pq);
        if (lastGrid != INT64_MIN && k <= lastGrid && ppqStart >= lastGridPpq) k = lastGrid + 1;
        for (; (double) k * pq < ppqEnd - eps; ++k)
        {
            lastGrid = k; lastGridPpq = (double) k * pq;
            Pulse out;
            simTick (p, k, out);
            const double t = (double) k * pq;
            if (out.hasNote) pendingNotes.push_back ({ t + out.delayQ, out.pitch, out.vel, out.durQ });
            if (out.hasAux)  pendingAux.push_back ({ t + out.delayQ, out.auxValue, out.rampQ });
        }

        // ---- notes: ons and offs in time order
        struct Ev { double t; int kind; int pitch; int vel; double dur; };   // kind 0 off, 1 on
        std::vector<Ev> evs;
        for (auto it = pendingNotes.begin(); it != pendingNotes.end();)
            if (it->onQ < ppqEnd - eps) { evs.push_back ({ std::max (it->onQ, ppqStart), 1, it->pitch, it->vel, it->durQ }); it = pendingNotes.erase (it); }
            else ++it;
        std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t || (! (b.t < a.t) && a.kind < b.kind); });
        const uint8_t ch = (uint8_t) (clampi (p.midiChannel, 1, 16) - 1);
        auto flushOffsUpTo = [&] (double t)
        {
            std::sort (active.begin(), active.end(), [] (const Active& a, const Active& b) { return a.offQ < b.offQ; });
            while (! active.empty() && active.front().offQ <= t + eps)
            {
                emit (MidiOut { toSample (active.front().offQ), (uint8_t) (0x80 | ch), (uint8_t) active.front().pitch, 0, 3 });
                active.erase (active.begin());
            }
        };
        for (auto& e : evs)
        {
            flushOffsUpTo (e.t);
            // makenote @repeatmode 1: retrigger a sounding pitch
            for (auto a = active.begin(); a != active.end(); ++a)
                if (a->pitch == e.pitch) { emit (MidiOut { toSample (e.t), (uint8_t) (0x80 | ch), (uint8_t) e.pitch, 0, 3 }); active.erase (a); break; }
            emit (MidiOut { toSample (e.t), (uint8_t) (0x90 | ch), (uint8_t) e.pitch, (uint8_t) e.vel, 3 });
            active.push_back ({ e.pitch, e.t + e.dur });
        }
        std::sort (active.begin(), active.end(), [] (const Active& a, const Active& b) { return a.offQ < b.offQ; });
        while (! active.empty() && active.front().offQ < ppqEnd - eps)
        {
            emit (MidiOut { toSample (active.front().offQ), (uint8_t) (0x80 | ch), (uint8_t) active.front().pitch, 0, 3 });
            active.erase (active.begin());
        }

        // ---- aux: [line] with a 5 ms grain into [change] (ints), then CC / bend / pressure
        processAux (p, ppqStart, ppqEnd, spq, numSamples, ch, emit);
    }

    template <typename Emit>
    void allNotesOff (int so, const Params& p, Emit&& emit)
    {
        const uint8_t ch = (uint8_t) (clampi (p.midiChannel, 1, 16) - 1);
        for (auto& a : active) emit (MidiOut { so, (uint8_t) (0x80 | ch), (uint8_t) a.pitch, 0, 3 });
        active.clear(); pendingNotes.clear(); pendingAux.clear();
        ramp.active = false;
    }

    // Render one cycle to notes (for the MIDI Clip export). Times in quarter notes.
    struct ClipNote { int pitch; double start, dur; int vel; double prob; };
    struct Clip { std::vector<ClipNote> notes; double lengthQ = 0; bool capped = false; };
    Clip renderClip (const Params& p) const
    {
        Engine t (*this);    // work on a copy: the live position is untouched
        return t.renderClipInner (p);
    }

    bool hasHeldNotes (const Params& p) const { return ! (p.latch ? latched : held).empty(); }

private:
    struct Pulse { bool hasNote = false; int pitch = 0, vel = 0; double durQ = 0, delayQ = 0;
                   bool hasAux = false; int auxValue = 0; double rampQ = 0; };
    struct PendingNote { double onQ; int pitch, vel; double durQ; };
    struct PendingAux  { double atQ; int value; double rampQ; };
    struct Active { int pitch; double offQ; };

    // ---- per-layer steps
    static bool layerUnlinked (const Params& p, int l) { return p.s.llen[(size_t) l] > 0; }
    static int stageCount (const Params& p) { return clampi (p.s.length, 1, NS); }
    static int effLen (const Params& p, int l) { return layerUnlinked (p, l) ? clampi (p.s.llen[(size_t) l], 1, NS) : stageCount (p); }
    int lidx (const Params& p, int l, int i) const { return layerUnlinked (p, l) ? lc[(size_t) l] % effLen (p, l) : i; }
    int rint (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }
    double rnd() { return std::uniform_real_distribution<double> (0.0, 1.0) (rng); }

    void resetLayers (const Params& p)
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            const int n = effLen (p, l);
            lc[(size_t) l] = p.s.direction == Rev ? n - 1 : (p.s.direction == Rand ? rint (0, n - 1) : 0);
            ld[(size_t) l] = 1;
        }
    }
    void advanceLayers (const Params& p)
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            if (! layerUnlinked (p, l)) continue;
            const int n = effLen (p, l); int c = lc[(size_t) l] % n;
            if (p.s.direction == Rev) c = (c - 1 + n) % n;
            else if (p.s.direction == Ping)
            {
                if (n == 1) c = 0;
                else { if (c + ld[(size_t) l] >= n || c + ld[(size_t) l] < 0) ld[(size_t) l] = -ld[(size_t) l]; c += ld[(size_t) l]; }
            }
            else if (p.s.direction == Rand) c = rint (0, n - 1);
            else c = (c + 1) % n;
            lc[(size_t) l] = c;
        }
    }
    static bool playable (const Params& p, int i) { return p.s.len[(size_t) i] > 0; }
    static bool anyPlayable (const Params& p) { for (int i = 0; i < stageCount (p); ++i) if (playable (p, i)) return true; return false; }
    int firstStage (const Params& p)
    {
        const int L = stageCount (p);
        if (p.s.direction == Rev) { for (int i = L - 1; i >= 0; --i) if (playable (p, i)) return i; }
        else if (p.s.direction == Rand)
        {
            std::vector<int> c; for (int i = 0; i < L; ++i) if (playable (p, i)) c.push_back (i);
            return c.empty() ? 0 : c[(size_t) rint (0, (int) c.size() - 1)];
        }
        else { for (int i = 0; i < L; ++i) if (playable (p, i)) return i; }
        return 0;
    }
    int nextStage (const Params& p, int cur)
    {
        const int L = stageCount (p);
        if (! anyPlayable (p)) return cur;
        if (p.s.direction == Rand)
        {
            std::vector<int> c; for (int i = 0; i < L; ++i) if (playable (p, i)) c.push_back (i);
            return c[(size_t) rint (0, (int) c.size() - 1)];
        }
        if (p.s.direction == Ping)
        {
            int n = cur;
            for (int k = 0; k < 2 * L + 2; ++k)
            {
                if (n + pingDir >= L || n + pingDir < 0) pingDir = -pingDir;
                n += pingDir;
                if (L == 1) n = 0;
                if (playable (p, n)) return n;
            }
            return cur;
        }
        const int step = p.s.direction == Rev ? -1 : 1;
        int n = cur;
        for (int k = 0; k < L; ++k) { n = ((n + step) % L + L) % L; if (playable (p, n)) return n; }
        return cur;
    }
    bool isCycleStart (const Params& p, int st)
    {
        if (p.countby == 1) return seqPulse == 0;
        if (p.s.direction == Rand) return true;
        return st == firstStageNoRandom (p);
    }
    static int firstStageNoRandom (const Params& p)   // firstStage without consuming randomness
    {
        const int L = stageCount (p);
        if (p.s.direction == Rev) { for (int i = L - 1; i >= 0; --i) if (playable (p, i)) return i; }
        else { for (int i = 0; i < L; ++i) if (playable (p, i)) return i; }
        return 0;
    }

    // ---- arp
    std::vector<int> arpPool (const Params& p) const
    {
        const auto& src = p.latch ? latched : held;
        if (src.empty()) return {};
        const int oct = clampi (p.arpoct, 1, 4), st = clampi (p.arporder, 0, 16);
        auto base = src;
        if (st != 13) std::sort (base.begin(), base.end());
        std::vector<int> a;
        for (int o = 0; o < oct; ++o) for (int n : base) a.push_back (n + 12 * o);
        if (a.size() < 2) return a;
        std::vector<int> d (a.rbegin(), a.rend());
        const int top = a.back(), bottom = a.front();
        auto between = [] (const std::vector<int>& notes, int fixed) { std::vector<int> o; for (int n : notes) { o.push_back (n); o.push_back (fixed); } return o; };
        auto thumbs  = [] (const std::vector<int>& notes, int fixed) { std::vector<int> o; for (int n : notes) { o.push_back (fixed); o.push_back (n); } return o; };
        switch (st)
        {
            case 1: return d;
            case 2: return upDown (a);
            case 3: return upDown (d);
            case 4: { auto o = a; o.insert (o.end(), d.begin(), d.end()); return o; }
            case 5: { auto o = d; o.insert (o.end(), a.begin(), a.end()); return o; }
            case 6: return converge (a);
            case 7: { auto c = converge (a); std::reverse (c.begin(), c.end()); return c; }
            case 8: { auto c = converge (a); if (c.size() > 2) for (size_t i = c.size() - 2; i >= 1; --i) c.push_back (c[i]); return c; }
            case 9:  return between (std::vector<int> (a.begin(), a.end() - 1), top);
            case 10: return between (upDown (std::vector<int> (a.begin(), a.end() - 1)), top);
            case 11: return thumbs (std::vector<int> (a.begin() + 1, a.end()), bottom);
            case 12: return thumbs (upDown (std::vector<int> (a.begin() + 1, a.end())), bottom);
            case 16: return seededShuffle (a, arpSeed);
            default: return a;
        }
    }
    int nextArpNote (const Params& p)
    {
        auto pool = arpPool (p);
        if (pool.empty()) return -1;
        const int st = clampi (p.arporder, 0, 16);
        if (st == 14) return pool[(size_t) rint (0, (int) pool.size() - 1)];
        if (st == 15)
        {
            const int L = (int) pool.size(), cyc = arpIdx / L;
            auto perm = seededShuffle (pool, arpSeed * 7919 + cyc);
            if (cyc > 0 && L > 1)
            {
                auto prev = seededShuffle (pool, arpSeed * 7919 + cyc - 1);
                if (perm[0] == prev[(size_t) L - 1]) std::swap (perm[0], perm[1]);
            }
            const int n = perm[(size_t) (arpIdx % L)];
            ++arpIdx;
            return n;
        }
        const int n = pool[(size_t) (arpIdx % (int) pool.size())];
        arpIdx = (arpIdx + 1) % (int) pool.size();
        return n;
    }
    int stagePitch (const Params& p, int i, int arpNote) const
    {
        int n;
        if (p.playmode == Bass) n = (bassOverride >= 0 ? bassOverride : p.bassnote) + 12 * p.s.oct[(size_t) i] + p.s.transpose;
        else if (p.playmode == Arp) { if (arpNote < 0) return -1; n = arpNote + p.s.pitch[(size_t) i] + p.s.transpose; }
        else n = 60 + p.s.pitch[(size_t) i] + p.s.transpose;
        return clampi (snap (p, n), 0, 127);
    }

    // ---- tick
    void stepPosition (Params& p)
    {
        ++pulse; ++seqPulse;
        const bool endOfStage = pulse >= p.s.len[(size_t) stage];
        const bool endOfSeq = p.countby == 1 && seqPulse >= clampi (p.pulses, 1, 64);
        if (endOfSeq) { stage = firstStage (p); pulse = 0; seqPulse = 0; pingDir = 1; resetLayers (p); }
        else if (endOfStage) { stage = nextStage (p, stage); pulse = 0; advanceLayers (p); }
        if (pulse == 0 && isCycleStart (p, stage)) onCycleStart (p);
    }
    void onCycleStart (Params& p)
    {
        if (pendingSlot)
        {
            p.s = *pendingSlot;          // exact timing: takes effect on this very pulse
            pendingSlot = nullptr;
            slotFired = true;
        }
    }
    void advanceCounters (const Params& p)
    {
        ++pulseCount;
        tickAccum += clockTicks (p.clock);
        static const int bars[] = { 0, 8, 16, 32, 64 };
        const int b = bars[clampi (p.evolve, 0, 4)];
        if (b && tickAccum >= (int64_t) b * 4 * TPQ) { tickAccum = 0; evolveRequested = true; }
    }
    void simTick (Params& p, int64_t gridIndex, Pulse& out)
    {
        if (! anyPlayable (p)) { advanceCounters (p); return; }
        bool startup = false;
        const bool restart = restartPending;
        restartPending = false;
        if (! running || restart)
        {
            if (! running && ! restart && ! p.resetstop && resumable && stage < stageCount (p) && playable (p, stage))
                running = true;   // resume where we were
            else
            {
                running = true; stage = firstStage (p); pulse = 0; seqPulse = 0; pingDir = 1; arpIdx = 0;
                resetLayers (p); startup = true;
            }
        }
        else stepPosition (p);
        if (startup && pendingSlot) onCycleStart (p);
        playPulse (p, stage, pulse, gridIndex, out);
        advanceCounters (p);
    }
    void playPulse (Params& p, int i, int pl, int64_t gridIndex, Pulse& out)
    {
        const double pq = clockQuarters (p.clock);
        const double msQ = tempo / 60000.0;                   // quarters per millisecond
        // swing: every 2nd pulse of the grid, up to half a pulse
        const double delay = ((gridIndex & 1) && p.swing > 0) ? p.swing / 100.0 * pq * 0.5 : 0.0;
        const int m = p.s.mode[(size_t) i];
        const int ip = lidx (p, 0, i), iv = lidx (p, 1, i), ig = lidx (p, 2, i), ic = lidx (p, 3, i), ia = lidx (p, 4, i);
        uiStage = i; uiPulse = pl;
        for (int l = 0; l < kNumLayers; ++l) uiLayerPos[(size_t) l] = layerUnlinked (p, l) ? lidx (p, l, i) : -1;

        if (pl == 0)
        {
            const int auxv = p.s.aux[(size_t) ia];
            const double rampQ = p.glide ? p.s.len[(size_t) i] * pq : 0.0;
            if (auxv != lastAux || p.glide) { out.hasAux = true; out.auxValue = auxv; out.rampQ = rampQ; lastAux = auxv; }
            if (p.chroll == 1) stageRoll = rnd() * 100.0 < p.s.chance[(size_t) ic];
        }
        out.delayQ = delay;

        const int d = clampi (p.s.div[(size_t) i], 0, 3) + 1;
        bool fire = false; double dur = 0;
        const double g = clampi (p.s.gate[(size_t) ig], 5, 100) / 100.0;
        if (m == 1 && pl == 0)          { fire = true; dur = g >= 1.0 ? pq + 8 * msQ : g * pq; }
        else if (m == 2 && pl % d == 0) { fire = true; dur = g >= 1.0 ? pq * d + 8 * msQ : g * pq * d; }
        else if (m == 3 && pl == 0)     { fire = true; dur = g >= 1.0 ? p.s.len[(size_t) i] * pq + 8 * msQ : p.s.len[(size_t) i] * pq * g; }
        if (! fire) return;

        const bool ok = p.chroll == 1 ? stageRoll : (rnd() * 100.0 < p.s.chance[(size_t) ic]);
        if (! ok) return;
        int an = -1;
        if (p.playmode == Arp) { an = nextArpNote (p); if (an < 0) return; }
        const int n = stagePitch (p, ip, an);
        if (n < 0) return;
        out.hasNote = true; out.pitch = n; out.vel = clampi (p.s.vel[(size_t) iv], 1, 127);
        out.durQ = std::max (5 * msQ, dur);
        uiHitStage = i; uiHitVel = out.vel; ++hitCounter;
    }
public:
    int hitCounter = 0;
private:
    void stopped (const Params& p)
    {
        running = false;
        resumable = true;
        if (p.resetstop) { stage = 0; pulse = 0; seqPulse = 0; pulseCount = 0; tickAccum = 0; }
        lastAux = -1;
        uiStage = -1;
        for (auto& x : uiLayerPos) x = -1;
    }
    void stoppedReset (bool)
    {
        running = false; resumable = false; stage = 0; pulse = 0; seqPulse = 0; pulseCount = 0; tickAccum = 0;
        pingDir = 1; lastAux = -1; wasPlaying = false; uiStage = -1;
        active.clear(); pendingNotes.clear(); pendingAux.clear(); ramp.active = false; lastSentAux = -1;
    }

    // ---- aux output
    struct Ramp { bool active = false; double t0 = 0, dur = 0, from = 0, to = 0; } ramp;
    double auxCur = 0;
    int lastSentAux = -1;

    template <typename Emit>
    void processAux (const Params& p, double q0, double q1, double spq, int numSamples, uint8_t ch, Emit&& emit)
    {
        constexpr double eps = 1e-9;
        const double grainQ = 0.005 * tempo / 60.0;                 // [line 0. 5.]: 5 ms grain
        auto send = [&] (double q, double value)
        {
            const int v = clampi ((int) value, 0, 127);             // [i] truncates, [change] dedupes
            if (v == lastSentAux) return;
            lastSentAux = v;
            const int so = clampi ((int) std::floor ((q - q0) * spq + 0.5), 0, numSamples - 1);
            if (p.auxKind == AuxPitchBend)       emit (MidiOut { so, (uint8_t) (0xE0 | ch), 0, (uint8_t) v, 3 });
            else if (p.auxKind == AuxAftertouch) emit (MidiOut { so, (uint8_t) (0xD0 | ch), (uint8_t) v, 0, 2 });
            else                                 emit (MidiOut { so, (uint8_t) (0xB0 | ch), (uint8_t) clampi (p.auxCC, 0, 127), (uint8_t) v, 3 });
        };
        std::sort (pendingAux.begin(), pendingAux.end(), [] (const PendingAux& a, const PendingAux& b) { return a.atQ < b.atQ; });
        double t = q0;
        auto runRampUntil = [&] (double until)
        {
            if (! ramp.active) return;
            // grain points inside (t, until]
            double g = ramp.t0 + std::ceil ((t - ramp.t0) / grainQ + eps) * grainQ;
            for (; g <= std::min (until, ramp.t0 + ramp.dur) + eps; g += grainQ)
            {
                const double f = ramp.dur > 0 ? std::min (1.0, (g - ramp.t0) / ramp.dur) : 1.0;
                auxCur = ramp.from + (ramp.to - ramp.from) * f;
                send (g, auxCur);
            }
            if (until >= ramp.t0 + ramp.dur - eps) { auxCur = ramp.to; send (std::min (until, ramp.t0 + ramp.dur), auxCur); ramp.active = false; }
        };
        for (auto it = pendingAux.begin(); it != pendingAux.end();)
        {
            if (it->atQ >= q1 - eps) { ++it; continue; }
            const double at = std::max (it->atQ, q0);
            runRampUntil (at);
            t = at;
            if (it->rampQ <= 0) { ramp.active = false; auxCur = it->value; send (at, auxCur); }
            else { ramp = { true, at, it->rampQ, auxCur, (double) it->value }; send (at, auxCur); }
            it = pendingAux.erase (it);
        }
        runRampUntil (q1 - eps);
    }

    // ---- clip rendering (renderNotes in the JS)
    static int gcd (int a, int b) { while (b) { int t = b; b = a % b; a = t; } return a ? a : 1; }
    Clip renderClipInner (const Params& pIn)
    {
        Params p = pIn;
        Clip clip;
        const double pb = clockQuarters (p.clock);
        const double sw = p.swing / 100.0 * pb * 0.5;
        pingDir = 1; arpIdx = 0; resetLayers (p);
        int cycle = 0; const int L = stageCount (p);
        if (p.countby == 1) cycle = clampi (p.pulses, 1, 64);
        else if (p.s.direction == Rand) cycle = 16;
        else
        {
            for (int k = 0; k < L; ++k) cycle += p.s.len[(size_t) k];
            if (p.s.direction == Ping) for (int k = 1; k < L - 1; ++k) cycle += p.s.len[(size_t) k];
        }
        if (cycle < 1) cycle = 1;
        double mult = 1;
        if (p.countby != 1 && p.s.direction != Rand)
        {
            int adv = 0, s0 = firstStage (p), q = 0;
            for (int tp = 0; tp < cycle; ++tp)
            {
                if (tp > 0) { ++q; if (q >= p.s.len[(size_t) s0]) { s0 = nextStage (p, s0); q = 0; } }
                if (q == 0) ++adv;
            }
            pingDir = 1;
            const int per = p.s.direction == Ping ? 2 : 1;
            for (int ll = 0; ll < 4; ++ll)
            {
                if (! layerUnlinked (p, ll)) continue;
                const int nl = effLen (p, ll) * per, steps = nl / gcd (adv, nl);
                mult = mult * steps / gcd ((int) mult, steps);
            }
        }
        const double cycleBeats = cycle * pb * mult;
        int bars = 0;
        for (int b = 1; b <= 16; ++b) { const double r = (b * 4) / cycleBeats; if (std::abs (r - std::round (r)) < 1e-6) { bars = b; break; } }
        if (! bars) bars = (int) std::ceil (cycleBeats / 4);
        clip.capped = bars > 16;
        if (clip.capped) bars = 16;
        const int total = (int) std::round (bars * 4 / pb);
        int st = firstStage (p), pl = 0, sp = 0;
        resetLayers (p);
        for (int gp = 0; gp < total; ++gp)
        {
            if (gp > 0)
            {
                ++pl; ++sp;
                if (p.countby == 1 && sp >= clampi (p.pulses, 1, 64)) { st = firstStage (p); pl = 0; sp = 0; pingDir = 1; resetLayers (p); }
                else if (pl >= p.s.len[(size_t) st]) { st = nextStage (p, st); pl = 0; advanceLayers (p); }
            }
            const int ip = lidx (p, 0, st), iv = lidx (p, 1, st), ig = lidx (p, 2, st), ic = lidx (p, 3, st);
            const int m = p.s.mode[(size_t) st], d = clampi (p.s.div[(size_t) st], 0, 3) + 1;
            const double g = clampi (p.s.gate[(size_t) ig], 5, 100) / 100.0;
            bool fire = false; double dur = 0;
            if (m == 1 && pl == 0) { fire = true; dur = g * pb; }
            else if (m == 2 && pl % d == 0) { fire = true; dur = g * pb * d; }
            else if (m == 3 && pl == 0) { fire = true; dur = p.s.len[(size_t) st] * pb * g; }
            if (! fire) continue;
            int an = -1;
            if (p.playmode == Arp) { an = nextArpNote (p); if (an < 0) continue; }
            const int n = stagePitch (p, ip, an);
            if (n < 0) continue;
            const double start = gp * pb + (gp % 2 == 1 ? sw : 0.0);
            clip.notes.push_back ({ n, start, std::max (0.01, std::min (dur, bars * 4 - start)),
                                    clampi (p.s.vel[(size_t) iv], 1, 127), clampi (p.s.chance[(size_t) ic], 0, 100) / 100.0 });
        }
        clip.lengthQ = bars * 4;
        return clip;
    }

    // ---- state
    double sampleRate = 44100.0, tempo = 120.0;
    bool wasPlaying = false, running = false, resumable = false, restartPending = false, learnArmed = false;
    int stage = 0, pulse = 0, seqPulse = 0, pingDir = 1, lastAux = -1, arpIdx = 0;
    bool stageRoll = true;
    int64_t pulseCount = 0, tickAccum = 0, lastGrid = INT64_MIN;
    double lastGridPpq = 0, expectedPpq = 0;
    std::array<int, kNumLayers> lc {}, ld { 1, 1, 1, 1, 1 };
    std::vector<int> held, latched;
    double arpSeed = 1;
    int bassOverride = -1;
    const Snapshot* pendingSlot = nullptr;
    std::vector<PendingNote> pendingNotes;
    std::vector<PendingAux> pendingAux;
    std::vector<Active> active;
    std::mt19937 rng { 0x7461u };
};

// ===========================================================================
// Editing operations (message thread): randomize, evolve, nudge, seq reset.
// They work on a Snapshot plus the settings they need, exactly as the JS does.
// ===========================================================================
struct EditContext
{
    int layer = 0, playmode = Seq, randtarget = 0, pool = 0, notes = 8, octjump = 0, evotarget = 0;
    int scalemode = 0, root = 0, scale = 1;
    std::array<int, 12> custom { 1,0,0,1,0,0,0,1,0,0,0,0 };
};

class Editor
{
public:
    explicit Editor (uint32_t seed = 0x1234) : rng (seed) {}

    Params paramsFor (const EditContext& c, const Snapshot& s) const
    {
        Params p; p.s = s; p.scalemode = c.scalemode; p.root = c.root; p.scale = c.scale; return p;
    }
    std::vector<int> poolIntervals (const EditContext& c) const
    {
        switch (clampi (c.pool, 0, 7))
        {
            case 0: return {};                     // Classic: any note
            case 1: return { 0 };
            case 2: return { 0, 7 };
            case 3: return { 0, 1 };
            case 4: return { 0, 1, 3 };
            case 5: return { 0, 1, 7, 12 };
            case 6:
                if (c.scalemode == 1) return scales()[(size_t) clampi (c.scale, 0, 15)].iv;
                return { 0,1,2,3,4,5,6,7,8,9,10,11 };
            default:
            {
                std::vector<int> v; for (int i = 0; i < 12; ++i) if (c.custom[(size_t) i]) v.push_back (i);
                return v.empty() ? std::vector<int> { 0 } : v;
            }
        }
    }
    // a pool note outside the scale moves UP to the next scale note
    int fitUp (const EditContext& c, const Snapshot& s, int v) const
    {
        if (c.scalemode != 1) return v;
        ActiveScale sc { true, c.root, &scales()[(size_t) clampi (c.scale, 0, 15)].iv };
        const int n = 60 + v + s.transpose;
        for (int k = 0; k < 12; ++k) if (inScale (sc, n + k)) return v + k;
        return v;
    }
    std::vector<int> pitchChoices (const EditContext& c, const Snapshot& s)
    {
        const int N = clampi (c.notes, 1, 8);
        if (c.pool == 0)
        {
            std::vector<int> all; for (int i = -36; i <= 36; ++i) all.push_back (i);
            shuffle (all); all.resize ((size_t) N); return all;
        }
        const auto iv = poolIntervals (c);
        const int root = c.scalemode == 1 ? c.root : 0;
        const bool custom = c.pool == 7 || c.pool == 6;
        std::vector<int> ch;
        for (int x : iv) { const int v = fitUp (c, s, (custom ? 0 : root) + x); if (std::find (ch.begin(), ch.end(), v) == ch.end()) ch.push_back (v); }
        if ((int) ch.size() > N)
        {
            if (custom) { shuffle (ch); ch.resize ((size_t) N); }
            else
            {
                std::vector<int> rest (ch.begin() + 1, ch.end()); shuffle (rest); rest.resize ((size_t) N - 1);
                std::vector<int> o { ch[0] }; o.insert (o.end(), rest.begin(), rest.end()); ch = o;
            }
        }
        return ch;
    }
    std::array<int, NS> randPitches (const EditContext& c, const Snapshot& s)
    {
        auto ch = pitchChoices (c, s);
        std::array<int, NS> out {};
        for (int i = 0; i < NS; ++i)
        {
            int v = pick (ch);
            if (c.octjump > 0 && rnd() * 100 < c.octjump) v += rnd() < 0.5 ? -12 : 12;
            out[(size_t) i] = clampi (v, -36, 36);
        }
        if (c.notes > 1 && ch.size() > 1)
        {
            bool same = true;
            for (int k = 1; k < NS; ++k) if (out[(size_t) k] % 12 != out[0] % 12) same = false;   // JS remainder semantics
            if (same)
            {
                std::vector<int> others; for (int x : ch) if (x != out[0]) others.push_back (x);
                if (! others.empty()) out[(size_t) rint (1, NS - 1)] = pick (others);
            }
        }
        return out;
    }
    void randRhythm (Snapshot& s)
    {
        for (int i = 0; i < NS; ++i)
        {
            s.mode[(size_t) i]   = weighted ({ 0, 1, 2, 3 }, { 15, 50, 20, 15 });
            s.div[(size_t) i]    = weighted ({ 0, 1, 2, 3 }, { 60, 25, 8, 7 });
            s.len[(size_t) i]    = weighted ({ 1, 2, 3, 4 }, { 45, 30, 15, 10 });
            s.gate[(size_t) i]   = weighted ({ 25, 50, 75, 100 }, { 30, 35, 20, 15 });
            s.chance[(size_t) i] = weighted ({ 100, 90, 75, 50 }, { 65, 15, 12, 8 });
        }
        const int L = clampi (s.length, 1, NS);
        int sounding = 0;
        for (int i = 0; i < L; ++i) if (s.mode[(size_t) i] != 0) ++sounding;
        if (sounding < std::min (3, L))
        {
            s.mode[0] = 1;
            if (L > 2) s.mode[(size_t) rint (1, (L - 1) / 2)] = 1;
            if (L > 1) s.mode[(size_t) rint ((L - 1) / 2 + 1, L - 1)] = 2;
        }
    }
    void randLayer (const EditContext& c, Snapshot& s, int L)
    {
        if (L == LPitch)
        {
            if (c.playmode == Bass)
                for (auto& o : s.oct) o = (c.octjump > 0 && rnd() * 100 < std::max (c.octjump, 25)) ? pick ({ -1, 1, 1, 2 }) : 0;
            else s.pitch = randPitches (c, s);
        }
        else if (L == LVel)
            for (auto& v : s.vel) { const int band = weighted ({ 0, 1, 2 }, { 30, 45, 25 }); v = band == 0 ? rint (110, 127) : (band == 1 ? rint (80, 109) : rint (40, 79)); }
        else if (L == LGate)   for (auto& g : s.gate) g = rint (2, 20) * 5;
        else if (L == LChance) for (auto& ch : s.chance) ch = weighted ({ 100, 90, 75, 50, 25 }, { 55, 15, 15, 10, 5 });
        else if (L == LAux)    for (auto& a : s.aux) a = rint (0, 127);
    }
    // 0 Layer (dials shown), 1 Rhythm, 2 All (every dial except chance)
    void randomize (const EditContext& c, Snapshot& s)
    {
        if (c.randtarget == 0) randLayer (c, s, c.layer);
        else if (c.randtarget == 1) randRhythm (s);
        else { randLayer (c, s, 0); randLayer (c, s, 1); randLayer (c, s, 2); randLayer (c, s, 4); }
    }
    int evolve (const EditContext& c, Snapshot& s)
    {
        const int t = c.evotarget == 2 ? (rnd() < 0.5 ? 0 : 1) : c.evotarget;
        std::vector<int> cand;
        for (int i = 0; i < clampi (s.length, 1, NS); ++i) if (s.mode[(size_t) i] != 0 && s.len[(size_t) i] > 0) cand.push_back (i);
        if (cand.empty()) cand.push_back (0);
        int st = pick (cand);
        if (t == 0 && s.llen[0] > 0) st = rint (0, clampi (s.llen[0], 1, NS) - 1);
        if (t == 0)
        {
            if (c.playmode == Bass) s.oct[(size_t) st] = s.oct[(size_t) st] == 0 ? pick ({ -1, 1 }) : 0;
            else
            {
                std::vector<int> used;
                for (int k = 0; k < NS; ++k) if (std::find (used.begin(), used.end(), s.pitch[(size_t) k]) == used.end()) used.push_back (s.pitch[(size_t) k]);
                std::vector<int> ch;
                if (c.pool == 0) { ch = used; ch.push_back (s.pitch[(size_t) st] + pick ({ -2, -1, 1, 2 })); }
                else { ch = pitchChoices (c, s); ch.insert (ch.end(), used.begin(), used.end()); }
                std::vector<int> alt; for (int x : ch) if (x != s.pitch[(size_t) st]) alt.push_back (x);
                if (! alt.empty()) s.pitch[(size_t) st] = clampi (pick (alt), -36, 36);
            }
        }
        else
        {
            const int what = rint (0, 3);
            if (what == 0) { std::vector<int> o; for (int x : { 1, 2, 3 }) if (x != s.mode[(size_t) st]) o.push_back (x); s.mode[(size_t) st] = pick (o); }
            else if (what == 1) s.len[(size_t) st] = clampi (s.len[(size_t) st] + pick ({ -1, 1 }), 1, 4);
            else if (what == 2) s.div[(size_t) st] = (s.div[(size_t) st] + 1) % 3;
            else { std::vector<int> o; for (int x : { 25, 50, 75, 100 }) if (x != s.gate[(size_t) st]) o.push_back (x); s.gate[(size_t) st] = pick (o); }
        }
        return st;
    }
    // rotate the active stages; unlinked layers turn within their own steps
    static void nudge (Snapshot& s, int d)
    {
        const int L = clampi (s.length, 1, NS);
        if (L < 2) return;
        auto rot = [d] (std::array<int, NS>& a, int n)
        {
            if (n < 2) return;
            if (d > 0) std::rotate (a.begin(), a.begin() + n - 1, a.begin() + n);
            else       std::rotate (a.begin(), a.begin() + 1, a.begin() + n);
        };
        auto eff = [&] (int l) { return s.llen[(size_t) l] > 0 ? clampi (s.llen[(size_t) l], 1, NS) : L; };
        rot (s.pitch, eff (0)); rot (s.oct, eff (0)); rot (s.vel, eff (1)); rot (s.gate, eff (2));
        rot (s.chance, eff (3)); rot (s.aux, eff (4)); rot (s.mode, L); rot (s.div, L); rot (s.len, L);
    }
    static void seqReset (Snapshot& s)
    {
        s.resetStages(); s.length = 8; s.direction = Fwd; s.llen.fill (0);
    }

private:
    int rint (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }
    double rnd() { return std::uniform_real_distribution<double> (0.0, 1.0) (rng); }
    int pick (const std::vector<int>& a) { return a[(size_t) rint (0, (int) a.size() - 1)]; }
    void shuffle (std::vector<int>& a) { for (int i = (int) a.size() - 1; i > 0; --i) std::swap (a[(size_t) i], a[(size_t) rint (0, i)]); }
    int weighted (const std::vector<int>& items, const std::vector<int>& w)
    {
        int t = 0; for (int x : w) t += x;
        double r = rnd() * t;
        for (size_t i = 0; i < items.size(); ++i) { r -= w[i]; if (r < 0) return items[i]; }
        return items.back();
    }
    std::mt19937 rng;
};

} // namespace tgb
