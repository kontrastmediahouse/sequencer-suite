// SNAKE-420: renders the UI to PNGs and runs a headless test of each link mode.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setPpqPosition (ppq); p.setBpm (bpm); p.setIsPlaying (playing); return p; }
};
static void setP (MachineProcessor& p, const juce::String& id, float plain)
{
    auto* prm = p.apvts.getParameter (id); jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static int test()
{
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& m = *dynamic_cast<MachineProcessor*> (base.get());
    FakePlayHead ph; m.setPlayHead (&ph);
    m.setPlayConfigDetails (0, 2, 48000.0, 512);
    m.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    double q0 = 0;
    auto bar = [&] (const char* name)
    {
        juce::String pitches; int ons = 0, offs = 0, ccs = 0; long pos = 0;
        for (int b = 0; b < 187; ++b)
        {
            juce::MidiBuffer midi; ph.ppq = q0 + pos / 24000.0;
            m.processBlock (buf, midi);
            for (const auto e : midi)
            {
                const auto msg = e.getMessage();
                if (msg.isNoteOn()) { ++ons; pitches << msg.getNoteNumber() << " "; }
                if (msg.isNoteOff()) ++offs;
                if (msg.isController()) ++ccs;
            }
            pos += 512;
        }
        q0 += 4.0;
        std::printf ("%-28s %2d ons %2d offs %2d CCs | %s\n", name, ons, offs, ccs, pitches.toRawUTF8());
        return ons;
    };
    // TGB: stage 2 rest, stage 3 two pulses, stage 4 an octave up
    setP (m, "tgb_mode2", 0); setP (m, "tgb_len3", 2); setP (m, "tgb_pitch4", 12);
    const int a = bar ("Off: both, merged");
    setP (m, s420::ids::snakeOut, 0);
    const int b = bar ("Off: TGB only");
    setP (m, s420::ids::link, 1);
    const int c = bar ("Snake pitch > TGB");
    setP (m, s420::ids::link, 2); setP (m, s420::ids::snakeOut, 1);
    const int d = bar ("TGB clock > Snake");
    setP (m, s420::ids::scale, 36); setP (m, s420::ids::root, 9);  // global A natural minor
    setP (m, s420::ids::link, 0); setP (m, s420::ids::snakeOut, 1); setP (m, s420::ids::tgbOut, 0);
    const int e = bar ("Snake only, global A minor");
    // state round trip
    juce::MemoryBlock mb; m.getStateInformation (mb);
    std::unique_ptr<juce::AudioProcessor> base2 (createPluginFilter());
    base2->setStateInformation (mb.getData(), (int) mb.getSize());
    auto& m2 = *dynamic_cast<MachineProcessor*> (base2.get());
    const bool stateOk = (int) m2.apvts.getRawParameterValue (s420::ids::scale)->load() == 36
                      && (int) m2.apvts.getRawParameterValue ("tgb_len3")->load() == 2;
    std::printf ("state round trip: %s\n", stateOk ? "ok" : "FAILED");
    return (a == b + 16 && b > 0 && std::abs (c - b) <= 1 && d > 0 && e == 16 && stateOk) ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--test") return test();
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& m = *dynamic_cast<MachineProcessor*> (base.get());
    setP (m, s420::ids::link, 2); setP (m, s420::ids::scale, 36); setP (m, s420::ids::root, 9);
    setP (m, "tgb_pool", 4); setP (m, "tgb_randtarget", 1); m.tgb.randomize();
    for (int page : { 1, 0 })
    {
        m.selectedModule = page;
        std::unique_ptr<juce::AudioProcessorEditor> ed (m.createEditor());
        ed->setSize (MachineContent::W, MachineContent::H);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.25f);
        auto f = outDir.getChildFile (page ? "s420_tgb_page.png" : "s420_snake_page.png");
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
