**Download `LP-DNB-windows.zip` below**, unzip it anywhere and double-click `lp-dnb.exe` or `lp-tenori.exe`. Nothing to install (64-bit Windows 10/11). The programs aren't signed, so the first time Windows may say "Windows protected your PC": click **More info**, then **Run anyway**.

## New: lp-tenori
A Tenori-on style sequencer for the Launchpad Mini, on LP-DNB's sound engine.

- **8 layers** playing at once, each with its own instrument (the style's drums, bass or chords, or its own synth from 16 sounds), octave, loop length and volume
- **4 modes**: Score (step grid, 1-16 step loops per layer), Bounce (balls that play as they land), Path (a light travels the route you draw), Push (hold to repeat, from once a bar to 1/64 notes)
- Ripples of light across the Launchpad for every note, and a Shift page for every setting on the hardware
- **Windows window**: clickable on-screen Launchpad, layer cards, a big editor for every mode, instrument picker, effects, mixer and devices
- Saves automatically, and leaves lp-dnb's own patterns and settings alone

![lp-tenori](https://raw.githubusercontent.com/shrieeiee/LP-DNB/main/docs/lp-tenori.png)

## Also in this release
- Launchpad lights are sent from their own thread, so a slow Launchpad driver can no longer freeze the window
- Freeze log: if a window ever stops responding, `%APPDATA%\LP-DNB\lp-dnb-freeze-log.txt` (or `lp-tenori-freeze-log.txt`) records what it was doing, including where in the code. Please attach it to an issue.
- The sound engine schedules notes ahead with sample-accurate timing, and has 8 extra synths for lp-tenori's layers
- MIDI and the Launchpad keep running while you drag the window or a file dialog is open

Built by GitHub Actions (Visual Studio) from commit COMMIT. `LP-DNB-windows-symbols.zip` holds the matching debug symbols, for reading freeze logs. Licences: `LICENSE` (MIT) and `THIRD-PARTY-NOTICES.txt` in the zip.
