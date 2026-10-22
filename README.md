# lp-dnb

A multi-style groovebox for the original Novation **Launchpad Mini** (micro-USB, mk1/mk2), written in C++17.
It started as drum & bass and now has **12 styles and 96 built-in patterns**. Each style has its own drum kit,
bass synth, chord sound, key, swing and effects, and the Launchpad remaps itself for each one.
Everything is synthesised live by default, and any drum can be swapped for a WAV sample of your own.
Every effect has its own light pattern on the pads.

![LP-DNB on Windows](docs/screenshot.png)

You can make your own named patterns (up to 4 bars) under any style, chain them into a song, and they're
saved between sessions. A second MIDI keyboard can play a lead synth over the top, with a choice of 17
sounds, and you can record what you play into the pattern. Record the output to WAV, export patterns as
MIDI files, and sync to other gear with MIDI clock.

## The sound engine

- **Stereo throughout**, with hats, percussion and toms placed across the field, wide unison chords and
  a stereo chorus.
- **Drums**: kicks with a beater click layer, snares with a rattling wire layer, wide claps, band-limited
  (alias-free) hats and cowbells. Each drum can have its own **tune, length and tone**, and any drum can be a
  **WAV sample** instead (single files, or a whole kit folder matched by file name).
- **Bass**: a 24 dB **ladder filter** (303 / Moog style) on the acid, Reese and wobble basses, an
  anti-aliased drive stage, filter updated every sample for smooth sweeps.
- **Chords and leads**: up to **7-voice supersaw** unison per note, spread across the stereo field.
- **Sidechain**: the kick really ducks the bass and chords (an envelope follower on the kick, with a
  release set per style), and you can set how much.
- **Effects**: a smooth 8-line reverb with adjustable size, a tempo-synced ping-pong delay (1/4, dotted 1/8,
  1/8, 1/8 triplet, 1/16) with adjustable echoes, chorus, plus per-sound reverb and delay sends.
- **Master**: gentle EQ, "glue" bus compression, stereo width, and a look-ahead limiter so it never clips.
- **Feel**: humanise (tiny timing and loudness variations), hat-only swing, automatic drum fills every 4, 8
  or 16 bars.
- **Per-step locks**: any step can have its own pitch, length and tone / filter (right-click a step).
- **Timing**: pads, keys and MIDI land with sample accuracy (constant latency, no block jitter); the audio
  buffer size is adjustable, and Windows can use WASAPI exclusive mode for the lowest latency.

On Windows it opens a window with an on-screen Launchpad that mirrors the real one's lights (and can
be clicked), a step sequencer, the effects, the lead instrument, a style browser and a devices panel. Devices are detected
live: plug the Launchpad in (or pull it out and plug it back) at any time and it reconnects by itself.
Other MIDI keyboards and pad controllers are picked up automatically too.

## Styles

| Style | BPM | Key | Sounds | Patterns |
|---|---|---|---|---|
| Drum & Bass | 174 | F minor | Punchy kick, ghost snares, Reese bass, rave stabs | Two-step, Amen roller, Jungle, Liquid, Amen (last bar), Think break, Halftime, Neuro stepper |
| House | 124 | A dorian | 909 kick, clap, shaker, deep sub, organ stabs | Classic, Deep, Jackin', Piano house, Chicago jack, Shuffle, Tech house, Garage house |
| Techno | 132 | E phrygian | Rumbling kick, acid 303 with slides and accents, dub chords | Peak time, Hypnotic, Industrial, Acid, Hard techno, Detroit, Minimal roller, Electro |
| Hip-hop | 90 | D dorian | Dusty kick, swung hats, round bass, Rhodes | Boom bap, Off-kilter, Laid back, Jazzy, Funky Drummer, Impeach, Head nod, Bongo break |
| Trap | 140 | G minor | 808 with pitch drop and slides, hi-hat rolls, bells | Classic, Rolling hats, Drill, Bounce, Triplet flow, Atlanta bounce, Grime hats drill, Rage |
| Dubstep | 140 | F phrygian | Heavy kick, huge snare, tempo-synced wobble bass, dark pad | Half-time, Wobble, Riddim, Dub, Deep dub, Brostep, Tearout, Ghost hats |
| UK Garage | 132 | G minor | Heavy shuffle, rims, organ bass, M1 organ stabs | 2-step, Speed garage, Bumpy, Vocal, Classic 2-step, Future garage, 4x4 garage, Grime |
| Reggaeton | 95 | A minor | Dembow snare, timbales, sub bass, plucks | Dembow, Perreo, Dancehall, Moombah, Dembow timbal, Trapeton, One drop, Cumbia |
| Disco | 118 | E dorian | Tambourine, cowbell, slap bass, strings | Four on the floor, Funk, Nu-disco, Cowbell groove, Philly, Boogie, Italo, Salsoul |
| Synthwave | 100 | A minor | Gated snare, 80s toms, pulsing arp bass, wide pads | Drive, Night drive, Outrun, Ballad, Darksynth, Retro halftime, Power ballad, Chillwave |
| Lo-fi | 78 | D dorian | Soft kit, lazy swing, warm sub, jazzy electric piano | Chill, Study, Rainy, Jazz hop, Dilla swing, Bossa, Brushes, Dusty boom bap |
| Afro House | 122 | G dorian | Congas, shaker, clave, cowbell, marimba | Afro house, Afrobeats, Tribal, Log drum, Amapiano, Afrobeat, Gqom, Afro tech |

Every built-in pattern is two bars long: the second bar adds the little end-of-phrase moves (ghost-note
pickups, an open hat into the next bar, the bass jumping an octave), and hat lines get strong/weak dynamics.

Chords are built from each style's scale, so they always fit the key (minor 7ths, major 9ths and so on
come out naturally). Switching style or pattern happens at the end of the bar, and switching style also
sets that style's tempo.

## Your own patterns

Every style holds up to 24 patterns: its 8 built-in ones plus 16 of your own.

- **New pattern**: the **+** next to the pattern list, or **+ New** in the Sequencer tab. Give it a name
  and start from a copy of the current pattern or from scratch. **Duplicate** makes a copy of whatever's playing.
- **Rename / Delete** work on your own patterns. Built-in ones can be edited too, and **Reset** puts them back.
- **On the Launchpad**: hold **View** and the top half shows styles (rows 1-2) and this style's patterns
  (rows 3-4; yours are amber; BPM-/BPM+ page through if there are more than 16). The **Pat 1-4** buttons step
  through the page of four the current pattern is on.
- **Saving is automatic.** Patterns, edits and settings (style, tempo, volume, audio device, lead sound,
  what each keyboard plays) are written to a readable text file whenever they change:
  - Windows: `%APPDATA%\LP-DNB\lp-dnb-data.txt`
  - macOS: `~/Library/Application Support/LP-DNB/lp-dnb-data.txt`
  - Linux: `~/.config/lp-dnb/lp-dnb-data.txt`

  Patterns in it use the same text format as `src/styles.cpp`, so you can copy them between the two.

## Longer patterns and songs

- **Length**: 1 to 4 bars (Sequencer tab). Making a pattern longer repeats what's there, ready to vary.
  **Showing** picks which bar you're editing (or **Follow** the playhead). On the Launchpad's EDIT view, BPM-/BPM+
  page through the bars of a long pattern.
- **Song** tab: a list of this style's patterns, each played 1-16 times, in order, then looping. Tick
  **Song mode** and press Play.

## Mixer and samples

The **Mixer** tab has a row for every sound in the style (each style keeps its own settings): level, pan,
reverb and delay sends, tune, length, tone and solo. Right-click any slider to reset it. Below it: stereo width,
glue compression, kick ducking, a stereo meter, how hard the limiter is working, and CPU use.

**Source** on each drum row switches it between the synth and a WAV file (8/16/24/32-bit or float, mono or
stereo, any sample rate). **Load kit folder...** picks a folder and matches its files to the style's drums by
name (kick/bd, snare/sd, clap, hat/hh, open/oh, ride, crash, rim, tom, conga, shaker, cowbell...). You can also
drag WAV files or a folder onto the window: onto a row in the Mixer to replace that sound.
Samples are remembered between sessions.

## Recording and exporting

- **Record** (the round button next to Play) writes what you hear to a 24-bit WAV.
- **Export MIDI** (Sequencer tab) saves the pattern as a .mid file with drums (General MIDI notes), bass,
  chords and lead on separate tracks, with the tempo and swing.
- **Rec lead** (Sequencer tab) records the lead keyboard into the pattern's Lead row, snapped to the nearest step.

Files go in `Music\LP-DNB` (Windows) or `~/Music/LP-DNB`.

## MIDI clock

In **Devices**: **Send clock to** another MIDI device (24 ticks per beat plus Start/Stop, locked to the audio
engine), or **Follow clock from** one of your MIDI inputs: LP-DNB takes its tempo, Start and Stop.

## Playing over the top (second MIDI instrument)

Plug in any other MIDI keyboard and it plays the **lead synth** over the beat. In the **Instrument** tab:

- **Sound**: Supersaw lead, Square lead, Brass, Pluck, Electric piano, Organ, Bells, Marimba, Strings,
  Warm pad (all polyphonic); Acid lead, Mono synth lead, Reese bass, 808 bass, Wobble bass (mono, gliding
  between held notes); or the current style's own bass or chord sound.
- **Volume**, **Octave** (-3 to +3) and **Snap to key**, which moves any wrong note to the nearest note in the
  style's key. Orange dots on the keyboard show which notes are in the key.
- Velocity, sustain pedal, pitch bend (+/-2 semitones) and mod wheel (vibrato) all work.
- The on-screen keyboard can be clicked and dragged to try sounds without a MIDI keyboard.
- Each keyboard can instead play the style's **bass** or its **drums**: choose next to its name (Instrument
  or Devices tab). Notes on channel 10 always play drums.
- The lead goes through the same effects as everything else, and each note sends a ripple across the
  Launchpad, placed by pitch.

## Where the patterns come from

The built-in patterns are written for this app's 16-step grid. The second set of four per style follows well-known
grooves and common genre templates: classic breaks (Amen, Think, Funky Drummer, Impeach the President), the dembow
3-3-2 spacing, Chicago/tech/garage house, 2-step and 4x4 garage, grime/drill hi-hat cadences, amapiano log-drum
placement, and so on. Breakdowns used as references:
[FaderPro: 5 kick and snare patterns](https://blog.faderpro.com/techniques/5-kick-and-snare-patterns-you-need-to-know/),
[Ethan Hein: Building the Amen break](https://ethanhein.substack.com/p/building-the-amen-break),
[Amped Studio: how to make a house beat](https://ampedstudio.com/blog/how-tomake-a-house-beat/),
[Attack Magazine: UK drill, Beat Dissected](https://www.attackmagazine.com/technique/beat-dissected/uk-drill/),
[UJAM: how to make a dembow groove](https://www.ujam.com/tutorials/how-to-make-a-dembow-groove/),
[RouteNote: how to make an amapiano beat](https://create.routenote.com/blog/beatmakers-guide-how-to-make-an-amapiano-beat/).

## Build

You need CMake (3.16+) and a C++17 compiler. The libraries it uses (RtMidi for MIDI, miniaudio for
audio, Dear ImGui for the window) are included in `third_party/`, so there is nothing else to download.

**Windows (Visual Studio 2019/2022, "Desktop development with C++")**

```
cmake -S . -B build
cmake --build build --config Release
build\Release\lp-dnb.exe
```

Using the CMake GUI instead: set "Where to build the binaries" to a `build` folder **inside** the
project (e.g. `.../lp-dnb/build`), not the project folder itself. Press Configure, then Generate,
then Open Project and build the `Release` configuration in Visual Studio.

**macOS / Linux** build a console version (same synth, lights and hot-plugging, no window).

**macOS**

```
cmake -S . -B build && cmake --build build -j
./build/lp-dnb
```

**Linux** (install ALSA headers first: `sudo apt install libasound2-dev`)

```
cmake -S . -B build && cmake --build build -j
./build/lp-dnb
```

Close any other app that's using the Launchpad (Ableton, Novation Components). On Windows only one
program can open a MIDI port at a time; LP-DNB keeps trying and connects as soon as it's free.

Console options: `--list` (show MIDI ports), `--style N` (start in style N), `--audio "device name"`,
`--demo out.wav` (render two patterns from every style), `--test [out.wav]` (render all 96 built-in patterns
and print their levels, limiter activity and CPU use; no hardware needed for either).

`lp-dnb-tests` (built alongside) checks the engine offline: WAV loading in every format, kit-folder matching,
sample-accurate timing, sampled drums, the limiter ceiling, stereo output, long patterns and song mode, and that
the save file round-trips exactly.

## Launchpad layout

```
 (Play) (View) (BPM-) (BPM+) (Pat1) (Pat2) (Pat3) (Pat4)        <- round top buttons

 [ ][ ][ ][ ][ ][ ][ ][ ]   rows 1-4: light show                (track 1)
 [ ][ ][ ][ ][ ][ ][ ][ ]     EDIT view: rows 1-2 = 16 steps     (track 2)
 [ ][ ][ ][ ][ ][ ][ ][ ]     hold View: style picker            (track 3)
 [ ][ ][ ][ ][ ][ ][ ][ ]                                        (track 4)
 6 drum sounds + 1 extra + Chord       the style's kit           (track 5)
 8 notes in the style's key            bass                      (track 6)
 8 effects                             hold, per style           (Bass)
 8 effects                             hold, per style           (Chords)
                                                   right column -^
```

- **Play** starts/stops. **View**: tap to flip between PERFORM and EDIT; **hold** it to show the picker:
  styles on rows 1-2 (lit in each style's colour; the current one blinks) and this style's patterns on
  rows 3-4 (built-in green, yours amber).
- **Pat 1–4**: patterns of the current style, four at a time (the page the current pattern is on).
  Switches land on the next bar.
- **Drum row**: the style's six drum tracks, an extra one-shot (crash, cowbell, clap...), and on the
  right the style's chord sound, played on the last note pad you pressed. Hold it to sustain pads.
- **Note row**: one octave of the style's scale on its bass synth.
- **Right column**: PERFORM = mute/unmute each track; EDIT = pick the track to edit.
- **EDIT view**: top two rows are the 16 steps of one bar (BPM-/BPM+ change bar on longer patterns). Tap to toggle. For bass and chords, hold a step and
  press a note pad to set its note.
- **Hold View + Pat N**: copy the current pattern into slot N. **Hold View + right column** (EDIT): clear that track.

## The window (Windows)

- **Top bar**: Play/Stop, Record, style, pattern list (with **+** for a new one), tempo, volume, stereo level meter.
- **Launchpad** (left): a live copy of the lights, labelled for the current style. Click and hold pads
  exactly like the hardware.
- **Sequencer** tab: click a step to toggle it. Right-click for more: accents, ghost notes, 2- and
  3-hit rolls on drums; notes, accents, slides and holds on the bass; chords (shown as i, IV, VII7...)
  and holds on the chord track. M mutes a track; clicking a track name selects it for the Launchpad's
  EDIT view. Above the grid, every pattern of the style and the length / bar selector; below it, Duplicate,
  Rename, Delete, Reset, Clear, Rec lead, Export MIDI, and the Feel settings (humanise, hat swing, fills).
  A small notch on a step means it has its own pitch / length / tone.
- **Mixer** tab: levels, sends and sound controls for every sound, sampled drums, master settings.
- **Effects** tab: the style's 16 effects. Hold a button to play one; hover for a description. Below:
  delay time and echoes, reverb size, chorus.
- **Song** tab: chain patterns into a song.
- **Instrument** tab: the lead synth for a second MIDI keyboard (see above), with a playable keyboard.
- **Styles** tab: all 12 styles with tempo, key and a description. Click to switch.
- **Devices** tab: Launchpad status and port choice (auto-detect by default), other MIDI inputs with an
  on/off tick each and an activity light, and the audio output. Everything is rescanned every second.
  If your chosen audio device is unplugged, sound moves to the system default and switches back
  when it returns. Also: audio buffer size and exclusive mode, MIDI clock in/out, and the recordings folder.
- **Computer keyboard**: Space = play/stop, Z X C V B N M , = drum row, 1–8 = notes,
  Q–I and A–K = hold effects (same order as the pads).

## Other MIDI controllers

Any MIDI input that isn't the Launchpad is connected automatically when plugged in. Notes play the
current style's bass (in its key, folded into range); notes on channel 10 (General MIDI drums, as most
pad controllers send) play the style's kit: 36 kick, 38 second drum, 42 third, 46 fourth, 39/51 fifth,
toms sixth, 49 crash/extra.

## Effects and their lights

Each style puts 16 of these on its two effect rows.

| Effect | Sound | Lights |
|---|---|---|
| Stutter 1/4 … 1/32 | Loops the last slice of audio | Flash + sweep / alternating halves / checkerboard / glitch strobe, all synced to the repeat |
| Tape stop | Audio winds down to a halt | Red stripes that slow down and fade with the tape |
| Half-time | Sequencer runs at half speed | Slow orange breathing glow |
| Reverse | Last beat played backwards | Green scanner running right to left |
| Delay throw | Tempo-synced ping-pong echoes (time set in the Effects tab) | Rings bouncing left/right, fading with each echo |
| Low-pass / High-pass | Resonant filter sweep on the master | A red ceiling closing down / amber floor rising up |
| Bitcrush | Sample-rate + bit reduction | Chunky random 2×2 blocks every 16th |
| Flanger | Jet sweep | Diagonal red/green waves |
| Reverb wash | Big send + longer tail | Sparkles that bloom and diffuse, and fade with the tail |
| Dub siren | Classic swooping siren into the delay | Rotating police-light beams |
| Noise riser | Builds while held (up to ~6 s) | Fills from the bottom, strobing faster; full flash on release (the drop) |
| Air horn | Brassy horn | Concentric squares exploding outward |
| Sidechain pump | Ducks the whole mix on every beat | The grid sucks in on each beat and swells back |
| Trance gate | Chops the mix into 16ths | Columns blink on and off with the gate |
| Lo-fi vinyl | Dark, warbly, crackly | Warm flicker with dust specks |
| Laser zap | Pew-pew every 8th note into the delay | Beams shooting across random rows |

Even with no effects held, the top half is a live level meter per track. Kicks send red ripples up from
the bottom, snares and claps drop amber ones from the top, chords close in from the sides, toms and
congas roll in from the corners, and every pad you touch sends a ripple. Switching style sweeps the
style's colour across the grid. When stopped, a slow wave in the style's colour drifts across, with the odd twinkle.

## Adding your own

Styles and patterns live in `src/styles.cpp` as plain text, one row per track:

```
pat("Two-step", {"x.........x.....",   // drums: x hit  X accent  o ghost  r 2-hit roll  t 3-hit roll
                 "....x.......x...",
                 ...
                 "0--0--2---0--3--",   // bass: 0-9 scale degree, - hold, A-J accent, a-j slide
                 "0-----5-------3-"}), // chords: degree of the chord's root, - hold
```

## Code map

- `src/styles.cpp` – every style: sounds, key, swing, effect layout and patterns
- `src/engine.cpp` – drum/bass/chord/lead voices, sampled drums, sequencer (swing, rolls, slides, locks, fills,
  song mode, lead recording), effects and master chain (audio thread)
- `src/dsp.h` – filters (SVF, ladder), band-limited oscillators, anti-aliased drive, reverb, chorus, compressor, limiter
- `src/sample.cpp` – WAV loading and kit-folder matching
- `src/clock.cpp` – MIDI clock output
- `src/controller.cpp` – button mapping (remapped per style) and all the light patterns (main thread)
- `src/launchpad.cpp` – MIDI hub: hot-plug scanning, Launchpad protocol (double-buffered LED updates), other inputs
- `src/app.cpp` – glues it together: audio device and hot-plug, pattern bank, lead synth input, samples,
  WAV recording, MIDI export, autosave
- `src/storage.cpp` – the save file
- `assets/fonts/` – Nunito (SIL Open Font License), built into the program via `tools/embed_fonts.py`
- `src/gui.cpp`, `src/main_win32.cpp` – the Windows window (Dear ImGui + Direct3D 11)
- `src/main_console.cpp` – console front-end for macOS / Linux, plus the offline demo/test renderer
- `tests/selftest.cpp` – offline engine and file-format checks
