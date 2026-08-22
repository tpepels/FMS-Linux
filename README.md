# FMS Linux

FMS Linux is a native desktop interpretation of the five-track FM step sequencer described in the supplied FMS manual. It runs directly on Linux—no GBA ROM or emulator is involved—and keeps the original instrument's central idea: every step owns its sound. The desktop workflow now combines that immediacy with deeper, M8-inspired sound design and an ambient-friendly path from first trigger to evolving loop.

The application combines four hybrid FM tracks with one PSG-style LFSR noise track, a polymetric 16-step sequencer, per-step synthesis, pattern memory, snapshots, algorithmic echo, transpose lanes, and per-track step modulators. Every FM step can use the original two-operator engine or an advanced four-operator engine with 12 algorithms, six oscillator shapes, ADSR, four modulation slots, multimode filtering, drive, and stereo unison.

## Interactive field guide

Open [docs/guide.html](docs/guide.html) in a browser for an interactive walkthrough: a keyboard-operable grid playground, a stateful seven-view Practice Console for synthesis/effects/pattern workflows, palette and snapshot exercises, guarded project actions, a searchable control reference, and an actual application render.

## Build and run

The only runtime dependency is SDL2.

On Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libsdl2-dev
make
./fms-linux
```

On Fedora:

```sh
sudo dnf install gcc-c++ make pkgconf-pkg-config SDL2-devel
make
./fms-linux
```

Install a desktop launcher and scalable icon with:

```sh
sudo make install
```

`PREFIX` and `DESTDIR` are supported for packaging. For example, `make PREFIX="$HOME/.local" install` installs for the current user.

## Essential controls

- `Space` — start or stop the sequencer
- Arrow keys — move between tracks and steps
- `Shift+Arrow keys` — create or extend a rectangular range
- `F5` — cycle edit scope through step/range, track, and all tracks
- `F6` — toggle the context-sensitive key-hints side panel
- `F7` — dismiss or reopen the four-step ambient-loop guide
- `F8` — switch Data between Perform and Manage
- `F9` — compare the selected Grid parameter across every compatible track
- `Enter` — place or remove a trigger
- `Delete` / `Backspace` — clear the selected step
- `[` / `]` — select the parameter to edit
- `-` / `=` — change its value; hold `Shift` for coarse changes
- `1`–`5` — jump to a track
- `,` / `.` — lower or raise BPM; hold `Shift` for 10 BPM steps
- `Alt+Left/Right` — change the selected track's rate
- `Alt+Up/Down` — change shuffle; add `Ctrl` to change all tracks
- `L` / `Shift+L` — increase or decrease track length
- `D` / `Shift+D` — cycle direction for the selected track / all tracks
- `O` / `Shift+O` — rotate a track or range forward / backward; add `Ctrl` for all tracks
- `Tab` / `Shift+Tab` — move between Grid, Synth, Echo, Transpose, Mod, Scale, and Data
- `C` / `X` / `V` — copy, cut, or paste a step or rectangular range
- `T` — toggle a trigless step
- `R` — incrementally randomize the selected parameter over the active scope
- `Shift+R` — generate a new random pattern for the selected track
- `M` / `Shift+M` — mute or solo the selected track
- `U` — unmute and clear solo on every track
- `P` — open the FM or noise sound palette
- `S` — capture or recall the performance snapshot
- `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` — undo / redo
- `Ctrl+K` — open the command palette
- `Ctrl+N` — open Project actions with Start New selected
- `Ctrl+Shift+Backspace` — open Project actions with Clear Working Tracks selected
- `Ctrl+S` — save immediately
- `Ctrl+Shift+S` — Save As to a named managed project
- `F1` or `?` — open the in-app control reference
- `F2` / `F3` — change theme or accent color
- `F4` — open controller mapping; mappings are saved with the project

The Synth view opens in **Basic** mode: 12 tactile controls for engine, algorithm, timbre, attack, release, cutoff, resonance, motion rate/depth, drive, detune, and stereo space sit beside a live four-operator routing diagram. Click, wheel, or use the keyboard to turn them; edits preview immediately. Press `B` for **Deep**, where all 50 engine fields are grouped into Operators, Envelope, Filter/Drive, Unison, and Modulation Matrix sections. `Page Up` / `Page Down` changes the step without leaving the view, and range/track/all scope remains available in both modes.

In the Palette overlay, `Enter` recalls a sound, `Shift+Enter` stores the selected step's sound, and `Ctrl+Enter` applies it to the whole track. `X` and `?` clear or generate a sound; slots `0`–`D` are the 14 persistent user sounds for the current FM/noise engine. Palette actions copy sound design only, leaving sequence triggers, notes, levels, panning, and timing intact.

The Data view separates live use from project curation. **Perform** keeps Load, Queue, Save, Target, and Mode visible. **Manage** adds the `X`/`?` operation slots, 12-character pattern names, five pattern colors, and bank controls. Use `V` or `F8` to switch.

Core Data controls are:

- `A` — target the selected track or the whole five-track column
- `I` — choose load-in-place or track-local load-and-reset
- `Enter` / `Shift+Enter` / `Q` — load, save, or cue the selected slot/column
- Select `X` / `?` — clear or randomize the current target
- `N` / `K` — rename or lock the selected bank
- `E` / `C` in Manage — name or color the selected pattern column
- `B` / `G` — recall bank BPM / scale; hold `Shift` to store or `Alt` for timed recall
- `Ctrl+B` / `Ctrl+G` — arm stored BPM / scale to change atomically with the next cue

The header **Project** menu offers Start New, Clear Working Tracks, Save, Save As, and Open Recent. You can also drop any `.fms` file onto the window to open it from outside the managed project library. New and Clear require a second confirmation. Start New preserves dirty work before creating a collision-safe `untitled` project and carries theme, accent, controller, and onboarding preferences forward. Open also preserves dirty work first; if the active target is unreadable or unwritable, FMS keeps the original untouched and writes a visible recovery project instead. A failed preservation cancels the transition.

The interface is mouse-aware: click tracks, steps, view tabs, synth macros, pattern actions, and overlay slots; use the wheel to adjust the control underneath it. Delayed hover labels explain unfamiliar regions. Single-track cues change at that track's next available nominal loop boundary. Whole-column cues and their optional BPM/scale payload change atomically at the next global 16-step boundary. A negative microtime offset on an incoming pattern's first step is clipped to its transition boundary so the new pattern never sounds early. On wide windows, `F6` reserves a learning inspector beside the workspace; compact windows use a non-modal overlay. The persistent status strip keeps scope, dirty/saved state, queue, mute/solo, snapshot, palette, and clipboard state visible.

## Instrument behavior

Each legacy FM trigger stores its own amplitude attack/hold/release, fractional modulator ratio, modulation depth and feedback, modulation envelope, pitch sweep, routing mode, note, level, pan, portamento, trigger condition, microtiming, chord allocation, echo send, and transpose enable state. Trigless steps update a sounding voice without resetting its phase or envelopes.

Advanced FM is opt-in per step, so old sounds retain the original rendering path. Its four operators each have waveform, fractional ratio, level, feedback, and fine detune. Twelve routing algorithms feed an ADSR amplifier, four freely routed LFO/envelope modulation slots, low/high/band/notch filtering with resonance, soft/hard/fold drive, and one-to-four-voice stereo unison. Advanced parameters are available to range, track, all-track, palette, pattern, snapshot, and preview workflows just like legacy fields.

Track clocks have independent length, rate, traversal direction, and shuffle. The audio callback advances those clocks at sample resolution. Chord tones share the four-voice FM pool; the noise track uses a configurable 15-bit or narrow 7-bit LFSR. Stereo panning is deliberately discrete: left, center, or right.

The included starter pattern is audible immediately after pressing `Space`. It can be cleared, changed, or replaced with the track randomizer.

## Saving

Dirty state is saved automatically on exit and before destructive project transitions. `Ctrl+S` saves the active target, while `Ctrl+Shift+S` creates a named project without overwriting an existing file. The default session follows the XDG base-directory convention:

```text
$XDG_DATA_HOME/fms-linux/state.bin
```

When `XDG_DATA_HOME` is unset, FMS uses `~/.local/share/fms-linux/state.bin`; named and recovery projects live below `~/.local/share/fms-linux/projects/`. Saves are versioned, checksummed, flushed, and published atomically so an interrupted write does not damage the previous session. Format 1.2 persists advanced synthesis, controller mappings, shared pattern names/colors, and onboarding preference. Existing 1.0 and 1.1 files migrate with sound-compatible defaults. Use `--save-path FILE` to open or keep a separate set.

## Verification and command-line options

```sh
make test          # model, migration/save, DSP/transport, UI-event, and render tests
make screenshot    # headless UI render to fms-ui.bmp
make audio-smoke   # exercise the real-time engine with SDL's dummy driver
./fms-linux --help
```

Useful launch flags include `--play`, `--no-audio`, `--run-for SEC`, `--screenshot FILE`, and `--save-path FILE`.

## Project layout

- `src/model.*` — instrument state, defaults, scale/rate helpers, randomization
- `src/audio.*` — SDL audio device, legacy/advanced voice DSP, event queues, track clocks
- `src/ui.*` — SDL renderer, embedded pixel type, range/palette/Data/controller workflows
- `src/persistence.*` — versioned on-disk encoding and atomic writes
- `packaging/` — freedesktop launcher and icon

FMS Linux uses no web view, game-console emulator, plug-in host, or bundled binary assets. The interface, sequencer, synthesis, and persistence layers are native C++20 code.

## Current scope

This release implements the supplied manual's core instrument plus its range, all-track, palette, column-pattern, bank, load-mode, controller, recovery, and project workflows with desktop-native controls. Console-specific ROM, cartridge-save, flash/link, and GBA deployment procedures do not apply. External clock sync and the manual's tap-tempo, temporary tempo-nudge, and free queued-tempo editing gestures remain outside the current build; stored bank tempos can still be recalled immediately, timed independently, or armed with a pattern cue.

The advanced engine and fast command-driven workflow take inspiration from M8 without cloning its interface or formats. This remains a focused five-track, per-step-patch instrument; the new Basic macros, motion routing, long envelopes, stereo unison, polymeters, algorithmic echo, snapshots, and timed pattern transitions are deliberately shaped for evolving ambient composition. Sampler/wavetable engines, song/phrase architecture, MIDI workstation features, and M8 file compatibility are not part of this release.
