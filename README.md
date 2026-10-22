# lp-dnb

A drum & bass groovebox for the original Novation **Launchpad Mini** (micro-USB, mk1/mk2), written in C++17.
Everything is synthesised live (no samples): punchy kick, crisp snares, 808-style metal hats and ride,
a detuned Reese bass with a sine sub, Fm9 chord stabs, plus 16 hold-to-play performance effects that
each have their own light pattern on the pads.

## Build

You need CMake (3.16+), a C++17 compiler and git. The two dependencies (RtMidi for MIDI,
miniaudio for audio) are downloaded automatically when you configure.

**Windows (Visual Studio 2019/2022, "Desktop development with C++")**

```
cmake -S . -B build
cmake --build build --config Release
build\Release\lp-dnb.exe
```

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

Close any other app that's using the Launchpad (Ableton, Novation Components) first — on Windows only
one program can open a MIDI port at a time.

Options: `--list` (show MIDI ports), `--in N --out N` (pick ports manually if auto-detect fails),
`--bpm 174`, `--fps 30` (LED refresh rate; drop to 20 if the lights stutter),
`--demo out.wav` (render a 24-bar demo with scripted effects, no hardware needed).

## Layout

```
 (Play) (View) (BPM-) (BPM+) (Pat1) (Pat2) (Pat3) (Pat4)        <- round top buttons

 [ ][ ][ ][ ][ ][ ][ ][ ]   rows 1-4: light show               (Kick)
 [ ][ ][ ][ ][ ][ ][ ][ ]            (EDIT view: rows 1-2 =     (Snare)
 [ ][ ][ ][ ][ ][ ][ ][ ]             16 steps of the track)     (Ghost)
 [ ][ ][ ][ ][ ][ ][ ][ ]                                        (Hat)
 Kick Snare Ghost Hat OHat Ride Crash Stab        drums          (OHat)
 F    G     Ab    Bb  C    Db   Eb    F           Reese bass     (Ride)
 St¼  St⅛   St1/16 St1/32 Tape HalfT Rev Delay   hold for FX    (Bass)
 LPF  HPF   Crush Flange Verb Siren Riser Horn   hold for FX    (Stab)
                                                   right column -^
```

- **Play** starts/stops the sequencer. **View** flips between PERFORM and EDIT.
- **Pat 1–4**: Two-step, Amen roller, Jungle, Liquid. Switches land on the next bar.
- **Right column**: PERFORM = mute/unmute each track; EDIT = pick the track to edit.
- **EDIT view**: top two rows are the 16 steps. Tap to toggle. For the bass track, hold a step and
  press a bass pad to set its note (a new bass step uses the last bass pad you played).
- **Hold View + Pat N**: copy the current pattern into slot N. **Hold View + right column** (EDIT): clear that track.

## The effects and their lights

| Pad | Sound | Lights |
|---|---|---|
| Stutter 1/4 … 1/32 | Loops the last slice of audio | Flash + sweep / alternating halves / checkerboard / glitch strobe, all synced to the repeat |
| Tape stop | Audio winds down to a halt | Red stripes that slow down and fade with the tape |
| Half-time | Sequencer runs at half speed | Slow orange breathing glow |
| Reverse | Last beat played backwards | Green scanner running right to left |
| Delay throw | Dotted-⅛ ping-pong echoes | Rings bouncing left/right, fading with each echo |
| LPF / HPF | Resonant filter sweep on the master | A red ceiling closing down / amber floor rising up |
| Bitcrush | Sample-rate + bit reduction | Chunky random 2×2 blocks every 16th |
| Flanger | Jet sweep | Diagonal red/green waves |
| Reverb wash | Big send + longer tail | Sparkles that bloom and diffuse, and fade with the tail |
| Dub siren | Classic swooping siren into the delay | Rotating police-light beams |
| Noise riser | Builds while held (up to ~6 s) | Fills from the bottom, strobing faster; full flash on release (the drop) |
| Air horn | Brassy horn | Concentric squares exploding outward |

Even with no effects held, the top half is a live level meter per track, kicks send red ripples up from
the bottom, snares drop amber ones from the top, stabs close in from the sides, and every pad you
touch sends a ripple. When stopped, it idles with a slow plasma.

## Code map

- `src/engine.cpp` – voices, sequencer, master FX chain (audio thread)
- `src/controller.cpp` – button mapping and all the light patterns (main thread)
- `src/launchpad.cpp` – Launchpad Mini protocol: double-buffered rapid LED updates
- `src/patterns.h` – the four starter patterns as text; easy to edit
