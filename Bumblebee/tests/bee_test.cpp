// Bumblebee engine tests for the Snake-side features (TGB-420 parity is the diff test)
#include "BeeCore.h"
#include <cstdio>
#include <set>
using namespace bee;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Ev { long t; int st, d1, d2; };
static std::vector<Ev> run (Params& p, double quarters, Engine* eIn = nullptr)
{
    Engine local; Engine& e = eIn ? *eIn : local; if (! eIn) e.prepare (48000);
    std::vector<Ev> out; const double spq = 24000.0; const long total = (long) (quarters * spq);
    for (long pos = 0; pos < total; pos += 512)
    {
        const int n = (int) std::min<long> (512, total - pos);
        e.process (p, true, pos / spq, 120.0, n, [&] (const MidiOut& m) { out.push_back ({ pos + m.sampleOffset, m.b0 & 0xF0, m.b1, m.b2 }); });
    }
    return out;
}
static std::vector<int> ons (const std::vector<Ev>& v, int what = 1)
{
    std::vector<int> r; for (auto& e : v) if (e.st == 0x90) r.push_back (what == 1 ? e.d1 : e.d2); return r;
}
static Params cellPitches()   // pitch = cell index, so the notes show which cell played
{
    Params p; p.s.length = 16;
    for (int c = 0; c < 16; ++c) p.s.pitch[(size_t) c] = c;
    return p;
}

int main()
{
    // ---- path: S2 plays the grid column by column
    {
        auto p = cellPitches(); p.s.path = 2;
        auto n = ons (run (p, 4.0));
        const int expect[16] = { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 };
        CHECK (n.size() == 16);
        for (size_t i = 0; i < n.size(); ++i) CHECK (n[i] == 60 + expect[i]);
    }
    // ---- shift: 4 stages, shift 100 % -> the last 4 cells of the path (S4 snake: 16 15 14 13)
    {
        auto p = cellPitches(); p.s.path = 6; p.s.length = 4; p.s.shift = 100;
        auto n = ons (run (p, 1.0));
        const int expect[4] = { 15, 14, 13, 12 };
        CHECK (n.size() == 4);
        for (size_t i = 0; i < n.size() && i < 4; ++i) CHECK (n[i] == 60 + expect[i]);
    }
    // ---- a layer with its own path: rhythm on S1, pitch read along S2 columns
    {
        auto p = cellPitches(); p.s.layers[LPitch].path = 2;
        auto n = ons (run (p, 1.0));
        CHECK (n.size() == 4 && n[0] == 60 && n[1] == 64 && n[2] == 68 && n[3] == 72);
    }
    // ---- own direction: pitch layer reversed against a forward rhythm
    {
        auto p = cellPitches(); p.s.length = 4; p.s.layers[LPitch].dir = Rev;
        auto n = ons (run (p, 1.0));
        CHECK (n.size() == 4 && n[0] == 63 && n[1] == 62 && n[2] == 61 && n[3] == 60);
    }
    // ---- polymeter by steps (TGB) and free-running rate (Snake): velocity at 1/8 over 16ths
    {
        Params p; p.s.length = 8;
        for (int c = 0; c < 16; ++c) p.s.vel[(size_t) c] = 10 + c;
        p.s.layers[LVel].rate = 3;   // 1/8
        auto v = ons (run (p, 2.0), 2);
        CHECK (v.size() == 8);
        if (v.size() == 8) { CHECK (v[0] == 10 && v[1] == 10 && v[2] == 11 && v[3] == 11 && v[6] == 13 && v[7] == 13); }
    }
    // ---- Degree pitch mode: the dial counts scale notes (Snake-style), here D dorian
    {
        Params p; p.pitchMode = 1; p.scale = 4; p.root = 2; p.s.length = 8;
        for (int c = 0; c < 8; ++c) p.s.pitch[(size_t) c] = c;
        auto n = ons (run (p, 2.0));
        const int expect[8] = { 62, 64, 65, 67, 69, 71, 72, 74 };
        CHECK (n.size() == 8);
        for (size_t i = 0; i < n.size() && i < 8; ++i) CHECK (n[i] == expect[i]);
        p.s.pitch[0] = -1;   // one degree below the root
        auto m = ons (run (p, 0.25));
        CHECK (! m.empty() && m[0] == 60);
    }
    // ---- User scale (C + G) in Semitone mode: nearest, ties down
    {
        Params p; p.scale = kScaleUser; p.userMask = (1 << 0) | (1 << 7); p.s.length = 4;
        p.s.pitch = { 1, 4, 6, 10 };
        auto n = ons (run (p, 1.0));
        CHECK (n.size() == 4 && n[0] == 60 && n[1] == 67 && n[2] == 67 && n[3] == 72);
    }
    // ---- MOD lanes: B on CC 20 at its own 1/8 rate with an output range
    {
        Params p; p.s.length = 8;
        p.mods[1].on = true; p.mods[1].cc = 20; p.mods[1].lo = 20; p.mods[1].hi = 100;
        for (int c = 0; c < 16; ++c) p.s.mod[1][(size_t) c] = (c % 2) * 127;
        p.s.layers[LModB].rate = 3;
        auto ev = run (p, 2.0);
        std::vector<int> cc; for (auto& e : ev) if (e.st == 0xB0 && e.d1 == 20) cc.push_back (e.d2);
        CHECK (cc.size() == 4);
        for (size_t i = 0; i < cc.size(); ++i) CHECK (cc[i] == (i % 2 ? 100 : 20));
    }
    // ---- pattern slot switches at the cycle start, path included
    {
        Engine e; e.prepare (48000);
        auto p = cellPitches(); p.s.length = 4;
        Snapshot slot = p.s; slot.path = 2;
        run (p, 0.5, &e);
        e.setPendingSlot (&slot);
        std::vector<Ev> out; const double spq = 24000.0;
        for (long pos = 12000; pos < 36000; pos += 512)
        {
            const int n = (int) std::min<long> (512, 36000 - pos);
            e.process (p, true, pos / spq, 120.0, n, [&] (const MidiOut& m) { out.push_back ({ pos + m.sampleOffset, m.b0 & 0xF0, m.b1, m.b2 }); });
        }
        auto n = ons (out);
        CHECK (n.size() == 4 && n[0] == 62 && n[1] == 63 && n[2] == 60 && n[3] == 64);   // switch lands on stage 1
        CHECK (p.s.path == 2);
    }
    // ---- editing: Paths roll, nudge along a path, Degree-mode rolls stay in the scale
    {
        Editor ed (7); EditContext c; Snapshot s;
        bool changed = false;
        for (int r = 0; r < 10 && ! changed; ++r) { Snapshot t = s; c.randtarget = 3; ed.randomize (c, t); changed = t.path != s.path || ! (t.layers == s.layers); }
        CHECK (changed);

        Snapshot n; n.path = 2; n.length = 4;          // S2: cells 0, 4, 8, 12
        for (int i = 0; i < 16; ++i) n.pitch[(size_t) i] = i;
        Editor::nudge (n, 1);
        CHECK (n.pitch[0] == 12 && n.pitch[4] == 0 && n.pitch[8] == 4 && n.pitch[12] == 8 && n.pitch[1] == 1);

        EditContext d; d.pitchMode = 1; d.scale = 2; d.root = 0; d.pool = 6; d.notes = 5; d.randtarget = 0;
        Snapshot ds; ed.randomize (d, ds);
        Params pp; pp.pitchMode = 1; pp.scale = 2; pp.s = ds;
        const auto sc = activeScale (pp);
        for (int v : ds.pitch) CHECK (inScale (sc, degreeNote (sc, v)));
        Snapshot r; Editor::seqReset (r);
        CHECK (r == Snapshot());
    }
    std::printf (failures ? "\n%d FAILURE(S)\n" : "All Bumblebee engine tests passed.\n", failures);
    return failures ? 1 : 0;
}
