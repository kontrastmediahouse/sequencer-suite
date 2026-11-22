// Renders the plug-in editor to PNG files (one per lane) without a host.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

// Fake host transport
struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setPpqPosition (ppq); p.setBpm (bpm); p.setIsPlaying (playing); return p;
    }
};

static int processorTest()
{
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    FakePlayHead ph; proc->setPlayHead (&ph);
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    int ons = 0, offs = 0, ccs = 0; juce::String pitches;
    long pos = 0;
    for (int b = 0; b < 187; ++b) // 1 bar at 120 bpm (96000 samples)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 74, (juce::uint8) 100), 0); // D4 -> root D
        ph.ppq = pos / 24000.0;
        proc->processBlock (buf, midi);
        for (const auto m : midi)
        {
            auto msg = m.getMessage();
            if (msg.isNoteOn()) { ++ons; pitches << msg.getNoteNumber() << " "; }
            if (msg.isNoteOff()) ++offs;
            if (msg.isController()) ++ccs;
        }
        pos += 512;
    }
    std::printf ("processor test: %d note-ons, %d note-offs, %d CCs\npitches: %s\n", ons, offs, ccs, pitches.toRawUTF8());
    return ons == 16 ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--test") return processorTest();
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto* sp = dynamic_cast<SnakeProcessor*> (proc.get());

    // make the shots a little more interesting
    sp->apvts.getParameter ("notes_shape")->setValueNotifyingHost (sp->apvts.getParameter ("notes_shape")->convertTo0to1 (10.0f));
    sp->apvts.getParameter ("notes_len")->setValueNotifyingHost (sp->apvts.getParameter ("notes_len")->convertTo0to1 (12.0f));
    sp->apvts.getParameter ("notes_shift")->setValueNotifyingHost (sp->apvts.getParameter ("notes_shift")->convertTo0to1 (50.0f));
    for (int k : { 0, 2, 3, 7, 10 })
        sp->apvts.getParameter (snake::ids::userKey (k))->setValueNotifyingHost (1.0f);
    for (int s : { 3, 6, 11, 14 })
        sp->apvts.getParameter (snake::ids::stepParam (snake::Gates, s))->setValueNotifyingHost (0.0f);

    for (int lane : { 0, 1, 3 })
    {
        sp->selectedLane = lane;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1100, 580);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("snake_ui_lane" + juce::String (lane) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
