# TGB-420 Sequencer v1.0: device analysis

The source is `TGB-420_Sequencer_v1.0.amxd`, a 1.3 MB frozen Max for Live MIDI effect by The Great Beyond, together with its 11-page manual. Almost all of the logic is in one readable JavaScript file, `tgb420.js` (1,670 lines). Thirteen smaller `jsui` scripts draw the interface. The patch itself only does the timing (a quantized `metro`), the playback queue (`coll` → `makenote`, `line` → `midiformat`), Live API observers, and parameter storage.

## How it works
- **Stages** (1–16, shown 8 at a time). Each stage has a **mode**: – rest, I one note, II a note on every pulse (thinned by a ÷1–÷4 divider), or I– one note held for the whole stage. It also has a **length** of 0–8 pulses, where 0 skips the stage. One pulse is one Clock step (1/32 to 1/4).
- **Five layers** per stage: Pitch (±36 st around C3, or ±2 octaves in Bass mode), Velocity 1–127, Gate 5–100 % (100 % ties into the next note), Chance 0–100 % (rolled per pulse or per stage), and Aux 0–127.
- **Polymeter:** any layer can be unlinked to its own 1–16 steps. An unlinked layer advances one step each time the sequence moves to a new stage, and follows the Direction.
- **Direction:** Fwd, Rev, Ping (no repeated ends) or Rand. **Count by Stage** ends the cycle after the last stage; **Count by Pulse** restarts after 1–64 pulses.
- **Play modes:** Seq (each stage plays its own pitch), Bass (one note, set by Bass Note or a key you play, plus octave jumps), and Arp (held keys in the stages' rhythm). Arp has 17 styles, matching Live's Arpeggiator, plus Oct 1–4, Latch and Retrig.
- **Scale:** Off, Live, or Custom (16 scales). Notes snap to the nearest scale note, with ties going down; the dials themselves are never changed.
- **Swing** delays every second pulse by up to half a pulse.
- **Randomizer:** targets are Layer, Rhythm (modes, dividers, pulses, gate, chance) or All. The 8 note pools are Classic, One Note, Power, Tension, Hypnotic, Dark, Scale and Custom. Pool notes outside the scale move *up* to the next scale note. Notes (1–8) caps how many pitches a roll uses, and Oct sets the chance of an octave jump. A roll always keeps at least 3 sounding stages.
- **History:** Back/Forward undo up to 16 steps, covering every roll, recall, save, clear, copy, nudge and Evolve step.
- **Patterns:** 8 slots. A recall while playing waits for the cycle start. **Evolve** makes one small change every 8–64 bars.
- **MIDI Clip** writes one full cycle (until all layers line up, at most 16 bars) into a clip in the Live Set.

## What changed in the VST3 port
| Original (Max for Live) | Port | Why |
|---|---|---|
| Pulses precomputed into a 128-slot queue, ~1 s ahead | computed sample-accurately in the audio block | no lookahead needed in a plug-in |
| Note length and swing in ms at the current tempo | the same values, in quarter notes | identical at a steady tempo |
| Swing parity counted from the moment play starts | parity from the bar grid | the same when you start on a beat; musically correct otherwise |
| Scale mode "Live" follows Live 12's scale | removed (Off / Custom) | VST3 hosts don't expose a scale |
| Aux **Map** drives any Live control | removed; Aux sends CC, Pitch Bend or Aftertouch, with Learn | a plug-in can't control other plug-ins |
| MIDI Clip writes a clip into the Live Set | writes `Music/TGB-420/Seq NN.mid`; drag the button onto a Cubase track | plug-ins can't create host clips. Note probability (chance) has no equivalent in a MIDI file, so all notes are written. |
| Evolve mutates at the exact pulse | applied by the UI thread within about 30 ms of the bar | parameter changes must happen off the audio thread |
| MIDI effect | instrument with MIDI output | Cubase doesn't host third-party VST3 MIDI effects |
| Live's looping keeps the sequence running | same: a Cubase cycle jump doesn't restart the pattern | faithful; Reset on stop still restarts on Stop |
| Push banks, Info View texts, logo animation, hot-streak flames | tooltips and a glowing die after 4 rolls in 5 seconds | not applicable outside Live |

## Verification
- **Differential test.** The original `tgb420.js` engine runs in Node.js with Max's functions stubbed out, alongside the C++ engine, on 61 configurations. These cover random stages (mode, divider, pulses, gate), lengths, directions Fwd/Rev/Ping, transpose, polymeter layer lengths, all clocks, swing, Count by Pulse, scales, and the Seq, Bass and Arp modes with 16 arp styles. **All 61 produce identical note-ons** (pitch, velocity, time within 1 ms) and identical durations. The one exception is where a repeated pitch cuts a tied note short, which the original's `makenote` also does when it plays. Random direction and random arp style can't be compared note for note, because their random numbers differ.
- **Unit tests** cover aux step/glide/pitch bend, the mode II divider, I–, rests and skips, pattern switching exactly at the cycle start, Evolve every 8 bars, note-offs on stop, Bass key override, Arp waiting for keys, scale snapping, clip length (5 stages → 5 bars), the Tension pool in C major (C + D) and Phrygian (C + C#), the 3-sounding-stages rule, nudge, and evolve.
- **Headless processor test:** the default pattern gives 16 notes per bar; a test riff gives 11 notes per bar, as counted by hand; the .mid export writes a file.
- **Build:** the VST3 builds with no warnings from project code and loads in JUCE's VST3 helper (Linux compile check). **It has not yet been built on macOS, run in Cubase, or compared by ear in Ableton.**
