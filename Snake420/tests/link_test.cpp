// SNAKE-420 link modes, engines only:  g++ -std=c++17 -ISource/snake -ISource/tgb tests/link_test.cpp
#include "SnakeEngine.h"
#include "TgbCore.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static snake::EngineParams snakeDefaults()
{
    snake::EngineParams p;
    const float init[16] = { 0.0f, 1.9606f, 3.2677f, 5.2283f, 6.5354f, 7.9953f, 11.7773f, 12.6533f, 14.5696f, 15.4455f, 17.3673f, 19.4228f, 21.0355f, 22.2612f, 25.1113f, 25.5967f };
    for (int i = 0; i < 16; ++i) { p.notes[(size_t) i] = init[i]; p.gates[(size_t) i] = true; p.velocities[(size_t) i] = 64; }
    p.ccEnabled = { false, false, false };
    return p;
}
static int snakePitch (const void* ctx, double ppq) { return static_cast<const snake::Engine*> (ctx)->pitchAt (ppq); }

struct Note { long t; int pitch, vel; bool fromSnake; };

int main()
{
    const double sr = 48000, bpm = 120, spq = 24000; const int block = 512;
    // ---- link 1: Snake pitch -> TGB rhythm. TGB stage 2 is a rest, stage 3 lasts 2 pulses.
    {
        snake::Engine se; se.prepare (sr); tgb::Engine te; te.prepare (sr);
        auto sp = snakeDefaults(); tgb::Params tp;
        tp.s.mode[1] = 0; tp.s.len[2] = 2; tp.s.pitch[3] = 12;     // stage 4 plays an octave above Snake
        tp.extPitch = snakePitch; tp.extCtx = &se;
        std::vector<Note> notes;
        for (long pos = 0; pos < (long) (2 * spq); pos += block)
        {
            const double q = pos / spq;
            se.process (sp, { true, q, bpm }, block, [&] (const snake::MidiEvent& m) { if ((m.status & 0xF0) == 0x90) notes.push_back ({ pos + m.sampleOffset, m.data1, m.data2, true }); });
            te.process (tp, true, q, bpm, block, [&] (const tgb::MidiOut& m) { if ((m.b0 & 0xF0) == 0x90) notes.push_back ({ pos + m.sampleOffset, m.b1, m.b2, false }); });
        }
        std::vector<Note> t; for (auto& n : notes) if (! n.fromSnake) t.push_back (n);
        // TGB pulses: st1 @0, st2 rest @6000, st3 @12000 (2 pulses), st4 @24000, st5..8 @30000..48000
        CHECK (t.size() >= 6);
        if (t.size() >= 3)
        {
            CHECK (t[0].t == 0 && t[0].pitch == 24);          // Snake step 1 = C0
            CHECK (t[1].t == 12000 && t[1].pitch == 28);      // Snake step 3 = E0 at 12000
            CHECK (t[2].t == 24000 && t[2].pitch == 31 + 12); // Snake step 5 = G0, + one octave from TGB stage 4
        }
        for (size_t i = 0; i < t.size() && i < 3; ++i) std::printf ("link1 TGB note %ld %d\n", t[i].t, t[i].pitch);
    }
    // ---- link 2: TGB rhythm clocks Snake. TGB stage 1 = 3 pulses (I), others 1 pulse.
    {
        snake::Engine se; se.prepare (sr); tgb::Engine te; te.prepare (sr);
        auto sp = snakeDefaults(); tgb::Params tp; tp.s.len[0] = 3; tp.s.gate.fill (50);
        std::vector<Note> notes;
        for (long pos = 0; pos < (long) (2 * spq); pos += block)
        {
            const double q = pos / spq;
            te.process (tp, true, q, bpm, block, [] (const tgb::MidiOut&) {});   // TGB notes become triggers only
            std::vector<snake::Engine::Trigger> trig;
            for (auto& x : te.triggers) trig.push_back ({ x.ppq, x.durQ, x.vel });
            se.processTriggers (sp, trig, true, q, bpm, block, [&] (const snake::MidiEvent& m) { if ((m.status & 0xF0) == 0x90) notes.push_back ({ pos + m.sampleOffset, m.data1, m.data2, true }); });
        }
        // cycle = 3 + 7 = 10 pulses: notes at pulses 0,3,4,5,6,7,8,9,10,13 ... ; Snake pitches advance one per trigger
        { int inside = 0; for (auto& n : notes) if (n.t < 48000) ++inside; CHECK (inside == 6); }
        const long expT[] = { 0, 18000, 24000, 30000, 36000 };
        const int expP[] = { 24, 26, 28, 29, 31 };
        for (int i = 0; i < 5 && i < (int) notes.size(); ++i) { CHECK (notes[(size_t) i].t == expT[i]); CHECK (notes[(size_t) i].pitch == expP[i]); }
        for (size_t i = 0; i < notes.size() && i < 5; ++i) std::printf ("link2 Snake note %ld %d\n", notes[i].t, notes[i].pitch);
    }
    std::printf (failures ? "\n%d FAILURE(S)\n" : "All link tests passed.\n", failures);
    return failures ? 1 : 0;
}
