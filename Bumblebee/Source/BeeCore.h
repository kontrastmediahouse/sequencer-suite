// BeeCore.h - Bumblebee sequencer engine (framework-free)
//
// One engine that fuses TGB-420 (stages with pulses, modes, ratchets, chance, layers,
// Seq/Bass/Arp, pools, history, patterns, evolve) with Snake (4x4 grid, 14 snake paths,
// shift windows, per-lane length/direction/speed, scale-degree pitch, user scale, CC lanes).
//
// With path S1, no shift, all layers linked, Semitone pitch, scale Off and MOD B-D off,
// it is note-for-note the TGB-420 engine (checked against the original tgb420.js).
#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>

namespace bee
{
constexpr int NS = 16;
constexpr int TPQ = 480;
constexpr int kNumShapes = 14;
constexpr int kNumMods = 4;
enum Layer { LPitch = 0, LVel, LGate, LChance, LModA, LModB, LModC, LModD, kNumLayers };
enum PlayMode { Seq = 0, Bass, Arp };
enum Direction { Fwd = 0, Rev, Ping, Rand };
enum ModKind { ModCC = 0, ModBend, ModPressure };

inline int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- paths (Snake)
// entry [cell] = the position (1..16) at which that cell plays; cells row by row
inline const std::array<std::array<int, 16>, kNumShapes>& shapeTable()
{
    static const std::array<std::array<int, 16>, kNumShapes> t {{
        {{  1, 2, 3, 4,  5, 6, 7, 8,  9,10,11,12, 13,14,15,16 }}, {{ 13,14,15,16,  9,10,11,12,  5, 6, 7, 8,  1, 2, 3, 4 }},
        {{  1, 5, 9,13,  2, 6,10,14,  3, 7,11,15,  4, 8,12,16 }}, {{  4, 8,12,16,  3, 7,11,15,  2, 6,10,14,  1, 5, 9,13 }},
        {{  1, 8, 9,16,  2, 7,10,15,  3, 6,11,14,  4, 5,12,13 }}, {{  4, 5,12,13,  3, 6,11,14,  2, 7,10,15,  1, 8, 9,16 }},
        {{  1, 2, 3, 4,  8, 7, 6, 5,  9,10,11,12, 16,15,14,13 }}, {{  4, 3, 2, 1,  5, 6, 7, 8, 12,11,10, 9, 13,14,15,16 }},
        {{  1, 2, 6, 7,  3, 5, 8,13,  4, 9,12,14, 10,11,15,16 }}, {{ 10,11,15,16,  4, 9,12,14,  3, 5, 8,13,  1, 2, 6, 7 }},
        {{  1, 2, 3, 4, 12,13,14, 5, 11,16,15, 6, 10, 9, 8, 7 }}, {{  4, 3, 2, 1,  5,14,13,12,  6,15,16,11,  7, 8, 9,10 }},
        {{ 10, 9, 8, 7, 11,16,15, 6, 12,13,14, 5,  1, 2, 3, 4 }}, {{  7, 8, 9,10,  6,15,16,11,  5,14,13,12,  4, 3, 2, 1 }} }};
    return t;
}
inline const char* shapeName (int s)
{
    static const char* n[] = { "S1","S1R","S2","S2R","S3","S3R","S4","S4R","S5","S5R","S6a","S6b","S6c","S6d" };
    return n[clampi (s, 0, kNumShapes - 1)];
}
inline int cellForPathPosition (int shape, int pos)
{
    const auto& row = shapeTable()[(size_t) clampi (shape, 0, kNumShapes - 1)];
    for (int c = 0; c < 16; ++c) if (row[(size_t) c] == pos + 1) return c;
    return 0;
}
inline int windowOffset (int shiftPercent, int length)
{
    const int room = 16 - clampi (length, 1, 16);
    return clampi ((int) ((float) clampi (shiftPercent, 0, 100) * (float) room / 100.0f), 0, room);
}

// ---------------------------------------------------------------- scales
// 0 = Off (chromatic, no snapping); 1..36 = Snake's scales; 37 = natural minor; 38 = User
struct ScaleDef { const char* name; std::vector<int> pcs; };
inline const std::vector<ScaleDef>& scaleList()
{
    static const std::vector<ScaleDef> s {
        { "Off", {} },
        { "Chromatic", {0,1,2,3,4,5,6,7,8,9,10,11} }, { "Major", {0,2,4,5,7,9,11} }, { "Melodic Minor*", {0,2,3,5,7,9,11} },
        { "Dorian", {0,2,3,5,7,9,10} }, { "Mixolydian", {0,2,4,5,7,9,10} }, { "Lydian", {0,2,4,6,7,9,11} },
        { "Phrygian", {0,1,3,5,7,8,10} }, { "Locrian", {0,1,3,5,6,8,10} }, { "Whole Tone", {0,2,4,6,8,10} },
        { "Half-Whole Dim", {0,1,3,4,6,7,9,10} }, { "Whole-Half Dim", {0,2,3,5,6,8,9,11} }, { "Minor Blues", {0,3,5,6,7,10} },
        { "Minor Pentatonic", {0,3,5,7,10} }, { "Major Pentatonic", {0,2,4,7,9} }, { "Harmonic Minor", {0,2,3,5,7,8,11} },
        { "Harmonic Major", {0,2,4,5,7,8,11} }, { "Dorian #4", {0,2,3,6,7,9,10} }, { "Phrygian Dominant", {0,1,4,5,7,8,10} },
        { "Melodic Minor", {0,2,3,5,7,9,11} }, { "Lydian Augmented", {0,2,4,6,8,9,11} }, { "Lydian Dominant", {0,2,4,6,7,9,10} },
        { "Super Locrian", {0,1,3,4,6,8,10} }, { "8-Tone Spanish", {0,1,3,4,5,6,8,10} }, { "Bhairav", {0,1,4,5,7,8,11} },
        { "Hungarian Minor", {0,2,3,6,7,8,11} }, { "Hirajoshi", {0,2,3,7,8} }, { "In Sen", {0,1,5,7,10} },
        { "Iwato", {0,1,5,6,10} }, { "Kumoi", {0,2,3,7,9} }, { "Pelog Selisir", {0,1,3,7,8} }, { "Pelog Tembung", {0,1,5,7,8} },
        { "Messiaen 3", {0,2,3,4,6,7,8,10,11} }, { "Messiaen 4", {0,1,2,5,6,7,8,11} }, { "Messiaen 5", {0,1,5,6,7,11} },
        { "Messiaen 6", {0,2,4,5,6,8,10,11} }, { "Messiaen 7", {0,1,2,3,5,6,7,8,9,11} },
        { "Natural Minor", {0,2,3,5,7,8,10} },
        { "User", {} } };
    return s;
}
constexpr int kScaleOff = 0, kScaleUser = 38;
inline int scaleCount() { return (int) scaleList().size(); }

inline const char* arpName (int i)
{
    static const char* n[] = { "Up", "Down", "UpDown", "DownUp", "Up & Down", "Down & Up", "Converge", "Diverge",
                               "Con & Diverge", "Pinky Up", "Pinky UpDown", "Thumb Up", "Thumb UpDown",
                               "Play Order", "Random", "Random Other", "Random Once" };
    return n[clampi (i, 0, 16)];
}
inline const char* poolName (int i)
{
    static const char* n[] = { "Classic", "One Note", "Power", "Tension", "Hypnotic", "Dark", "Scale", "Custom" };
    return n[clampi (i, 0, 7)];
}
inline std::string noteName (int n)
{
    static const char* nn[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return std::string (nn[((n % 12) + 12) % 12]) + std::to_string ((int) std::floor (n / 12.0) - 2);
}
inline double clockQuarters (int clock) { static const double q[] = { 0.125, 0.25, 0.5, 1.0 }; return q[clampi (clock, 0, 3)]; }
inline int clockTicks (int clock)       { static const int t[] = { 60, 120, 240, 480 }; return t[clampi (clock, 0, 3)]; }
// layer Rate: 0 = Stage (advance once per stage, TGB), 1..5 = 1/32, 1/16, 1/8, 1/4, 1/2 (free running, Snake)
inline double rateQuarters (int rate) { static const double q[] = { 0, 0.125, 0.25, 0.5, 1.0, 2.0 }; return q[clampi (rate, 0, 5)]; }

// ---------------------------------------------------------------- data
struct LayerCfg
{
    int steps = 0;   // 0 = linked to Stages
    int path  = -1;  // -1 = follow the sequence path
    int shift = 0;   // 0..100 %
    int dir   = -1;  // -1 = follow the sequence direction
    int rate  = 0;   // 0 = per stage
    bool linked() const { return steps == 0 && rate == 0 && dir < 0; }
    bool operator== (const LayerCfg& o) const { return steps == o.steps && path == o.path && shift == o.shift && dir == o.dir && rate == o.rate; }
};

struct Snapshot   // a pattern: what slots, undo and Seq Reset work on
{
    std::array<int, NS> pitch {}, oct {}, vel {}, gate {}, chance {}, mode {}, div {}, len {};
    std::array<std::array<int, NS>, kNumMods> mod {};
    int length = 8, direction = Fwd, transpose = 0, path = 0, shift = 0;
    std::array<LayerCfg, kNumLayers> layers {};

    Snapshot() { resetStages(); }
    void resetStages()
    {
        pitch.fill (0); oct.fill (0); vel.fill (100); gate.fill (50); chance.fill (100);
        mode.fill (1); div.fill (0); len.fill (1);
        for (auto& m : mod) m.fill (0);
    }
    std::array<int, NS>& layerValues (int l)
    {
        switch (l) { case LPitch: return pitch; case LVel: return vel; case LGate: return gate; case LChance: return chance; default: return mod[(size_t) (l - LModA)]; }
    }
    bool operator== (const Snapshot& o) const
    {
        return pitch == o.pitch && oct == o.oct && vel == o.vel && gate == o.gate && chance == o.chance && mode == o.mode
            && div == o.div && len == o.len && mod == o.mod && length == o.length && direction == o.direction
            && transpose == o.transpose && path == o.path && shift == o.shift && layers == o.layers;
    }
};

struct ModOut { bool on = false; int kind = ModCC, cc = 74, lo = 0, hi = 127, glide = 0; };

struct Params
{
    Snapshot s;
    int playmode = Seq, clock = 1, swing = 0, bassnote = 36, retrig = 1, arporder = 0, arpoct = 1, latch = 0;
    int countby = 0, pulses = 16;
    int scale = kScaleOff, root = 0, pitchMode = 0;   // pitchMode 0 Semitone, 1 Degree
    uint16_t userMask = 1;
    int chroll = 0;
    std::array<ModOut, kNumMods> mods {};
    int resetstop = 1, evolve = 0, midiChannel = 1;
    Params() { mods[0].on = true; for (int m = 1; m < kNumMods; ++m) mods[(size_t) m].cc = 19 + m; }
};

struct MidiOut { int sampleOffset; uint8_t b0, b1, b2; int size; };

// ---------------------------------------------------------------- scale helpers
struct ActiveScale { bool on = false; int root = 0; int pcs[12] {}; int n = 0; };
inline ActiveScale activeScale (int scale, int root, uint16_t userMask)
{
    ActiveScale a;
    if (scale <= kScaleOff) return a;
    a.on = true; a.root = root;
    if (scale >= kScaleUser)
    {
        for (int i = 0; i < 12; ++i) if (userMask & (1u << i)) a.pcs[a.n++] = i;
        if (a.n == 0) a.pcs[a.n++] = 0;
    }
    else for (int v : scaleList()[(size_t) clampi (scale, 1, kScaleUser - 1)].pcs) a.pcs[a.n++] = v;
    return a;
}
inline ActiveScale activeScale (const Params& p) { return activeScale (p.scale, p.root, p.userMask); }
inline bool inScale (const ActiveScale& sc, int n)
{
    const int pc = ((n - sc.root) % 12 + 12) % 12;
    for (int i = 0; i < sc.n; ++i) if (sc.pcs[i] == pc) return true;
    return false;
}
inline int snap (const ActiveScale& sc, int n)   // nearest scale note, ties go down (TGB-420)
{
    if (! sc.on) return n;
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (sc, n - d)) return n - d;
        if (d > 0 && inScale (sc, n + d)) return n + d;
    }
    return n;
}
// Degree mode (after Snake): the dial counts scale notes up/down from the root at C3
inline int degreeNote (const ActiveScale& sc, int degree)
{
    if (! sc.on) return 60 + degree;
    const int N = sc.n;
    const int oct = (int) std::floor ((double) degree / N);
    const int d = degree - oct * N;
    return 60 + sc.root + 12 * oct + sc.pcs[d];
}

// ---------------------------------------------------------------- arp helpers (TGB-420)
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

// Loop index for a free-running (Snake-style) layer
template <typename Rng>
inline int freeIndex (int64_t count, int dir, int length, Rng& rng)
{
    length = clampi (length, 1, 16);
    if (length == 1) return 0;
    auto pmod = [] (int64_t a, int64_t m) { auto r = a % m; return (int) (r < 0 ? r + m : r); };
    switch (dir)
    {
        case Rev:  return length - 1 - pmod (count, length);
        case Ping: { const int period = 2 * length - 2, m = pmod (count, period); return m < length ? m : period - m; }
        case Rand: return (int) (rng() % (uint32_t) length);
        default:   return pmod (count, length);
    }
}

// ===========================================================================
class Engine
{
public:
    // UI read-outs (cells are grid cells 0..15)
    int uiCell = -1, uiPulse = 0;
    std::array<int, kNumLayers> uiLayerCell { -1, -1, -1, -1, -1, -1, -1, -1 };
    int uiHitCell = -1, hitCounter = 0;
    bool evolveRequested = false, slotFired = false;
    bool learnedCC = false; int learnedCCNumber = 0;
    struct Trigger { double ppq; double durQ; int vel; };
    std::vector<Trigger> triggers;

    void prepare (double sr) { sampleRate = sr; reset(); triggers.reserve (256); }
    void setArpSeed (double s) { arpSeed = s; }
    void setPendingSlot (const Snapshot* s) { pendingSlot = s; }
    void armLearn (bool on) { learnArmed = on; }
    void ccIn (int num) { if (learnArmed) { learnArmed = false; learnedCC = true; learnedCCNumber = num; } }
    void playModeChanged() { bassOverride = -1; held.clear(); latched.clear(); }
    void latchOff() { latched.clear(); }
    bool hasHeldNotes (const Params& p) const { return ! (p.latch ? latched : held).empty(); }

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

    template <typename Emit>
    void process (Params& p, bool playing, double ppqStart, double bpm, int numSamples, Emit&& emit)
    {
        evolveRequested = false; slotFired = false; triggers.clear();
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
        else if (std::abs (ppqStart - expectedPpq) > 1e-3) { allNotesOff (0, p, emit); lastGrid = INT64_MIN; }
        wasPlaying = true;
        expectedPpq = ppqEnd;

        int64_t k = (int64_t) std::ceil ((ppqStart - eps) / pq);
        if (lastGrid != INT64_MIN && k <= lastGrid && ppqStart >= lastGridPpq) k = lastGrid + 1;
        for (; (double) k * pq < ppqEnd - eps; ++k)
        {
            lastGrid = k; lastGridPpq = (double) k * pq;
            curPulsePpq = (double) k * pq;
            Pulse out;
            simTick (p, k, out);
            const double t = (double) k * pq;
            if (out.hasNote) pendingNotes.push_back ({ t + out.delayQ, out.pitch, out.vel, out.durQ });
            for (int m = 0; m < kNumMods; ++m)
                if (out.hasMod[(size_t) m]) pendingMods.push_back ({ m, t + out.delayQ, out.modValue[(size_t) m], out.modRampQ[(size_t) m] });
        }

        struct Ev { double t; int kind; int pitch; int vel; double dur; };
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
            for (auto a = active.begin(); a != active.end(); ++a)
                if (a->pitch == e.pitch) { emit (MidiOut { toSample (e.t), (uint8_t) (0x80 | ch), (uint8_t) e.pitch, 0, 3 }); active.erase (a); break; }
            emit (MidiOut { toSample (e.t), (uint8_t) (0x90 | ch), (uint8_t) e.pitch, (uint8_t) e.vel, 3 });
            active.push_back ({ e.pitch, e.t + e.dur });
            triggers.push_back ({ e.t, e.dur, e.vel });
        }
        std::sort (active.begin(), active.end(), [] (const Active& a, const Active& b) { return a.offQ < b.offQ; });
        while (! active.empty() && active.front().offQ < ppqEnd - eps)
        {
            emit (MidiOut { toSample (active.front().offQ), (uint8_t) (0x80 | ch), (uint8_t) active.front().pitch, 0, 3 });
            active.erase (active.begin());
        }
        processMods (p, ppqStart, ppqEnd, spq, numSamples, ch, emit);
    }

    template <typename Emit>
    void allNotesOff (int so, const Params& p, Emit&& emit)
    {
        const uint8_t ch = (uint8_t) (clampi (p.midiChannel, 1, 16) - 1);
        for (auto& a : active) emit (MidiOut { so, (uint8_t) (0x80 | ch), (uint8_t) a.pitch, 0, 3 });
        active.clear(); pendingNotes.clear(); pendingMods.clear();
        for (auto& r : ramps) r.active = false;
    }

    // ---- MIDI clip (one full cycle)
    struct ClipNote { int pitch; double start, dur; int vel; double prob; };
    struct Clip { std::vector<ClipNote> notes; double lengthQ = 0; bool capped = false; };
    Clip renderClip (const Params& p) const { Engine t (*this); return t.renderClipInner (p); }

    // ---- shared helpers (also used by the UI)
    static int stageCount (const Params& p) { return clampi (p.s.length, 1, NS); }
    static int cellAt (const Params& p, int pos)    // sequence position -> grid cell
    {
        return cellForPathPosition (p.s.path, clampi (pos + windowOffset (p.s.shift, stageCount (p)), 0, 15));
    }
    static int effLen (const Params& p, int l)
    {
        const auto& c = p.s.layers[(size_t) l];
        return c.steps > 0 ? clampi (c.steps, 1, NS) : stageCount (p);
    }

private:
    struct Pulse
    {
        bool hasNote = false; int pitch = 0, vel = 0; double durQ = 0, delayQ = 0;
        std::array<bool, kNumMods> hasMod {}; std::array<int, kNumMods> modValue {}; std::array<double, kNumMods> modRampQ {};
    };
    struct PendingNote { double onQ; int pitch, vel; double durQ; };
    struct PendingMod  { int lane; double atQ; int value; double rampQ; };
    struct Active { int pitch; double offQ; };

    int rint (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }
    double rnd() { return std::uniform_real_distribution<double> (0.0, 1.0) (rng); }
    static int layerDir (const Params& p, int l) { const int d = p.s.layers[(size_t) l].dir; return d >= 0 ? d : p.s.direction; }

    // grid cell a layer reads at sequence position pos (pulse time t)
    int layerCell (const Params& p, int l, int pos, double t)
    {
        const auto& c = p.s.layers[(size_t) l];
        const int shape = c.path >= 0 ? c.path : p.s.path;
        if (c.linked())
            return cellForPathPosition (shape, clampi (pos + windowOffset (p.s.shift, stageCount (p)), 0, 15));
        const int n = effLen (p, l);
        int idx;
        if (c.rate > 0)
        {
            const auto count = (int64_t) std::floor (t / rateQuarters (c.rate) + 1e-9);
            if (count != freeCount[(size_t) l]) { freeCount[(size_t) l] = count; freeIdx[(size_t) l] = freeIndex (count, layerDir (p, l), n, rng); }
            idx = freeIdx[(size_t) l];
        }
        else idx = lc[(size_t) l] % n;
        return cellForPathPosition (shape, clampi (idx + windowOffset (c.shift, n), 0, 15));
    }
    bool counterLayer (const Params& p, int l) const { const auto& c = p.s.layers[(size_t) l]; return ! c.linked() && c.rate == 0; }

    void resetLayers (const Params& p)
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            const int n = effLen (p, l), d = layerDir (p, l);
            lc[(size_t) l] = d == Rev ? n - 1 : (d == Rand ? rint (0, n - 1) : 0);
            ld[(size_t) l] = 1;
        }
    }
    void advanceLayers (const Params& p)
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            if (! counterLayer (p, l)) continue;
            const int n = effLen (p, l), dir = layerDir (p, l); int c = lc[(size_t) l] % n;
            if (dir == Rev) c = (c - 1 + n) % n;
            else if (dir == Ping)
            {
                if (n == 1) c = 0;
                else { if (c + ld[(size_t) l] >= n || c + ld[(size_t) l] < 0) ld[(size_t) l] = -ld[(size_t) l]; c += ld[(size_t) l]; }
            }
            else if (dir == Rand) c = rint (0, n - 1);
            else c = (c + 1) % n;
            lc[(size_t) l] = c;
        }
    }
    static bool playable (const Params& p, int pos) { return p.s.len[(size_t) cellAt (p, pos)] > 0; }
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
    static int firstStageNoRandom (const Params& p)
    {
        const int L = stageCount (p);
        if (p.s.direction == Rev) { for (int i = L - 1; i >= 0; --i) if (playable (p, i)) return i; }
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
    int stagePitch (const Params& p, int cell, int arpNote) const
    {
        const auto sc = activeScale (p);
        int n;
        if (p.playmode == Bass) n = (bassOverride >= 0 ? bassOverride : p.bassnote) + 12 * p.s.oct[(size_t) cell] + p.s.transpose;
        else if (p.playmode == Arp) { if (arpNote < 0) return -1; n = arpNote + p.s.pitch[(size_t) cell] + p.s.transpose; }
        else if (p.pitchMode == 1) n = degreeNote (sc, p.s.pitch[(size_t) cell]) + p.s.transpose;
        else n = 60 + p.s.pitch[(size_t) cell] + p.s.transpose;
        return clampi (snap (sc, n), 0, 127);
    }

    // ---- tick
    void stepPosition (Params& p)
    {
        ++pulse; ++seqPulse;
        const bool endOfStage = pulse >= p.s.len[(size_t) cellAt (p, stage)];
        const bool endOfSeq = p.countby == 1 && seqPulse >= clampi (p.pulses, 1, 64);
        if (endOfSeq) { stage = firstStage (p); pulse = 0; seqPulse = 0; pingDir = 1; resetLayers (p); }
        else if (endOfStage) { stage = nextStage (p, stage); pulse = 0; advanceLayers (p); }
        if (pulse == 0 && isCycleStart (p, stage)) onCycleStart (p);
    }
    void onCycleStart (Params& p)
    {
        if (pendingSlot) { p.s = *pendingSlot; pendingSlot = nullptr; slotFired = true; }
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
                running = true;
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
    void playPulse (Params& p, int pos, int pl, int64_t gridIndex, Pulse& out)
    {
        const double pq = clockQuarters (p.clock);
        const double msQ = tempo / 60000.0;
        const double t = curPulsePpq;
        const double delay = ((gridIndex & 1) && p.swing > 0) ? p.swing / 100.0 * pq * 0.5 : 0.0;
        const int cell = cellAt (p, pos);
        std::array<int, kNumLayers> lcell {};
        for (int l = 0; l < kNumLayers; ++l) lcell[(size_t) l] = layerCell (p, l, pos, t);
        uiCell = cell; uiPulse = pl;
        for (int l = 0; l < kNumLayers; ++l) uiLayerCell[(size_t) l] = lcell[(size_t) l];

        // MOD lanes: per stage (stage-rate layers) or whenever their own clock moves on
        for (int m = 0; m < kNumMods; ++m)
        {
            const int l = LModA + m;
            const auto& c = p.s.layers[(size_t) l];
            bool step = pl == 0;
            if (c.rate > 0)
            {
                const auto count = (int64_t) std::floor (t / rateQuarters (c.rate) + 1e-9);
                step = count != lastModCount[(size_t) m];
                lastModCount[(size_t) m] = count;
            }
            if (! step || ! p.mods[(size_t) m].on) continue;
            const int v = p.s.mod[(size_t) m][(size_t) lcell[(size_t) l]];
            const double rampQ = p.mods[(size_t) m].glide ? (c.rate > 0 ? rateQuarters (c.rate) : p.s.len[(size_t) cell] * pq) : 0.0;
            if (v != lastMod[(size_t) m] || p.mods[(size_t) m].glide)
            {
                out.hasMod[(size_t) m] = true; out.modValue[(size_t) m] = v; out.modRampQ[(size_t) m] = rampQ;
                lastMod[(size_t) m] = v;
            }
        }
        if (pl == 0 && p.chroll == 1) stageRoll = rnd() * 100.0 < p.s.chance[(size_t) lcell[LChance]];
        out.delayQ = delay;

        const int m = p.s.mode[(size_t) cell];
        const int d = clampi (p.s.div[(size_t) cell], 0, 3) + 1;
        bool fire = false; double dur = 0;
        const double g = clampi (p.s.gate[(size_t) lcell[LGate]], 5, 100) / 100.0;
        const int len = p.s.len[(size_t) cell];
        if (m == 1 && pl == 0)          { fire = true; dur = g >= 1.0 ? pq + 8 * msQ : g * pq; }
        else if (m == 2 && pl % d == 0) { fire = true; dur = g >= 1.0 ? pq * d + 8 * msQ : g * pq * d; }
        else if (m == 3 && pl == 0)     { fire = true; dur = g >= 1.0 ? len * pq + 8 * msQ : len * pq * g; }
        if (! fire) return;

        const bool ok = p.chroll == 1 ? stageRoll : (rnd() * 100.0 < p.s.chance[(size_t) lcell[LChance]]);
        if (! ok) return;
        int an = -1;
        if (p.playmode == Arp) { an = nextArpNote (p); if (an < 0) return; }
        const int n = stagePitch (p, lcell[LPitch], an);
        if (n < 0) return;
        out.hasNote = true; out.pitch = n; out.vel = clampi (p.s.vel[(size_t) lcell[LVel]], 1, 127);
        out.durQ = std::max (5 * msQ, dur);
        uiHitCell = cell; ++hitCounter;
    }
    void stopped (const Params& p)
    {
        running = false; resumable = true;
        if (p.resetstop) { stage = 0; pulse = 0; seqPulse = 0; pulseCount = 0; tickAccum = 0; }
        lastMod.fill (-1); lastSent.fill (-1); lastModCount.fill (INT64_MIN); freeCount.fill (INT64_MIN);
        uiCell = -1;
        for (auto& x : uiLayerCell) x = -1;
    }
    void reset()
    {
        running = false; resumable = false; stage = 0; pulse = 0; seqPulse = 0; pulseCount = 0; tickAccum = 0;
        pingDir = 1; wasPlaying = false; uiCell = -1;
        lastMod.fill (-1); lastSent.fill (-1); lastModCount.fill (INT64_MIN); freeCount.fill (INT64_MIN);
        active.clear(); pendingNotes.clear(); pendingMods.clear();
        for (auto& r : ramps) r.active = false;
    }

    // ---- MOD output: [line] 5 ms grain -> [change] -> CC / bend / pressure
    struct Ramp { bool active = false; double t0 = 0, dur = 0, from = 0, to = 0; };
    std::array<Ramp, kNumMods> ramps {};
    std::array<double, kNumMods> modCur {};
    std::array<int, kNumMods> lastSent { -1, -1, -1, -1 };

    template <typename Emit>
    void processMods (const Params& p, double q0, double q1, double spq, int numSamples, uint8_t ch, Emit&& emit)
    {
        constexpr double eps = 1e-9;
        const double grainQ = 0.005 * tempo / 60.0;
        std::sort (pendingMods.begin(), pendingMods.end(), [] (const PendingMod& a, const PendingMod& b) { return a.atQ < b.atQ; });
        for (int m = 0; m < kNumMods; ++m)
        {
            const auto& mo = p.mods[(size_t) m];
            auto& ramp = ramps[(size_t) m];
            auto send = [&] (double q, double raw)
            {
                const int r = clampi ((int) raw, 0, 127);
                const int v = clampi ((int) std::lround (mo.lo + (mo.hi - mo.lo) * (r / 127.0)), 0, 127);
                if (v == lastSent[(size_t) m]) return;
                lastSent[(size_t) m] = v;
                const int so = clampi ((int) std::floor ((q - q0) * spq + 0.5), 0, numSamples - 1);
                if (mo.kind == ModBend)          emit (MidiOut { so, (uint8_t) (0xE0 | ch), 0, (uint8_t) v, 3 });
                else if (mo.kind == ModPressure) emit (MidiOut { so, (uint8_t) (0xD0 | ch), (uint8_t) v, 0, 2 });
                else                             emit (MidiOut { so, (uint8_t) (0xB0 | ch), (uint8_t) clampi (mo.cc, 0, 127), (uint8_t) v, 3 });
            };
            double t = q0;
            auto runRampUntil = [&] (double until)
            {
                if (! ramp.active) return;
                double gq = ramp.t0 + std::ceil ((t - ramp.t0) / grainQ + eps) * grainQ;
                for (; gq <= std::min (until, ramp.t0 + ramp.dur) + eps; gq += grainQ)
                {
                    const double f = ramp.dur > 0 ? std::min (1.0, (gq - ramp.t0) / ramp.dur) : 1.0;
                    modCur[(size_t) m] = ramp.from + (ramp.to - ramp.from) * f;
                    send (gq, modCur[(size_t) m]);
                }
                if (until >= ramp.t0 + ramp.dur - eps) { modCur[(size_t) m] = ramp.to; send (std::min (until, ramp.t0 + ramp.dur), modCur[(size_t) m]); ramp.active = false; }
            };
            for (auto it = pendingMods.begin(); it != pendingMods.end();)
            {
                if (it->lane != m || it->atQ >= q1 - eps) { ++it; continue; }
                const double at = std::max (it->atQ, q0);
                runRampUntil (at);
                t = at;
                if (it->rampQ <= 0) { ramp.active = false; modCur[(size_t) m] = it->value; send (at, modCur[(size_t) m]); }
                else { ramp = { true, at, it->rampQ, modCur[(size_t) m], (double) it->value }; send (at, modCur[(size_t) m]); }
                it = pendingMods.erase (it);
            }
            runRampUntil (q1 - eps);
        }
    }

    // ---- clip rendering
    static int gcd (int a, int b) { while (b) { int t = b; b = a % b; a = t; } return a ? a : 1; }
    Clip renderClipInner (const Params& pIn)
    {
        Params p = pIn;
        Clip clip;
        const double pb = clockQuarters (p.clock);
        const double sw = p.swing / 100.0 * pb * 0.5;
        pingDir = 1; arpIdx = 0; resetLayers (p);
        freeCount.fill (INT64_MIN);
        int cycle = 0; const int L = stageCount (p);
        if (p.countby == 1) cycle = clampi (p.pulses, 1, 64);
        else if (p.s.direction == Rand) cycle = 16;
        else
        {
            for (int k = 0; k < L; ++k) cycle += p.s.len[(size_t) cellAt (p, k)];
            if (p.s.direction == Ping) for (int k = 1; k < L - 1; ++k) cycle += p.s.len[(size_t) cellAt (p, k)];
        }
        if (cycle < 1) cycle = 1;
        double mult = 1;
        if (p.countby != 1 && p.s.direction != Rand)
        {
            int adv = 0, s0 = firstStage (p), q = 0;
            for (int tp = 0; tp < cycle; ++tp)
            {
                if (tp > 0) { ++q; if (q >= p.s.len[(size_t) cellAt (p, s0)]) { s0 = nextStage (p, s0); q = 0; } }
                if (q == 0) ++adv;
            }
            pingDir = 1;
            for (int ll = 0; ll < 4; ++ll)
            {
                if (! counterLayer (p, ll)) continue;
                const int per = layerDir (p, ll) == Ping ? 2 : 1;
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
                else if (pl >= p.s.len[(size_t) cellAt (p, st)]) { st = nextStage (p, st); pl = 0; advanceLayers (p); }
            }
            const double t = gp * pb;
            const int cell = cellAt (p, st);
            const int ip = layerCell (p, LPitch, st, t), iv = layerCell (p, LVel, st, t), ig = layerCell (p, LGate, st, t), ic = layerCell (p, LChance, st, t);
            const int m = p.s.mode[(size_t) cell], d = clampi (p.s.div[(size_t) cell], 0, 3) + 1;
            const double g = clampi (p.s.gate[(size_t) ig], 5, 100) / 100.0;
            bool fire = false; double dur = 0;
            if (m == 1 && pl == 0) { fire = true; dur = g * pb; }
            else if (m == 2 && pl % d == 0) { fire = true; dur = g * pb * d; }
            else if (m == 3 && pl == 0) { fire = true; dur = p.s.len[(size_t) cell] * pb * g; }
            if (! fire) continue;
            int an = -1;
            if (p.playmode == Arp) { an = nextArpNote (p); if (an < 0) continue; }
            const int n = stagePitch (p, ip, an);
            if (n < 0) continue;
            const double start = t + (gp % 2 == 1 ? sw : 0.0);
            clip.notes.push_back ({ n, start, std::max (0.01, std::min (dur, bars * 4 - start)),
                                    clampi (p.s.vel[(size_t) iv], 1, 127), clampi (p.s.chance[(size_t) ic], 0, 100) / 100.0 });
        }
        clip.lengthQ = bars * 4;
        return clip;
    }

    // ---- state
    double sampleRate = 44100.0, tempo = 120.0;
    bool wasPlaying = false, running = false, resumable = false, restartPending = false, learnArmed = false;
    int stage = 0, pulse = 0, seqPulse = 0, pingDir = 1, arpIdx = 0;
    bool stageRoll = true;
    int64_t pulseCount = 0, tickAccum = 0, lastGrid = INT64_MIN;
    double lastGridPpq = 0, expectedPpq = 0, curPulsePpq = 0;
    std::array<int, kNumLayers> lc {}, ld { 1, 1, 1, 1, 1, 1, 1, 1 }, freeIdx {};
    std::array<int64_t, kNumLayers> freeCount {};
    std::array<int, kNumMods> lastMod { -1, -1, -1, -1 };
    std::array<int64_t, kNumMods> lastModCount {};
    std::vector<int> held, latched;
    double arpSeed = 1;
    int bassOverride = -1;
    const Snapshot* pendingSlot = nullptr;
    std::vector<PendingNote> pendingNotes;
    std::vector<PendingMod> pendingMods;
    std::vector<Active> active;
    std::mt19937 rng { 0x6265u };
};

// ===========================================================================
// Editing (message thread): randomize, evolve, nudge, resets
// ===========================================================================
struct EditContext
{
    int layer = 0, playmode = Seq, randtarget = 0, pool = 0, notes = 8, octjump = 0, evotarget = 0;
    int scale = kScaleOff, root = 0, pitchMode = 0; uint16_t userMask = 1;
    std::array<int, 12> custom { 1,0,0,1,0,0,0,1,0,0,0,0 };
    std::array<int, kNumLayers> rlo { -36, 1, 5, 0, 0, 0, 0, 0 }, rhi { 36, 127, 100, 100, 127, 127, 127, 127 };
};

class Editor
{
public:
    explicit Editor (uint32_t seed = 0x1234) : rng (seed) {}

    std::vector<int> poolIntervals (const EditContext& c) const
    {
        const auto sc = activeScale (c.scale, c.root, c.userMask);
        switch (clampi (c.pool, 0, 7))
        {
            case 0: return {};
            case 1: return { 0 };
            case 2: return { 0, 7 };
            case 3: return { 0, 1 };
            case 4: return { 0, 1, 3 };
            case 5: return { 0, 1, 7, 12 };
            case 6: if (sc.on) return std::vector<int> (sc.pcs, sc.pcs + sc.n);
                    return { 0,1,2,3,4,5,6,7,8,9,10,11 };
            default:
            {
                std::vector<int> v; for (int i = 0; i < 12; ++i) if (c.custom[(size_t) i]) v.push_back (i);
                return v.empty() ? std::vector<int> { 0 } : v;
            }
        }
    }
    int fitUp (const EditContext& c, const Snapshot& s, int v) const
    {
        const auto sc = activeScale (c.scale, c.root, c.userMask);
        if (! sc.on) return v;
        const int n = 60 + v + s.transpose;
        for (int k = 0; k < 12; ++k) if (inScale (sc, n + k)) return v + k;
        return v;
    }
    // semitone offset -> the dial value that plays it (Degree mode stores scale degrees)
    int toDial (const EditContext& c, int semis) const
    {
        if (c.pitchMode != 1) return semis;
        const auto sc = activeScale (c.scale, c.root, c.userMask);
        if (! sc.on) return semis;
        const int target = 60 + semis;
        int best = 0, bestD = 999;
        for (int d = -36; d <= 36; ++d) { const int dd = std::abs (degreeNote (sc, d) - target); if (dd < bestD) { bestD = dd; best = d; } }
        return best;
    }
    std::vector<int> pitchChoices (const EditContext& c, const Snapshot& s)
    {
        const int N = clampi (c.notes, 1, 8);
        if (c.pool == 0)
        {
            std::vector<int> all; for (int i = c.rlo[0]; i <= c.rhi[0]; ++i) all.push_back (i);
            if (all.empty()) all.push_back (0);
            shuffle (all); all.resize ((size_t) std::min ((int) all.size(), N)); return all;
        }
        const auto iv = poolIntervals (c);
        const auto sc = activeScale (c.scale, c.root, c.userMask);
        const int root = sc.on ? c.root : 0;
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
            for (int k = 1; k < NS; ++k) if (out[(size_t) k] % 12 != out[0] % 12) same = false;
            if (same)
            {
                std::vector<int> others; for (int x : ch) if (x != out[0]) others.push_back (x);
                if (! others.empty()) out[(size_t) rint (1, NS - 1)] = pick (others);
            }
        }
        for (auto& v : out) v = clampi (toDial (c, v), -36, 36);
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
        // at least three sounding stages, counted along the path
        Params p; p.s = s;
        const int L = clampi (s.length, 1, NS);
        int sounding = 0;
        for (int i = 0; i < L; ++i) if (s.mode[(size_t) Engine::cellAt (p, i)] != 0) ++sounding;
        if (sounding < std::min (3, L))
        {
            s.mode[(size_t) Engine::cellAt (p, 0)] = 1;
            if (L > 2) s.mode[(size_t) Engine::cellAt (p, rint (1, (L - 1) / 2))] = 1;
            if (L > 1) s.mode[(size_t) Engine::cellAt (p, rint ((L - 1) / 2 + 1, L - 1))] = 2;
        }
    }
    void randLayer (const EditContext& c, Snapshot& s, int L)
    {
        auto clampTo = [&] (std::array<int, NS>& a, int l) { for (auto& v : a) v = clampi (v, std::min (c.rlo[(size_t) l], c.rhi[(size_t) l]), std::max (c.rlo[(size_t) l], c.rhi[(size_t) l])); };
        if (L == LPitch)
        {
            if (c.playmode == Bass)
                for (auto& o : s.oct) o = (c.octjump > 0 && rnd() * 100 < std::max (c.octjump, 25)) ? pick ({ -1, 1, 1, 2 }) : 0;
            else { s.pitch = randPitches (c, s); clampTo (s.pitch, LPitch); }
        }
        else if (L == LVel)
        {
            for (auto& v : s.vel) { const int band = weighted ({ 0, 1, 2 }, { 30, 45, 25 }); v = band == 0 ? rint (110, 127) : (band == 1 ? rint (80, 109) : rint (40, 79)); }
            clampTo (s.vel, LVel);
        }
        else if (L == LGate)   { for (auto& g : s.gate) g = rint (2, 20) * 5; clampTo (s.gate, LGate); }
        else if (L == LChance) { for (auto& ch : s.chance) ch = weighted ({ 100, 90, 75, 50, 25 }, { 55, 15, 15, 10, 5 }); clampTo (s.chance, LChance); }
        else
        {
            const int lo = std::min (c.rlo[(size_t) L], c.rhi[(size_t) L]), hi = std::max (c.rlo[(size_t) L], c.rhi[(size_t) L]);
            for (auto& a : s.mod[(size_t) (L - LModA)]) a = rint (lo, hi);
        }
    }
    // Paths: the sequence's path + direction, and some layers get their own path or length
    void randPaths (Snapshot& s)
    {
        s.path = rint (0, kNumShapes - 1);
        s.direction = weighted ({ Fwd, Ping, Rev, Rand }, { 50, 20, 15, 15 });
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& c = s.layers[(size_t) l];
            c.path = rnd() < 0.35 ? rint (0, kNumShapes - 1) : -1;
            c.steps = rnd() < 0.25 ? pick ({ 3, 5, 6, 7, 9, 12 }) : 0;
        }
    }
    // 0 Layer, 1 Rhythm, 2 All, 3 Paths
    void randomize (const EditContext& c, Snapshot& s)
    {
        if (c.randtarget == 0) randLayer (c, s, c.layer);
        else if (c.randtarget == 1) randRhythm (s);
        else if (c.randtarget == 2) { randLayer (c, s, 0); randLayer (c, s, 1); randLayer (c, s, 2); for (int l = LModA; l < kNumLayers; ++l) randLayer (c, s, l); }
        else randPaths (s);
    }
    int evolve (const EditContext& c, Snapshot& s)
    {
        Params p; p.s = s;
        const int t = c.evotarget == 2 ? (rnd() < 0.5 ? 0 : 1) : c.evotarget;
        std::vector<int> cand;
        for (int i = 0; i < Engine::stageCount (p); ++i) { const int cell = Engine::cellAt (p, i); if (s.mode[(size_t) cell] != 0 && s.len[(size_t) cell] > 0) cand.push_back (cell); }
        if (cand.empty()) cand.push_back (Engine::cellAt (p, 0));
        int st = pick (cand);
        if (t == 0)
        {
            if (c.playmode == Bass) s.oct[(size_t) st] = s.oct[(size_t) st] == 0 ? pick ({ -1, 1 }) : 0;
            else
            {
                std::vector<int> used;
                for (int k = 0; k < NS; ++k) if (std::find (used.begin(), used.end(), s.pitch[(size_t) k]) == used.end()) used.push_back (s.pitch[(size_t) k]);
                std::vector<int> ch;
                if (c.pool == 0) { ch = used; ch.push_back (s.pitch[(size_t) st] + pick ({ -2, -1, 1, 2 })); }
                else { for (int v : pitchChoices (c, s)) ch.push_back (toDial (c, v)); ch.insert (ch.end(), used.begin(), used.end()); }
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
    // rotate values one place along the path (each layer along its own path and length)
    static void nudge (Snapshot& s, int d)
    {
        Params p; p.s = s;
        const int L = Engine::stageCount (p);
        if (L < 2) return;
        auto rot = [d] (std::array<int, NS>& a, const std::vector<int>& cells)
        {
            const int n = (int) cells.size();
            if (n < 2) return;
            std::vector<int> v; for (int c : cells) v.push_back (a[(size_t) c]);
            if (d > 0) std::rotate (v.begin(), v.end() - 1, v.end()); else std::rotate (v.begin(), v.begin() + 1, v.end());
            for (int i = 0; i < n; ++i) a[(size_t) cells[(size_t) i]] = v[(size_t) i];
        };
        auto cellsFor = [&] (int l)
        {
            std::vector<int> cells;
            const auto& c = s.layers[(size_t) l];
            if (l < 0 || c.linked()) { for (int i = 0; i < L; ++i) cells.push_back (Engine::cellAt (p, i)); return cells; }
            const int n = Engine::effLen (p, l), shape = c.path >= 0 ? c.path : s.path;
            for (int i = 0; i < n; ++i) cells.push_back (cellForPathPosition (shape, clampi (i + windowOffset (c.shift, n), 0, 15)));
            return cells;
        };
        rot (s.pitch, cellsFor (LPitch)); rot (s.oct, cellsFor (LPitch)); rot (s.vel, cellsFor (LVel));
        rot (s.gate, cellsFor (LGate)); rot (s.chance, cellsFor (LChance));
        for (int m = 0; m < kNumMods; ++m) rot (s.mod[(size_t) m], cellsFor (LModA + m));
        const auto stageCells = cellsFor (-1);
        rot (s.mode, stageCells); rot (s.div, stageCells); rot (s.len, stageCells);
    }
    static void seqReset (Snapshot& s)
    {
        s.resetStages(); s.length = 8; s.direction = Fwd; s.path = 0; s.shift = 0;
        for (auto& l : s.layers) l = LayerCfg();
    }
    static void resetLayer (Snapshot& s, int l)
    {
        Snapshot d;
        s.layerValues (l) = d.layerValues (l);
        if (l == LPitch) s.oct.fill (0);
        s.layers[(size_t) l] = LayerCfg();
    }

private:
    int rint (int lo, int hi) { return std::uniform_int_distribution<int> (std::min (lo, hi), std::max (lo, hi)) (rng); }
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

} // namespace bee
