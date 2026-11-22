// Unit tests for SnakeEngine (no JUCE needed):  g++ -std=c++17 -O1 -I../Source engine_test.cpp
#include "SnakeEngine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace snake;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct Abs { long sample; int status, d1, d2; };

static EngineParams defaults()
{
    EngineParams p;
    const float noteInit[16] = { 0.0f, 1.9606f, 3.2677f, 5.2283f, 6.5354f, 7.9953f, 11.7773f, 12.6533f,
                                 14.5696f, 15.4455f, 17.3673f, 19.4228f, 21.0355f, 22.2612f, 25.1113f, 25.5967f };
    for (int i = 0; i < 16; ++i) { p.notes[i] = noteInit[i]; p.gates[i] = true; p.velocities[i] = 64; }
    return p;
}

// Run the engine over `quarters` quarter notes with a given block size, return absolute-sample events.
static std::vector<Abs> run (const EngineParams& p, double quarters, int block, double sr = 48000.0, double bpm = 120.0, double startPpq = 0.0)
{
    Engine e; e.prepare (sr);
    std::vector<Abs> out;
    const double spq = sr * 60.0 / bpm;
    const long total = (long) (quarters * spq);
    for (long pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<long> (block, total - pos);
        Transport t { true, startPpq + pos / spq, bpm };
        e.process (p, t, n, [&] (const MidiEvent& m) { out.push_back ({ pos + m.sampleOffset, m.status, m.data1, m.data2 }); });
    }
    Transport stop { false, 0, bpm };
    e.process (p, stop, block, [&] (const MidiEvent& m) { out.push_back ({ total + m.sampleOffset, m.status, m.data1, m.data2 }); });
    return out;
}

static std::vector<Abs> filter (const std::vector<Abs>& v, int statusHi)
{
    std::vector<Abs> r; for (auto& a : v) if ((a.status & 0xF0) == statusHi) r.push_back (a); return r;
}

int main()
{
    // --- quantiser reproduces the device's default note display (Visu1 values)
    {
        auto p = defaults();
        const int expect[16] = { 24,26,28,29,31,33,35,36,38,40,41,43,45,47,48,50 };
        const auto& maj = scales()[1].pcs;
        for (int i = 0; i < 16; ++i)
            CHECK (quantizeNote (p.notes[i], maj.data(), (int) maj.size(), 0) == expect[i]);
        CHECK (quantizeNote (83.0f, maj.data(), 7, 0) == 24 + 72 + 11);
        CHECK (quantizeNote (0.0f, maj.data(), 7, 5) == 29);
    }

    // --- shapes are permutations of 1..16
    for (int s = 0; s < kNumShapes; ++s)
    {
        int seen[17] = {};
        for (int c = 0; c < 16; ++c) seen[shapeTable()[s][c]]++;
        for (int k = 1; k <= 16; ++k) CHECK (seen[k] == 1);
    }
    CHECK (cellForPathPosition (2, 1) == 4);   // S2 columns: second step is cell 5 (index 4)
    CHECK (cellForPathPosition (10, 4) == 7);  // S6a spiral: 5th step is cell 8

    // --- directions
    {
        uint32_t dummy = 1; auto rng = [&] { return dummy++; };
        CHECK (loopIndex (0, Forward, 4, rng) == 0 && loopIndex (5, Forward, 4, rng) == 1);
        CHECK (loopIndex (0, Backward, 4, rng) == 3);
        const int fb[] = { 0,1,2,3,2,1,0,1 };
        for (int i = 0; i < 8; ++i) CHECK (loopIndex (i, ForeBack, 4, rng) == fb[i]);
        CHECK (loopIndex (0, BackFore, 4, rng) == 3 && loopIndex (3, BackFore, 4, rng) == 0);
        CHECK (windowOffset (100.0f, 4) == 12 && windowOffset (50.0f, 4) == 6 && windowOffset (100.0f, 16) == 0);
    }

    // --- default pattern: 16 sixteenths per bar, half-step notes, ascending major run
    {
        auto p = defaults();
        auto ev = run (p, 4.0, 512);
        auto ons = filter (ev, 0x90), offs = filter (ev, 0x80);
        CHECK (ons.size() == 16);
        CHECK (offs.size() == 16);
        const int expect[16] = { 24,26,28,29,31,33,35,36,38,40,41,43,45,47,48,50 };
        for (size_t i = 0; i < ons.size() && i < 16; ++i)
        {
            CHECK (ons[i].d1 == expect[i]);
            CHECK (ons[i].sample == (long) i * 6000);         // 16th @120bpm/48k = 6000 samples
            CHECK (offs[i].sample == (long) i * 6000 + 3000); // half step
            CHECK (ons[i].d2 == 64);
        }
    }

    // --- block-size independence
    {
        auto p = defaults(); p.swing = 63.0f; p.lanes[Notes].direction = ForeBack; p.lanes[Gates].shape = 4;
        p.gates[3] = false; p.gates[9] = false;
        auto a = run (p, 8.0, 64), b = run (p, 8.0, 1000), c = run (p, 8.0, 4096);
        auto same = [] (const std::vector<Abs>& x, const std::vector<Abs>& y)
        {
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (x[i].sample != y[i].sample || x[i].status != y[i].status || x[i].d1 != y[i].d1) return false;
            return true;
        };
        CHECK (same (a, b));
        CHECK (same (a, c));
    }

    // --- swing: odd 16ths move later by swing * 0.488 of a step
    {
        auto p = defaults(); p.swing = 100.0f;
        auto ons = filter (run (p, 1.0, 256), 0x90);
        CHECK (ons.size() == 4);
        if (ons.size() == 4)
        {
            CHECK (ons[0].sample == 0);
            CHECK (std::labs (ons[1].sample - (6000 + (long) (0.488 * 6000))) <= 1);
            CHECK (ons[2].sample == 12000);
        }
    }

    // --- gates off -> silent steps; shape on the gates lane picks which cell is read
    {
        auto p = defaults();
        for (auto& g : p.gates) g = false;
        p.gates[4] = true;                  // cell 5
        p.lanes[Gates].shape = 2;           // S2 columns -> cell 5 is the 2nd step
        auto ons = filter (run (p, 4.0, 512), 0x90);
        CHECK (ons.size() == 1);
        if (! ons.empty()) CHECK (ons[0].sample == 6000);
    }

    // --- length + shift window: notes lane length 4, shift 100% -> cells 13..16 (S1)
    {
        auto p = defaults();
        p.lanes[Notes].length = 4; p.lanes[Notes].shift = 100.0f;
        auto ons = filter (run (p, 2.0, 512), 0x90);
        const int expect[4] = { 45,47,48,50 };
        CHECK (ons.size() == 8);
        for (size_t i = 0; i < ons.size(); ++i) CHECK (ons[i].d1 == expect[i % 4]);
    }

    // --- user scale + root
    {
        auto p = defaults();
        p.scaleIndex = kUserScale; p.userMask = (1 << 0) | (1 << 7); p.root = 2; // C,G  -> D,A
        auto ons = filter (run (p, 1.0, 512), 0x90);
        for (auto& o : ons) CHECK ((o.d1 % 12) == 2 || (o.d1 % 12) == 9);
    }

    // --- custom lane -> CC with min/max mapping; different speed (8n)
    {
        auto p = defaults();
        p.lanes[Custom1].speed = 2;
        for (int i = 0; i < 16; ++i) p.custom[0][i] = 127 * (i % 2);
        p.ccNumber[0] = 74; p.ccMin[0] = 20; p.ccMax[0] = 100;
        p.ccEnabled[1] = p.ccEnabled[2] = false;
        auto cc = filter (run (p, 2.0, 333), 0xB0);
        CHECK (cc.size() == 4);
        for (size_t i = 0; i < cc.size(); ++i)
        {
            CHECK (cc[i].d1 == 74);
            CHECK (cc[i].d2 == (i % 2 ? 100 : 20));
            CHECK (cc[i].sample == (long) i * 12000);
        }
    }

    // --- velocity 0 mutes, velocity lane at a different rate
    {
        auto p = defaults();
        for (int i = 0; i < 16; ++i) p.velocities[i] = (i % 2) ? 0 : 100;
        p.lanes[Velocities].speed = 2; // 8n: two 16ths share each velocity step
        auto ons = filter (run (p, 4.0, 512), 0x90);
        CHECK (ons.size() == 8);
        for (auto& o : ons) CHECK (o.d2 == 100);
    }

    // --- loop jump: transport jumps back, notes are closed and pattern restarts cleanly
    {
        auto p = defaults();
        Engine e; e.prepare (48000.0);
        std::vector<Abs> out; long base = 0;
        auto blk = [&] (double ppq) { Transport t { true, ppq, 120.0 };
            e.process (p, t, 512, [&] (const MidiEvent& m) { out.push_back ({ base + m.sampleOffset, m.status, m.data1, m.data2 }); }); base += 512; };
        blk (3.99); blk (0.0);
        int ons = 0, offs = 0; for (auto& a : out) { if ((a.status & 0xF0) == 0x90) ++ons; if ((a.status & 0xF0) == 0x80) ++offs; }
        CHECK (ons == 2);      // one at ppq 4.0 (bar 2), one at 0.0 after the jump
        CHECK (offs >= 1);     // the hanging note got closed on the jump
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "All engine tests passed.\n", failures);
    return failures ? 1 : 0;
}
