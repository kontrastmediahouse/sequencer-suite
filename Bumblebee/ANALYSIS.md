# Bumblebee: function analysis of Snake + TGB-420, and the fused design

Bumblebee is a single sequencer engine. TGB-420's stage engine is the core, because it has the richer rhythm model. Snake's ideas are built into that engine: its 4×4 grid and snake paths become how stages are ordered, and its per-lane independence becomes a property every layer has. Every function from both devices is listed below with its place in Bumblebee.

## 1. Function map

### Grid, order and rhythm
| Function | Snake | TGB-420 | Bumblebee |
|---|---|---|---|
| Steps / stages | 16 steps on a 4×4 grid | 1–16 stages in a row (8 per page) | **16 stages on a 4×4 grid**; Stages 1–16 sets how many are used |
| Order of play | 14 snake **paths** through the grid | left to right | **Path** (14 shapes) for the sequence, plus an own path per layer |
| Direction | Fwd, Back, Fore/Back, Back/Fore, Random per lane | Fwd, Rev, Ping, Rand for the whole sequence | Fwd, Rev, Ping, Rand for the sequence; each layer can **Follow** or set its own |
| Loop window | Steps + **Shift** (slides the window along the path) | Stages | Stages + **Shift** for the sequence; Steps + Shift per layer |
| Step length | 1 step = lane speed | **1–8 pulses** per stage, 0 = skip | 1–8 pulses per stage, 0 = skip |
| Note behaviour | gate on/off | **Mode**: rest, I, II (÷1–÷4 ratchets), I– (held) | Mode + Divider per stage |
| Clock | per-lane speed 1/32–1/2 | Clock 1/32–1/4 (one pulse) | Clock 1/32–1/4; each layer's **Rate** is Stage (TGB) or 1/32–1/2 (Snake, free running) |
| Cycle | – | count by Stage, or by Pulse 1–64 | same |
| Swing | 0–100 % on the gates lane | 0–100 %, every 2nd pulse | 0–100 %, every 2nd pulse |
| Note length | half a gate step | Gate 5–100 % per stage (100 = tie) | Gate layer |

### Layers (values per stage)
| Function | Snake | TGB-420 | Bumblebee |
|---|---|---|---|
| Pitch | dial 0–83, spread over scale degrees | ±36 semitones, snapped to the nearest scale note | **Pitch** ±36, with Pitch mode **Semitone** (TGB) or **Degree** (Snake-style: the dial counts scale notes) |
| Velocity | lane 0–127 | layer 1–127 | Velocity layer |
| Gate length | – | layer 5–100 % | Gate layer |
| Probability | – | Chance 0–100 %, per pulse or per stage | Chance layer |
| Modulation out | 3 CC lanes (CC#, out min/max, on/off) | Aux: CC / Pitch Bend / Aftertouch, Step/Glide, Learn | **4 MOD lanes (A–D)**, each with CC / Bend / Aftertouch target, CC#, min/max, Step/Glide, Learn, on/off |
| Polymeter | each lane has its own steps, speed, direction, path | each layer can have its own Steps | every layer: **Steps, Path, Shift, Direction, Rate** (linked by default) |
| Octave (Bass) | – | Octave ±2 per stage | Octave per stage (Bass mode) |

### Pitch, scale and play modes
| Function | Snake | TGB-420 | Bumblebee |
|---|---|---|---|
| Scales | 36 + User | Off, Live, Custom (16) | **Off + 37** (Snake's 36 + natural minor) **+ User** |
| Root | Fundamental | root | Root |
| User scale | 12-key keyboard; MIDI chord C3–B3 sets it | – | 12-key keyboard + MIDI chord |
| Transpose | – | ±24 | ±24 |
| Play modes | – | Seq, Bass, Arp (17 styles, Oct, Latch, Retrig) | Seq, Bass, Arp |
| MIDI remote | C4–B4 root, C2 random all, D2 random notes | keys feed Bass / Arp; CC feeds Learn | MIDI In: **Play** (keys for Bass/Arp) or **Remote** (Snake's root/random/chord keys) |
| Thru | – | MIDI Thru (Seq) | MIDI Thru |

### Randomize, patterns, workflow
| Function | Snake | TGB-420 | Bumblebee |
|---|---|---|---|
| Randomize | per lane, within a range (rslider) | dice: Layer / Rhythm / All, Note pools, Notes cap, Oct jumps, hot streak | dice: **Layer / Rhythm / All / Paths**; pools; Notes; Oct; **range per layer** |
| Reset | per lane, all | Seq Reset | per layer + Seq Reset |
| Undo | – | Back / Forward, 16 steps | Back / Forward, 16 steps |
| Patterns | – | 8 slots, recall at cycle end, save/clear/copy | 8 slots |
| Evolve | – | every 8–64 bars: pitch / rhythm / both | same |
| Nudge | – | rotate stages left / right | rotate along the path |
| Export | – | MIDI Clip | MIDI Clip (.mid file / drag into the DAW) |
| Reset on stop | – | yes | yes |

## 2. What was merged rather than copied
- **Snake's gates lane → TGB's stage modes + Chance.** A Snake gate on/off is a stage mode of I or rest. A gate pattern with its own length is the Chance layer at 0/100 % with its own Steps.
- **Snake's custom CC lanes + TGB's Aux → four MOD lanes.** These are the same idea (a value per step to a controller), and both devices' options are kept.
- **Snake's swing (up to 0.488 step) → TGB's swing (up to 0.5 pulse).** These are near-identical; there is one control.
- **Snake's note length (half a gate step) → Gate layer**, default 50 %, which gives the same result.
- **Snake's degree-style pitch dial → Pitch mode Degree.** Semitone mode keeps TGB-420's exact behaviour.

## 3. Compatibility
With default settings (path S1, no shift, all layers linked, Pitch mode Semitone, scale Off, MOD B–D off), Bumblebee is TGB-420. I check this against the original `tgb420.js` engine with the same differential test as the TGB-420 port. Snake's behaviours (paths, shift, per-lane length, speed and direction, degree-mapped scales, user scale, CC lanes) are covered by unit tests.
