// SnakeEngine.h
// Framework-free sequencer core for the Snake VST3 port.
// Re-implements the behaviour of the Max for Live device (MDD Snake v3.2.3):
//   * 6 lanes (Notes, Gates, Velocities, Custom 1-3), 16 steps each, laid out on a 4x4 grid
//   * each lane: direction, speed, length, shift (loop window offset), "snake" shape (grid path)
//   * Gates lane is the clock: every ON gate step fires one note (with swing on odd steps)
//   * Notes lane is scale-quantised (36 scales + user scale, root note)
//   * Custom lanes send MIDI CC (replaces the Live-API parameter mapping)
// No JUCE dependency so it can be unit-tested anywhere.
#pragma once

#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

namespace snake
{

enum Lane : int { Notes = 0, Gates, Velocities, Custom1, Custom2, Custom3, NumLanes };
constexpr int kNumSteps   = 16;
constexpr int kNumShapes  = 14;
constexpr int kNumScales  = 37; // 36 fixed + User
constexpr int kUserScale  = 36;

enum Direction : int { Forward = 0, Backward, ForeBack, BackFore, Random, NumDirections };

// Speed menu: 32n 16n 8n 4n 2n  -> step length in quarter notes
inline double speedToQuarters (int speedIndex)
{
    static constexpr double q[] = { 0.125, 0.25, 0.5, 1.0, 2.0 };
    return q[std::clamp (speedIndex, 0, 4)];
}

// ---------------------------------------------------------------------------
// Snake shapes, copied verbatim from the device ("p Snake Core").
// Entry [cell] = the step number (1..16) at which that grid cell plays.
// Cells are numbered row-major: 1 2 3 4 / 5 6 7 8 / 9 10 11 12 / 13 14 15 16.
// Names follow the device's SVG icons: S1 S1R S2 S2R ... S6a-d.
// ---------------------------------------------------------------------------
inline const std::array<std::array<int, 16>, kNumShapes>& shapeTable()
{
    static const std::array<std::array<int, 16>, kNumShapes> t {{
        {{  1, 2, 3, 4,  5, 6, 7, 8,  9,10,11,12, 13,14,15,16 }}, // S1   rows
        {{ 13,14,15,16,  9,10,11,12,  5, 6, 7, 8,  1, 2, 3, 4 }}, // S1R  rows bottom-up
        {{  1, 5, 9,13,  2, 6,10,14,  3, 7,11,15,  4, 8,12,16 }}, // S2   columns
        {{  4, 8,12,16,  3, 7,11,15,  2, 6,10,14,  1, 5, 9,13 }}, // S2R  columns from bottom-left
        {{  1, 8, 9,16,  2, 7,10,15,  3, 6,11,14,  4, 5,12,13 }}, // S3   vertical snake
        {{  4, 5,12,13,  3, 6,11,14,  2, 7,10,15,  1, 8, 9,16 }}, // S3R  vertical snake bottom-up
        {{  1, 2, 3, 4,  8, 7, 6, 5,  9,10,11,12, 16,15,14,13 }}, // S4   horizontal snake
        {{  4, 3, 2, 1,  5, 6, 7, 8, 12,11,10, 9, 13,14,15,16 }}, // S4R  horizontal snake from right
        {{  1, 2, 6, 7,  3, 5, 8,13,  4, 9,12,14, 10,11,15,16 }}, // S5   zig-zag diagonal
        {{ 10,11,15,16,  4, 9,12,14,  3, 5, 8,13,  1, 2, 6, 7 }}, // S5R  zig-zag diagonal bottom-up
        {{  1, 2, 3, 4, 12,13,14, 5, 11,16,15, 6, 10, 9, 8, 7 }}, // S6a  spiral in, from top-left
        {{  4, 3, 2, 1,  5,14,13,12,  6,15,16,11,  7, 8, 9,10 }}, // S6b  spiral in, from top-right
        {{ 10, 9, 8, 7, 11,16,15, 6, 12,13,14, 5,  1, 2, 3, 4 }}, // S6c  spiral in, from bottom-left
        {{  7, 8, 9,10,  6,15,16,11,  5,14,13,12,  4, 3, 2, 1 }}, // S6d  spiral in, from bottom-right
    }};
    return t;
}

inline const char* shapeName (int s)
{
    static const char* n[] = { "S1","S1R","S2","S2R","S3","S3R","S4","S4R","S5","S5R","S6a","S6b","S6c","S6d" };
    return n[std::clamp (s, 0, kNumShapes - 1)];
}

// Inverse: for path position p (0..15) -> cell index (0..15)
inline int cellForPathPosition (int shape, int pathPos)
{
    const auto& row = shapeTable()[(size_t) std::clamp (shape, 0, kNumShapes - 1)];
    for (int c = 0; c < 16; ++c)
        if (row[(size_t) c] == pathPos + 1)
            return c;
    return 0;
}

// ---------------------------------------------------------------------------
// Scales (from the device's [table] objects, same order as its Scale menu)
// NB: the device's "Minor" table is 0 2 3 5 7 9 11 (= melodic minor). Kept as-is
//     so patterns sound identical to the original.
// ---------------------------------------------------------------------------
struct Scale { const char* name; std::vector<int> pcs; };

inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "Chromatic",        { 0,1,2,3,4,5,6,7,8,9,10,11 } },
        { "Major",            { 0,2,4,5,7,9,11 } },
        { "Minor",            { 0,2,3,5,7,9,11 } },
        { "Dorian",           { 0,2,3,5,7,9,10 } },
        { "Mixolydian",       { 0,2,4,5,7,9,10 } },
        { "Lydian",           { 0,2,4,6,7,9,11 } },
        { "Phrygian",         { 0,1,3,5,7,8,10 } },
        { "Locrian",          { 0,1,3,5,6,8,10 } },
        { "WholeTone",        { 0,2,4,6,8,10 } },
        { "HalfWholeDim",     { 0,1,3,4,6,7,9,10 } },
        { "WholeHalfDim",     { 0,2,3,5,6,8,9,11 } },
        { "MinorBlues",       { 0,3,5,6,7,10 } },
        { "MinorPentatonic",  { 0,3,5,7,10 } },
        { "MajorPentatonic",  { 0,2,4,7,9 } },
        { "HarmonicMinor",    { 0,2,3,5,7,8,11 } },
        { "HarmonicMajor",    { 0,2,4,5,7,8,11 } },
        { "Dorian#4",         { 0,2,3,6,7,9,10 } },
        { "PhrygianDominant", { 0,1,4,5,7,8,10 } },
        { "MelodicMinor",     { 0,2,3,5,7,9,11 } },
        { "LydianAugmented",  { 0,2,4,6,8,9,11 } },
        { "LydianDominant",   { 0,2,4,6,7,9,10 } },
        { "SuperLocrian",     { 0,1,3,4,6,8,10 } },
        { "8ToneSpanish",     { 0,1,3,4,5,6,8,10 } },
        { "Bhairav",          { 0,1,4,5,7,8,11 } },
        { "HungarianMinor",   { 0,2,3,6,7,8,11 } },
        { "Hirajoshi",        { 0,2,3,7,8 } },
        { "InSen",            { 0,1,5,7,10 } },
        { "Iwato",            { 0,1,5,6,10 } },
        { "Kumoi",            { 0,2,3,7,9 } },
        { "PelogSelisir",     { 0,1,3,7,8 } },
        { "PelogTembung",     { 0,1,5,7,8 } },
        { "Messiaen3",        { 0,2,3,4,6,7,8,10,11 } },
        { "Messiaen4",        { 0,1,2,5,6,7,8,11 } },
        { "Messiaen5",        { 0,1,5,6,7,11 } },
        { "Messiaen6",        { 0,2,4,5,6,8,10,11 } },
        { "Messiaen7",        { 0,1,2,3,5,6,7,8,9,11 } },
    };
    return s;
}

// Fills pcs from a 12-bit mask (bit 0 = C). Empty mask -> {0} (device's "keepone").
inline int pcsFromMask (uint16_t mask, int* out)
{
    int n = 0;
    for (int i = 0; i < 12; ++i)
        if (mask & (1u << i)) out[n++] = i;
    if (n == 0) out[n++] = 0;
    return n;
}

// Note dial (0..83) -> MIDI note, replicating "p quantize" / "p Conversion":
//   octave  = int(v) / 12          (0..6, maps to -36..+36 around MIDI 60)
//   percent = (v - 12*octave) / 11 * 100
//   degree  = int(percent * N / 100), clipped to N-1
//   note    = 24 + 12*octave + scale[degree] + root
inline int quantizeNote (float dialValue, const int* pcs, int numPcs, int root)
{
    const float v   = std::clamp (dialValue, 0.0f, 83.0f);
    const int   oct = std::clamp ((int) v / 12, 0, 6);
    const float pct = (v - 12.0f * (float) oct) / 11.0f * 100.0f;
    int deg = (int) (pct * (float) numPcs / 100.0f);
    deg = std::clamp (deg, 0, numPcs - 1);
    return std::clamp (24 + 12 * oct + pcs[deg] + root, 0, 127);
}

// ---------------------------------------------------------------------------
// Parameters snapshot handed to the engine each block
// ---------------------------------------------------------------------------
struct LaneSettings
{
    int   direction = Forward;
    int   speed     = 1;      // 16n
    int   length    = 16;     // 1..16
    float shift     = 0.0f;   // 0..100 %
    int   shape     = 0;      // 0..13
};

struct EngineParams
{
    std::array<LaneSettings, NumLanes> lanes {};
    std::array<float, kNumSteps> notes {};
    std::array<bool,  kNumSteps> gates {};
    std::array<int,   kNumSteps> velocities {};
    std::array<std::array<int, kNumSteps>, 3> custom {};

    int      root        = 0;     // 0..11
    int      scaleIndex  = 1;     // Major
    uint16_t userMask    = 1;     // used when scaleIndex == kUserScale
    float    swing       = 0.0f;  // 0..100
    int      midiChannel = 1;     // 1..16

    std::array<bool, 3> ccEnabled { true, true, true };
    std::array<int, 3>  ccNumber  { 20, 21, 22 };
    std::array<int, 3>  ccMin     { 0, 0, 0 };
    std::array<int, 3>  ccMax     { 127, 127, 127 };
};

struct Transport
{
    bool   isPlaying = false;
    double ppq       = 0.0;   // position at start of block, in quarter notes
    double bpm       = 120.0;
};

struct MidiEvent
{
    int     sampleOffset;
    uint8_t status, data1, data2;
};

// Loop position inside the lane (0..length-1) for an absolute step count,
// matching live.step's direction modes. Random uses the supplied rng.
template <typename Rng>
inline int loopIndex (int64_t stepCount, int direction, int length, Rng& rng)
{
    length = std::clamp (length, 1, 16);
    if (length == 1) return 0;
    auto pmod = [] (int64_t a, int64_t m) { auto r = a % m; return (int) (r < 0 ? r + m : r); };

    switch (direction)
    {
        case Backward: return length - 1 - pmod (stepCount, length);
        case ForeBack:
        {
            const int period = 2 * length - 2;
            const int m = pmod (stepCount, period);
            return m < length ? m : period - m;
        }
        case BackFore:
        {
            const int period = 2 * length - 2;
            const int m = pmod (stepCount, period);
            return length - 1 - (m < length ? m : period - m);
        }
        case Random:   return (int) (rng() % (uint32_t) length);
        case Forward:
        default:       return pmod (stepCount, length);
    }
}

// Shift: device scales 0..100 % onto 0..(16 - length) and adds it to the loop index.
inline int windowOffset (float shiftPercent, int length)
{
    const int room = 16 - std::clamp (length, 1, 16);
    return std::clamp ((int) (std::clamp (shiftPercent, 0.0f, 100.0f) * (float) room / 100.0f), 0, room);
}

// ---------------------------------------------------------------------------
class Engine
{
public:
    Engine() { pending.reserve (64); active.reserve (64); events.reserve (512); }

    void prepare (double newSampleRate) { sampleRate = newSampleRate; resetState(); }

    void resetState()
    {
        pending.clear();
        active.clear();
        wasPlaying = false;
        for (auto& s : lastStep) s = INT64_MIN;
        for (auto& c : currentCell) c = -1;
    }

    // Cell (0..15) currently playing on each lane, -1 when stopped. For the UI.
    std::array<int, NumLanes> currentCell {};
    int latchedPitch = 60;
    int latchedVelocity = 64;

    // Process one block. emit(const MidiEvent&) is called in time order.
    template <typename Emit>
    void process (const EngineParams& p, const Transport& t, int numSamples, Emit&& emit)
    {
        lastChannel = (uint8_t) (std::clamp (p.midiChannel, 1, 16) - 1);

        if (! t.isPlaying || t.bpm <= 0.0 || numSamples <= 0)
        {
            if (wasPlaying)
                allNotesOff (0, emit);
            pending.clear();
            wasPlaying = false;
            for (auto& s : lastStep) s = INT64_MIN;
            for (auto& c : currentCell) c = -1;
            return;
        }

        const double samplesPerQuarter = sampleRate * 60.0 / t.bpm;
        const double ppqStart = t.ppq;
        const double ppqEnd   = ppqStart + (double) numSamples / samplesPerQuarter;
        constexpr double eps  = 1.0e-9;

        // Start, or relocation (cycle jump / scrub): resync everything.
        const bool jumped   = wasPlaying && std::abs (ppqStart - expectedPpq) > 1.0e-3;
        const bool starting = ! wasPlaying || jumped;
        if (starting)
        {
            allNotesOff (0, emit);
            pending.clear();
            for (auto& s : lastStep) s = INT64_MIN;
        }
        wasPlaying  = true;
        expectedPpq = ppqEnd;

        auto toSample = [&] (double ppq)
        {
            const auto s = (int) std::floor ((ppq - ppqStart) * samplesPerQuarter + 0.5);
            return std::clamp (s, 0, numSamples - 1);
        };

        // ---- gather lane step events in this block
        events.clear();
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            const double q = speedToQuarters (p.lanes[(size_t) lane].speed);

            // Starting mid-step: value lanes latch their current step right away
            // (live.step outputs its current step as soon as it runs). Gates do not fire.
            if (starting && lane != Gates)
            {
                const auto cur = (int64_t) std::floor (ppqStart / q + eps);
                if ((double) cur * q < ppqStart - eps)
                    events.push_back ({ ppqStart, 0, lane, cur, 0.0 });
            }

            for (auto k = (int64_t) std::ceil ((ppqStart - eps) / q); (double) k * q < ppqEnd - eps; ++k)
                if (k > lastStep[(size_t) lane])
                    events.push_back ({ std::max ((double) k * q, ppqStart), 0, lane, k, 0.0 });
        }

        // ---- swung note-ons carried over from earlier blocks
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->ppq < ppqEnd - eps)
            {
                events.push_back ({ std::max (it->ppq, ppqStart), 1, -1, 0, it->lengthQ });
                it = pending.erase (it);
            }
            else ++it;
        }

        auto order = [] (const Ev& a, const Ev& b)
        {
            if (a.ppq < b.ppq) return true;
            if (b.ppq < a.ppq) return false;
            return a.prio < b.prio; // lane updates before note-ons at the same instant
        };
        std::stable_sort (events.begin(), events.end(), order);

        int pcs[12];
        const int numPcs = p.scaleIndex >= kUserScale ? pcsFromMask (p.userMask, pcs)
                                                      : copyScale (p.scaleIndex, pcs);
        const uint8_t ch = lastChannel;

        auto emitOffsetsUpTo = [&] (double ppq, bool inclusive)
        {
            // note-offs due before (or at) ppq, in time order
            std::sort (active.begin(), active.end(), [] (const Active& a, const Active& b) { return a.offPpq < b.offPpq; });
            while (! active.empty() && (inclusive ? active.front().offPpq <= ppq + eps : active.front().offPpq < ppq - eps))
            {
                emit (MidiEvent { toSample (active.front().offPpq), (uint8_t) (0x80 | ch), (uint8_t) active.front().pitch, 0 });
                active.erase (active.begin());
            }
        };

        for (size_t i = 0; i < events.size(); ++i)
        {
            const Ev e = events[i]; // copy: vector may grow below
            emitOffsetsUpTo (e.ppq, true);
            const int so = toSample (e.ppq);

            if (e.prio == 1) // ---- note on, uses the latched pitch & velocity
            {
                const int pitch = latchedPitch, vel = latchedVelocity;
                for (auto a = active.begin(); a != active.end(); ++a)
                    if (a->pitch == pitch)
                    {
                        emit (MidiEvent { so, (uint8_t) (0x80 | ch), (uint8_t) pitch, 0 });
                        active.erase (a);
                        break;
                    }
                if (vel > 0)
                {
                    emit (MidiEvent { so, (uint8_t) (0x90 | ch), (uint8_t) pitch, (uint8_t) std::clamp (vel, 1, 127) });
                    active.push_back ({ pitch, e.ppq + e.lenQ });
                }
                continue;
            }

            // ---- lane step
            const int  lane = e.lane;
            const auto& ls  = p.lanes[(size_t) lane];
            lastStep[(size_t) lane] = std::max (lastStep[(size_t) lane], e.step);

            const int idx  = loopIndex (e.step, ls.direction, ls.length, rngs[(size_t) lane]);
            const int pos  = std::clamp (idx + windowOffset (ls.shift, ls.length), 0, 15);
            const int cell = cellForPathPosition (ls.shape, pos);
            currentCell[(size_t) lane] = cell;

            if (lane == Notes)
            {
                latchedPitch = quantizeNote (p.notes[(size_t) cell], pcs, numPcs, p.root);
            }
            else if (lane == Velocities)
            {
                latchedVelocity = std::clamp (p.velocities[(size_t) cell], 0, 127);
            }
            else if (lane == Gates)
            {
                if (p.gates[(size_t) cell])
                {
                    const double q = speedToQuarters (ls.speed);
                    // Device: transport-locked phasor over 2 steps; the 2nd pulse moves
                    // from 0.5 to 0.744 of the cycle as Swing goes 0 -> 100 %.
                    const bool   odd   = (e.step & 1) != 0;
                    const double delay = odd ? (std::clamp (p.swing, 0.0f, 100.0f) / 100.0) * 0.244 * 2.0 * q : 0.0;
                    const double onPpq = (double) e.step * q + delay;
                    const double lenQ  = q * 0.5; // device: note length = half a gate step
                    if (onPpq < ppqEnd - eps)
                    {
                        const Ev on { std::max (onPpq, e.ppq), 1, -1, 0, lenQ };
                        events.insert (std::upper_bound (events.begin() + (long) i + 1, events.end(), on, order), on);
                    }
                    else
                        pending.push_back ({ onPpq, lenQ });
                }
            }
            else // custom lanes -> MIDI CC (replaces Live parameter mapping)
            {
                const int ci = lane - Custom1;
                if (p.ccEnabled[(size_t) ci])
                {
                    const int raw = std::clamp (p.custom[(size_t) ci][(size_t) cell], 0, 127);
                    const int lo = p.ccMin[(size_t) ci], hi = p.ccMax[(size_t) ci];
                    const int v  = std::clamp ((int) std::lround (lo + (hi - lo) * (raw / 127.0)), 0, 127);
                    emit (MidiEvent { so, (uint8_t) (0xB0 | ch), (uint8_t) std::clamp (p.ccNumber[(size_t) ci], 0, 127), (uint8_t) v });
                }
            }
        }

        emitOffsetsUpTo (ppqEnd, false);
    }

    template <typename Emit>
    void allNotesOff (int sampleOffset, Emit&& emit)
    {
        for (const auto& a : active)
            emit (MidiEvent { sampleOffset, (uint8_t) (0x80 | lastChannel), (uint8_t) a.pitch, 0 });
        active.clear();
    }

private:
    struct Ev      { double ppq; int prio; int lane; int64_t step; double lenQ; };
    struct Pending { double ppq; double lengthQ; };
    struct Active  { int pitch; double offPpq; };

    static int copyScale (int index, int* out)
    {
        const auto& s = scales()[(size_t) std::clamp (index, 0, (int) scales().size() - 1)].pcs;
        std::copy (s.begin(), s.end(), out);
        return (int) s.size();
    }

    struct XorShift
    {
        uint32_t s = 0x9E3779B9u;
        uint32_t operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    };

    double sampleRate = 44100.0;
    bool   wasPlaying = false;
    double expectedPpq = 0.0;
    uint8_t lastChannel = 0;
    std::array<int64_t, NumLanes> lastStep {};
    std::array<XorShift, NumLanes> rngs {};
    std::vector<Pending> pending;
    std::vector<Active>  active;
    std::vector<Ev>      events;
};

} // namespace snake
