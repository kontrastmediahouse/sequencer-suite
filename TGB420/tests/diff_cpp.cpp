// Differential driver: reads a config on stdin, prints note-ons as "ms pitch vel durMs"
#include "TgbCore.h"
#include <iostream>
#include <sstream>
#include <map>
#include <cstdio>
using namespace tgb;
int main()
{
    Params p; std::vector<int> heldNotes; double arpSeed = 1; double quarters = 16; std::string line;
    while (std::getline (std::cin, line))
    {
        std::istringstream is (line); std::string key; is >> key;
        auto arr = [&] (std::array<int, NS>& a) { for (int i = 0; i < NS; ++i) is >> a[(size_t) i]; };
        if (key == "pitch") arr (p.s.pitch); else if (key == "oct") arr (p.s.oct); else if (key == "vel") arr (p.s.vel);
        else if (key == "gate") arr (p.s.gate); else if (key == "chance") arr (p.s.chance); else if (key == "aux") arr (p.s.aux);
        else if (key == "mode") arr (p.s.mode); else if (key == "div") arr (p.s.div); else if (key == "len") arr (p.s.len);
        else if (key == "length") is >> p.s.length; else if (key == "direction") is >> p.s.direction;
        else if (key == "transpose") is >> p.s.transpose;
        else if (key == "llen") for (auto& x : p.s.llen) is >> x;
        else if (key == "playmode") is >> p.playmode; else if (key == "clock") is >> p.clock; else if (key == "swing") is >> p.swing;
        else if (key == "bassnote") is >> p.bassnote; else if (key == "arporder") is >> p.arporder; else if (key == "arpoct") is >> p.arpoct;
        else if (key == "countby") is >> p.countby; else if (key == "pulses") is >> p.pulses;
        else if (key == "scalemode") is >> p.scalemode; else if (key == "root") is >> p.root; else if (key == "scale") is >> p.scale;
        else if (key == "held") { int n; while (is >> n) heldNotes.push_back (n); }
        else if (key == "arpseed") is >> arpSeed; else if (key == "quarters") is >> quarters;
    }
    const double sr = 48000, bpm = 120; const int block = 512;
    Engine e; e.prepare (sr);
    for (int n : heldNotes) e.noteIn (n, 100, p);
    e.setArpSeed (arpSeed);
    std::map<int, long> onAt; std::map<int, int> onVel;
    struct R { long on; int pitch, vel; long off; };
    std::vector<R> rs;
    const long total = (long) (quarters * sr * 60 / bpm);
    long pos = 0;
    auto handle = [&] (const MidiOut& m)
    {
        const long t = pos + m.sampleOffset; const int st = m.b0 & 0xF0;
        if (st == 0x90) rs.push_back ({ t, m.b1, m.b2, -1 });
        else if (st == 0x80) for (auto it = rs.rbegin(); it != rs.rend(); ++it) if (it->pitch == m.b1 && it->off < 0) { it->off = t; break; }
    };
    for (; pos < total + 48000; pos += block)
        e.process (p, true, pos / (sr * 60 / bpm), bpm, block, handle);
    for (auto& r : rs) if (r.on < total) std::printf ("%.1f %d %d %.1f\n", r.on / 48.0, r.pitch, r.vel, (r.off - r.on) / 48.0);
}
