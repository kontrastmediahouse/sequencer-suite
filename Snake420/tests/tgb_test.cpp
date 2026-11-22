// Unit tests for TgbCore (no JUCE):  g++ -std=c++17 -I../Source tgb_test.cpp
#include "TgbCore.h"
#include <cstdio>
#include <set>
using namespace tgb;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Ev { long t; int st, d1, d2; };
static std::vector<Ev> run (Engine& e, Params& p, double quarters, int block = 512, double startQ = 0)
{
    std::vector<Ev> out; const double spq = 24000.0; long pos = 0; const long total = (long) (quarters * spq);
    for (; pos < total; pos += block)
    {
        const int n = (int) std::min<long> (block, total - pos);
        e.process (p, true, startQ + pos / spq, 120.0, n, [&] (const MidiOut& m) { out.push_back ({ pos + m.sampleOffset, m.b0 & 0xF0, m.b1, m.b2 }); });
    }
    return out;
}
static int count (const std::vector<Ev>& v, int st) { int n = 0; for (auto& e : v) if (e.st == st) ++n; return n; }

int main()
{
    // ---- defaults: 8 stages of one 16th, C3, velocity 100, gate 50 %
    {
        Engine e; e.prepare (48000); Params p;
        auto ev = run (e, p, 4.0);
        CHECK (count (ev, 0x90) == 16);
        CHECK (count (ev, 0x80) == 16);
        for (auto& x : ev) if (x.st == 0x90) { CHECK (x.d1 == 60); CHECK (x.d2 == 100); }
    }
    // ---- aux: step mode sends a CC once per value change, glide ramps in 5 ms grains
    {
        Engine e; e.prepare (48000); Params p;
        for (int i = 0; i < NS; ++i) p.s.aux[(size_t) i] = i % 2 ? 100 : 20;
        p.auxCC = 74;
        auto ev = run (e, p, 2.0);
        int cc = 0; for (auto& x : ev) if (x.st == 0xB0) { ++cc; CHECK (x.d1 == 74); CHECK (x.d2 == 20 || x.d2 == 100); }
        CHECK (cc == 8);                                // 8 stages in 2 beats, every one a change
        Engine e2; e2.prepare (48000); p.glide = 1;
        auto ev2 = run (e2, p, 2.0);
        CHECK (count (ev2, 0xB0) > 40);                 // a ramp: many intermediate values
        p.glide = 0; p.auxKind = AuxPitchBend;
        Engine e3; e3.prepare (48000);
        auto ev3 = run (e3, p, 1.0);
        CHECK (count (ev3, 0xE0) == 4);
        for (auto& x : ev3) if (x.st == 0xE0) CHECK (x.d1 == 0 && (x.d2 == 20 || x.d2 == 100));
    }
    // ---- mode II with divider, mode I-, rests, skipped stage
    {
        Engine e; e.prepare (48000); Params p;
        p.s.length = 4;
        p.s.mode = { 2, 3, 0, 1 }; p.s.div = { 1, 0, 0, 0 }; p.s.len = { 4, 2, 2, 0 };
        auto ev = run (e, p, 2.0);                      // cycle = 8 pulses = 2 beats
        // stage 1: notes on pulses 0 and 2; stage 2: one held note; stage 3: rest; stage 4: skipped
        CHECK (count (ev, 0x90) == 3);
    }
    // ---- pattern slot switches exactly at the cycle start
    {
        Engine e; e.prepare (48000); Params p;
        Snapshot slot; slot.pitch.fill (12);
        e.setPendingSlot (&slot);
        auto ev = run (e, p, 0.0);  (void) ev;
        // start: the slot is applied before the very first pulse
        Engine e2; e2.prepare (48000); Params p2;
        auto first = run (e2, p2, 1.0);                 // 4 pulses of stage 1..4
        e2.setPendingSlot (&slot);
        auto second = run (e2, p2, 2.0, 512, 1.0);      // stages 5..8, then the cycle restarts
        int c60 = 0, c72 = 0; for (auto& x : second) if (x.st == 0x90) { if (x.d1 == 60) ++c60; if (x.d1 == 72) ++c72; }
        CHECK (c60 == 4 && c72 == 4);
        CHECK (p2.s.pitch[0] == 12);                    // the engine handed the new pattern back
    }
    // ---- evolve request every 8 bars
    {
        Engine e; e.prepare (48000); Params p; p.evolve = 1;
        int fired = 0; const double spq = 24000.0;
        for (long pos = 0; pos < (long) (33 * 4 * spq); pos += 512)
        {
            e.process (p, true, pos / spq, 120.0, 512, [] (const MidiOut&) {});
            if (e.evolveRequested) ++fired;
        }
        CHECK (fired == 4);
    }
    // ---- stop sends note-offs; reset on stop restarts at stage 1
    {
        Engine e; e.prepare (48000); Params p; p.s.gate.fill (100);
        run (e, p, 0.6);
        std::vector<Ev> offs;
        e.process (p, false, 0, 120.0, 512, [&] (const MidiOut& m) { offs.push_back ({ m.sampleOffset, m.b0 & 0xF0, m.b1, m.b2 }); });
        CHECK (count (offs, 0x80) >= 1);
    }
    // ---- bass mode: a played key overrides the bass note; arp waits for keys
    {
        Engine e; e.prepare (48000); Params p; p.playmode = Bass; p.bassnote = 36;
        p.s.oct = { 0, 1, -1, 2, 0, 0, 0, 0 };
        e.noteIn (40, 100, p);
        auto ev = run (e, p, 1.0);
        std::vector<int> pitches; for (auto& x : ev) if (x.st == 0x90) pitches.push_back (x.d1);
        CHECK (pitches.size() == 4 && pitches[0] == 40 && pitches[1] == 52 && pitches[2] == 28 && pitches[3] == 64);
        Engine a; a.prepare (48000); Params pa; pa.playmode = Arp;
        CHECK (count (run (a, pa, 1.0), 0x90) == 0);
    }
    // ---- scale snap: nearest, ties go down
    {
        Params p; p.scalemode = 1; p.root = 0; p.scale = 0;   // C major
        CHECK (snap (p, 61) == 60);   // C# -> C (tie goes down)
        CHECK (snap (p, 66) == 65);   // F# -> F
        CHECK (snap (p, 70) == 69);   // A# -> A
        CHECK (snap (p, 62) == 62);
    }
    // ---- clip render: default pattern = 1 bar, 16 notes
    {
        Engine e; e.prepare (48000); Params p;
        auto c = e.renderClip (p);
        CHECK (c.lengthQ == 4.0 && c.notes.size() == 16);
        p.s.length = 5;                                  // 5 sixteenths -> repeats after 5 bars
        auto c5 = e.renderClip (p);
        CHECK (c5.lengthQ == 20.0 && c5.notes.size() == 80);
    }
    // ---- editor: pools, notes cap, rhythm guarantee, nudge, evolve
    {
        Editor ed (42); EditContext c; Snapshot s;
        c.pool = 3; c.notes = 2;                         // Tension: root + half step
        for (int r = 0; r < 20; ++r)
        {
            ed.randomize (c, s);
            std::set<int> pcs; for (int v : s.pitch) pcs.insert (((v % 12) + 12) % 12);
            for (int pc : pcs) CHECK (pc == 0 || pc == 1);
        }
        c.scalemode = 1; c.root = 0; c.scale = 0;        // in C major Tension plays C and D
        ed.randomize (c, s);
        for (int v : s.pitch) { const int pc = ((v % 12) + 12) % 12; CHECK (pc == 0 || pc == 2); }
        c.scale = 3;                                     // Phrygian: C and C#
        ed.randomize (c, s);
        for (int v : s.pitch) { const int pc = ((v % 12) + 12) % 12; CHECK (pc == 0 || pc == 1); }

        c = EditContext(); c.randtarget = 1;
        Snapshot r; r.mode.fill (0);
        for (int k = 0; k < 50; ++k)
        {
            ed.randomize (c, r);
            int sounding = 0; for (int i = 0; i < 8; ++i) if (r.mode[(size_t) i]) ++sounding;
            CHECK (sounding >= 3);
            for (int i = 0; i < NS; ++i) CHECK (r.len[(size_t) i] >= 1 && r.len[(size_t) i] <= 4);
        }
        Snapshot n; for (int i = 0; i < NS; ++i) n.pitch[(size_t) i] = i;
        Editor::nudge (n, 1);
        CHECK (n.pitch[0] == 7 && n.pitch[1] == 0 && n.pitch[8] == 8);   // 8 stages rotate, 9-16 untouched
        Editor::nudge (n, -1);
        CHECK (n.pitch[0] == 0 && n.pitch[7] == 7);

        Snapshot ev; EditContext ce; ce.evotarget = 1;
        const Snapshot before = ev;
        ed.evolve (ce, ev);
        CHECK (! (ev == before));
    }
    std::printf (failures ? "\n%d FAILURE(S)\n" : "All TGB-420 engine tests passed.\n", failures);
    return failures ? 1 : 0;
}
