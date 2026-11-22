// Renders the editor to PNGs and runs a headless processor test.
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

static void setP (BeeProcessor& p, const juce::String& name, float plain)
{
    auto* prm = p.apvts.getParameter (bee::ids::id (name));
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static int processorTest()
{
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& proc = *dynamic_cast<BeeProcessor*> (base.get());
    FakePlayHead ph; proc.setPlayHead (&ph);
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    auto runBar = [&] (juce::String& pitches, int& ons, int& offs, int& ccs, double startQ)
    {
        long pos = 0; ons = offs = ccs = 0;
        for (int b = 0; b < 187; ++b)
        {
            juce::MidiBuffer midi;
            ph.ppq = startQ + pos / 24000.0;
            proc.processBlock (buf, midi);
            for (const auto m : midi)
            {
                auto msg = m.getMessage();
                if (msg.isNoteOn()) { ++ons; pitches << msg.getNoteNumber() << " "; }
                if (msg.isNoteOff()) ++offs;
                if (msg.isController()) ++ccs;
            }
            pos += 512;
        }
    };
    juce::String pitches; int ons, offs, ccs;
    runBar (pitches, ons, offs, ccs, 0.0);
    std::printf ("default bar: %d note-ons, %d note-offs, %d CCs\n  pitches: %s\n", ons, offs, ccs, pitches.toRawUTF8());
    const bool ok1 = ons == 16 && offs >= 15;

    // a little riff: stage pitches, a rest, a long stage, swing
    for (int i = 0; i < 8; ++i) setP (proc, "pitch" + juce::String (i + 1), (float) ((i * 3) % 7));
    setP (proc, "mode3", 0); setP (proc, "len5", 3); setP (proc, "mode5", 3); setP (proc, "swing", 50);
    setP (proc, "path", 3); setP (proc, "Lvel_steps", 5); setP (proc, "Lvel_rate", 2); setP (proc, "moda_on", 1);
    setP (proc, "scale", 2); setP (proc, "root", 2);
    juce::String p2; runBar (p2, ons, offs, ccs, 4.0);
    std::printf ("riff bar:    %d note-ons, %d note-offs\n  pitches: %s\n", ons, offs, p2.toRawUTF8());

    auto f = proc.exportClip();
    std::printf ("clip export: %s (%lld bytes)\n", f.getFullPathName().toRawUTF8(), (long long) f.getSize());
    return ok1 && ons > 0 && f.existsAsFile() ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--test") return processorTest();
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& proc = *dynamic_cast<BeeProcessor*> (base.get());
    // a rolled hypnotic riff so the shots show something
    setP (proc, "pool", 4); setP (proc, "notes", 3); setP (proc, "octjump", 20);
    setP (proc, "root", 9); setP (proc, "scale", 37);
    setP (proc, "randtarget", 2); proc.randomize();
    setP (proc, "randtarget", 0);
    setP (proc, "length", 12); setP (proc, "path", 5); setP (proc, "shift", 2);
    setP (proc, "Lvel_steps", 5); setP (proc, "Lvel_path", 4); setP (proc, "Lmoda_rate", 3); setP (proc, "moda_on", 1);
    proc.slotClick (0, 1); proc.slotClick (2, 1);

    struct Shot { int layer, playmode; const char* name; };
    for (auto s : { Shot { 0, 0, "pitch" }, Shot { 1, 0, "velo" }, Shot { 4, 1, "moda_bass" }, Shot { 3, 2, "chance_arp" } })
    {
        setP (proc, "layer", (float) s.layer); setP (proc, "playmode", (float) s.playmode);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1400, 800);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        auto file = outDir.getChildFile (juce::String ("bee_ui_") + s.name + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
    return 0;
}
