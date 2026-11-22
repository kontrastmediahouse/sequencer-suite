# MDD Snake v3.2.3: device analysis

Source: `MDD___SNAKE_V3.2.3.amxd`, a frozen Max 8.1.7 MIDI device of 3.7 MB. Once unpacked, it contains 192 sub-patchers and about 4,700 objects, plus 14 SVG icons for the shapes. It has no gen~, no JavaScript and no third-party externals. Everything is built from standard Max/MSP objects, so the port could be an exact logic rewrite.

## 1. Signal flow

```
 transport ──► [midi seq] x6 ──► step 0..L-1 ──► + shift ──► [Snake Core] ──► cell 1..16
 (live.step "time", 10-tick metro)                 (shape lookup)              │
                                                                               ▼
   NOTES lane ── dial (0-83) ─► [quantize] ─► latched pitch ─┐
   VELO  lane ── dial (0-127) ──────────────► latched vel ───┤
   GATES lane ── toggle ─► open gate~ ─► [p swing] ─► trigger ┴─► [del 1] ► flush ► noteout
                                                    └► [del 64n..4n] ► note-off (flush)
   CUSTOM 1-3 ── dial (0-127) ─► line~ 10 ms ─► live.remote~ (mapped Live parameter)
```

There are **six lanes**, each with 16 steps on a 4×4 grid: Notes, Gates, Velocities, and Custom 1–3. Each lane has its own clock and settings. **The Gates lane is the clock for notes.** Each ON gate step fires one note, using the pitch and velocity that the Notes and Velocities lanes are holding at that moment. Because each lane can have a different speed, length, direction and shape, the lanes drift against each other into polymetric patterns. That drift is the core idea of the device.

## 2. Per-lane controls

| Control | Values | Behaviour (from the patch) |
|---|---|---|
| Direction | forwards, backwards, fore/back, back/fore, random | sent to `live.step direction` |
| Speed | 32n 16n 8n 4n 2n (default 16n) | `live.step interval`, locked to the transport |
| Steps | 1–16 | `live.step loop 1 L` |
| Shift | 0–100 % | `scale 0 100 0 (16-L)`, added to the step index. It slides the loop window along the snake path. |
| Shape | 14 snakes | `p Snake Core`: maps each step to a grid cell (table below) |
| Random / Reset | buttons | `random 127` scaled into the lane's range slider, or the dial's initial value. Gates use `random 2`. |
| Random range | `rslider` | Notes 0–83, the others 0–128 |

## 3. Snake shapes

Each row gives the step number at which each cell plays. Cells are numbered row by row: 1–4 on the top row, then 5–8, 9–12 and 13–16.

| # | Icon | Cells 1…16 → step |
|---|---|---|
| 1 | S1 | 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 (rows) |
| 2 | S1R | 13 14 15 16 9 10 11 12 5 6 7 8 1 2 3 4 (rows, bottom up) |
| 3 | S2 | 1 5 9 13 2 6 10 14 3 7 11 15 4 8 12 16 (columns) |
| 4 | S2R | 4 8 12 16 3 7 11 15 2 6 10 14 1 5 9 13 |
| 5 | S3 | 1 8 9 16 2 7 10 15 3 6 11 14 4 5 12 13 (vertical snake) |
| 6 | S3R | 4 5 12 13 3 6 11 14 2 7 10 15 1 8 9 16 |
| 7 | S4 | 1 2 3 4 8 7 6 5 9 10 11 12 16 15 14 13 (horizontal snake) |
| 8 | S4R | 4 3 2 1 5 6 7 8 12 11 10 9 13 14 15 16 |
| 9 | S5 | 1 2 6 7 3 5 8 13 4 9 12 14 10 11 15 16 (diagonal zig-zag) |
| 10 | S5R | 10 11 15 16 4 9 12 14 3 5 8 13 1 2 6 7 |
| 11–14 | S6a–d | inward spirals from each corner |

## 4. Note quantising (`p quantize` → `p Conversion`)

The note dial has a range of 0–83, which covers 7 octaves. For a dial value `v`:

```
octave = int(v) / 12                         -> offset −36…+36 around MIDI 60
pct    = (v − 12·octave) / 11 · 100
degree = int(pct · N / 100), clipped to N−1  (N = number of notes in the scale)
note   = 60 − 36 + 12·octave + scale[degree] + root
```

Each octave of dial travel is spread evenly across the notes of the selected scale. The default dials produce C0 D0 E0 F0 … C1 D1 in C major, so the factory pattern is low. Raise the dials or randomise them to move it up.

There are 36 scale tables plus a User scale. The **"Minor" table in the original is `0 2 3 5 7 9 11`, which is melodic minor, not natural minor.** I kept it as-is so existing patterns sound the same.

## 5. Swing and gate timing (`p swing`)

A `phasor~` locked to the transport runs over **two gate steps**. Four comparators combine into pulses that start at phase 0 and at phase *s*:

```
pulse = | (φ>0) − (φ>0.25) − (φ>s) + (φ>s+0.25) |,   s = zmap(swing, 0..1 → 0.5..0.744)
```

An ON gate step opens the `gate~`, the next pulse edge fires the note, and a delay closes the gate after the note length. The result is that even steps play on the grid and odd steps are delayed by up to **0.488 of a step** at 100 % swing. The note length is always **half a gate step** (`Direction_convert`: 64n for 32n steps … 4n for 2n steps).

## 6. Hidden MIDI-input features

| Input | Patch | Effect |
|---|---|---|
| notes 72–83 | `p midiin` | set Fondamental (root) |
| note 48 | main `== 48` | Random All |
| note 50 | `== 50` inside the bpatcher | Random Notes lane |
| notes 60–71, with "Midi In C3 to B3" on | `p Midi_In` (borax/bag) | held chord → User scale; a new chord replaces the previous one once all keys have been released |

Incoming notes are **not** passed through.

## 7. What changed in the VST3 port

| Original | Port | Why |
|---|---|---|
| Custom lanes → `live.remote~` on any Live parameter | Custom lanes → **MIDI CC**, with number, min/max and on/off | VST3 plug-ins cannot control other plug-ins |
| `live.remote~` 10 ms ramp | stepped CC values | the receiving synth smooths them |
| MIDI device | **instrument with MIDI output** | Cubase does not host third-party VST3 MIDI effects in MIDI inserts |
| `live.step` pendulum/random behaviour | reimplemented: fore/back = 0…L-1…1, with no repeated end steps; random = new value each step | **check by ear against Live.** These are the only internal behaviours not visible in the patch. |
| Timing from a 10-tick polling metro (~5 ms jitter) | sample-accurate from the host's song position | tighter timing, same grid |
| live.banks, Push mappings, the Max `pattrstorage` | dropped. All controls are VST3 parameters (automatable and saved with the project). | not applicable outside Live |
| UI: Live dials and a missing `MDD_FONT-01.png` background | new vintage-hardware UI with the same 4 sections and a live snake-path display | requested redesign |

## 8. Verified

- Engine unit tests pass. They cover the default pattern's pitches and timing, swing offsets, gate-lane shapes, length/shift windows, user scale with root, CC scaling, velocity-0 muting, and loop jumps. Output is identical at block sizes of 64, 1000 and 4096.
- In a headless processor test, the VST3 wrapper emits 16 notes per bar with matching note-offs and 3 CC lanes.
- The VST3 builds cleanly (on Linux, as a compile check) and loads in JUCE's VST3 helper. **It has not yet been built on macOS or run in Cubase.** That is the next step on your machine.
