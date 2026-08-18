# FMS Linux

FMS Linux is a native desktop interpretation of the five-track FM step sequencer described in the supplied FMS manual. It runs directly on Linux—no GBA ROM or emulator is involved—and keeps the original instrument's central idea: every step owns its sound.

The application combines four two-operator FM tracks with one PSG-style LFSR noise track, a polymetric 16-step sequencer, per-step synthesis, pattern memory, snapshots, algorithmic echo, transpose lanes, and per-track step modulators.

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
- `Enter` — place or remove a trigger
- `Delete` / `Backspace` — clear the selected step
- `[` / `]` — select the parameter to edit
- `-` / `=` — change its value; hold `Shift` for coarse changes
- `1`–`5` — jump to a track
- `,` / `.` — lower or raise BPM; hold `Shift` for 10 BPM steps
- `Alt+Left/Right` — change the selected track's rate
- `Alt+Up/Down` — change the selected track's shuffle
- `L` / `Shift+L` — increase or decrease track length
- `D` / `Shift+D` — cycle track direction
- `Tab` / `Shift+Tab` — move between Grid, Echo, Transpose, Mod, Scale, and Data
- `C` / `V` — copy or paste the selected step
- `T` — toggle a trigless step
- `R` — randomize the selected track
- `M` / `Shift+M` — mute or solo the selected track
- `S` — capture or recall the performance snapshot
- `Ctrl+S` — save immediately
- `F1` or `?` — open the in-app control reference
- `F2` / `F3` — change theme or accent color
- `K` in Data — lock or unlock the selected bank

The interface is mouse-aware: click tracks, steps, view tabs, and parameters; use the wheel to adjust the selected field or the track-header value beneath it. The specialized views expose the echo engine, transpose sequence, step modulator, scale mask, and 8 × 16 pattern library. `Shift+click` stores a pattern; selecting a pattern while playing cues it at that track's next available, not-yet-scheduled nominal loop boundary without resetting the other tracks. A negative microtime offset on the incoming pattern's first step is clipped to that boundary so the new pattern never sounds before the old one finishes. Current shortcut hints are always shown along the bottom edge.

## Instrument behavior

Each FM trigger stores its own amplitude attack/hold/release, fractional modulator ratio, modulation depth and feedback, modulation envelope, pitch sweep, routing mode, note, level, pan, portamento, trigger condition, microtiming, chord allocation, echo send, and transpose enable state. Trigless steps update a sounding voice without resetting its phase or envelopes.

Track clocks have independent length, rate, traversal direction, and shuffle. The audio callback advances those clocks at sample resolution. Chord tones share the four-voice FM pool; the noise track uses a configurable 15-bit or narrow 7-bit LFSR. Stereo panning is deliberately discrete: left, center, or right.

The included starter pattern is audible immediately after pressing `Space`. It can be cleared, changed, or replaced with the track randomizer.

## Saving

State is saved automatically on exit and with `Ctrl+S`. The default location follows the XDG base-directory convention:

```text
$XDG_DATA_HOME/fms-linux/state.bin
```

When `XDG_DATA_HOME` is unset, FMS uses `~/.local/share/fms-linux/state.bin`. Saves are versioned, checksummed, and replaced atomically so an interrupted write does not damage the previous session. Use `--save-path FILE` to keep a separate set.

## Verification and command-line options

```sh
make test          # model, save, audio, UI-event, and render tests
make screenshot    # headless UI render to fms-ui.bmp
make audio-smoke   # exercise the real-time engine with SDL's dummy driver
./fms-linux --help
```

Useful launch flags include `--play`, `--no-audio`, `--run-for SEC`, `--screenshot FILE`, and `--save-path FILE`.

## Project layout

- `src/model.*` — instrument state, defaults, scale/rate helpers, randomization
- `src/audio.*` — SDL audio device, voice DSP, event queues, track clocks
- `src/ui.*` — SDL renderer, embedded pixel type, editing and alternate views
- `src/persistence.*` — versioned on-disk encoding and atomic writes
- `packaging/` — freedesktop launcher and icon

FMS Linux uses no web view, game-console emulator, plug-in host, or bundled binary assets. The interface, sequencer, synthesis, and persistence layers are native C++20 code.

## Current scope

This release implements the manual's core instrument and headline sequencer feature set with desktop-native controls. Console-specific flash/link workflows and configurable GBA button mappings do not apply. External clock sync, range/all-track editing and rotation, incremental parameter randomization, separate load-in-place/load-reset commands, Data-view slot clear/random commands, the sound-palette overlay, bank renaming and bank-level BPM/scale recall, and the manual's tap/nudge/queued-tempo gestures are not exposed in the current Linux UI. The internal clock and all live synthesis, per-step sound editing, polymeter, modulation, snapshot, echo, scale, track randomization, and per-track pattern storage/cueing operate natively.
