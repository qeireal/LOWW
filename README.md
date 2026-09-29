<img src="docs/logo.png" width="96" alt="LOWW logo">

# LOWW 0.2.1

A native C++20 / JUCE bass processor for macOS, with three-band processing, dual cabinet IRs, an interactive frequency graph, transpose, tuner, MIDI CC learn, and original factory tones.

Local Apple Silicon builds are in `dist/macOS/`: `LOWW.app`, `LOWW.vst3`, and `LOWW.component`. They are ad-hoc signed, not notarized, published, or installed in system plugin folders. `dist/archive/0.1.3/` retains the previous packages. A running older instance must be restarted to load the new executable; save its tone first.

## Start playing

1. Open `dist/macOS/LOWW.app`.
2. Open **Audio settings...** and choose your instrument interface, physical input, output, and buffer size.
3. Leave **Mono** selected for one instrument input. **Stereo** preserves two physical inputs; the separate **Mono lows** tone option centres only the low band.
4. Enable **Monitor input**. Use your interface and headphones to avoid a microphone/speaker feedback loop.

Audio opens disconnected and device selection remains explicit. It does not reopen the last device or preset automatically. DAW plugins use the host's audio layout.

## Processing modes

**Parallel voicing** (new-instance default) uses independent low-pass (70–400 Hz), fixed 400 Hz band-pass (Q 0.7071), and high-pass (100–2000 Hz) branches. The low compressor has a 3 ms attack, 600 ms release and fixed 4:1 ratio; its amount adjusts threshold/makeup with a short fade to unity at zero. The outer EQ bands are shelves at 100 Hz and 5 kHz; the four middle bell bands are 250, 500, 1000 and 1500 Hz. All six gains span ±12 dB. Mono lows is off by default and enabled by the new rig-study presets.

With Parallel voicing **off**, the original fourth-order Linkwitz–Riley crossover topology, 15 ms / 120 ms compressor with variable ratio, and six bell EQ bands are retained. Its low/high ranges remain 60–400 / 500–3000 Hz. Older schema-1 `.loww` and `.trifold` presets migrate into this original mode. The original four factory tones also retain it. Existing parameter IDs and their ranges are retained; new independent filter parameters are used in Parallel mode.

Both modes use original asymmetric mid and symmetric high waveshaping with 4× IIR oversampling. These are independent implementations. The documented Parallax controls informed the new mode, but its proprietary filters, saturation, compressor transfer curve and cabinet sound have not been measured or matched. Parallel mode's summed response is intentionally not the flat crossover reconstruction tested for the original mode.

Input trim → gate/transpose → 4× oversampling → three bands → low compression and mid/high saturation → two parallel cabinet slots on mids/highs → six-band EQ → wet/dry blend → output trim → latency-aligned bypass.

The wet/dry branch shares the selected band topology and transpose. Whole-processor bypass returns the original input with reported latency compensation. Band processing switches retain clean signal rather than muting the band. Gains, drive, compression amount, filter frequencies, cabinet mix controls, EQ coefficients and bypasses are smoothed. Switching the topology is a tone change, not a transparent morph.

## Controls and tools

- Cyan and slate curves show each band filter and its level; the white curve shows their combined response. They follow both processing modes and remain visible with Spectrum off. These guides exclude compression, distortion, cabinet and EQ.
- Drag **L/H** horizontally to change their filter frequency and vertically to change band level. Drag **M** vertically for mid level. **Spectrum** toggles the live output trace; handles remain available.
- Double-click a dial to reset it; click its value to type. Right-click a dial for **Learn next MIDI CC** or **Clear MIDI mapping**. One incoming CC completes learning. Mappings are stored in host state; loading a tone preset keeps the current mappings. Enable your MIDI input in standalone Audio settings, or route MIDI in your host. VST3 and standalone accept MIDI; AU routing depends on host support for MIDI to an audio-effect unit. Physical MIDI hardware was not tested.
- **Transpose** spans −12 to +12 semitones using Signalsmith Stretch. At zero, the original low-latency audio path is retained. Nonzero settings add the library's analysis/synthesis latency, reported to the host and displayed beside the preset description while audio is running. This is an offline-verified pitch shifter, not a claim of Parallax's low-latency performance. Crossing zero changes latency; change tuning between takes rather than expecting a seamless pitch pedal.
- **Live tuner** shows note, cents and frequency from the unprocessed input, calibrated to A=440 Hz. Play one note at a time. Its computation runs on the editor thread. It has no mute function or alternate reference calibration yet.
- Standalone **Click**, **Tap**, BPM (30–300), and 1–12 beats per bar provide a quarter-note metronome with a first-beat accent. Metronome settings stay outside tone presets. These are not DAW-synchronised subdivisions or selectable accent samples.
- The editor scales from 840×675 to 1680×1350. The standalone adds its audio-routing bar. Input/output peak meters and compression reduction are visible.

## Cabinet slots

A and B each have an IR loader, stock response, enable, level, pan and polarity inversion. B starts disabled. Both affect mids/highs only. Panning is stereo balance with unity centre; mono output ignores pan. Two equal IRs at equal levels and opposite polarity cancel the upper-band cabinet signal.

WAV/AIFF IRs may have one or two channels, run at 8–192 kHz, and last up to two seconds. Both slots embed imported samples in saved state for portable recall. Factory tone selection preserves the chosen IRs **and all cabinet controls**, including enable states. Exported factory files use the original synthetic stock response.

The stock response is an original short filtered impulse with reflections, not a measured cabinet. No measured microphone bank, mic-position interpolation, or copied commercial cabinet assets are included. See the research notes for licensed-data leads and the requirements for adding real virtual microphones.

## Presets

The bank contains **11 original tones**:

- Original mode: Foundation, Clean weight, Pick attack, Split fury.
- Parallel rig studies: Deep Edge, Progressive Grind, Mid Focus, Dual Amp Growl, Modern Articulation, Clean Funk, Midrange Roar.

The new recipes draw on documented rig approaches associated with Geddy Lee, Chris Squire, Justin Chancellor, Tim Commerford, Nolly Getgood, Flea and Lemmy. They are original, unendorsed starting points; they do not reproduce an artist's recorded sound. The UI identifies the inspiration. Numerical LOWW settings are inferred, not transcribed artist settings. See [artist-rig-research.md](docs/artist-rig-research.md) for sources, uncertainties, complete recipes and cabinet licensing.

The selected name remains visible; `*` indicates an edit. Previous/next cycle through factory and user presets. **Save** updates the opened user file; **Save as...** writes a new file; **Open** imports a `.loww` or legacy `.trifold` file. The **Folder** menu refreshes the library, opens its directory, resets the tone to Foundation, or moves the current user file to Trash. Factory entries cannot be deleted through this menu.

The user library is `~/Documents/LOWW Presets/`. Subfolders appear as relative paths in the browser. Standalone does not automatically reopen the last preset. MIDI mappings belong to host state rather than portable tone files. Imported IR samples embedded in a preset may carry redistribution restrictions; the bundled bank contains no third-party IR audio.

## Build and verification

Requires CMake 3.24+, Git and a C++20 platform toolchain. JUCE is pinned to 8.0.12 (`29396c22c93392d6738e021b83196283d6e4d850`), or provide `JUCE_SOURCE_DIR`. Signalsmith Stretch headers are vendored at `a670068d9aeb64913331d5cc29337b19a457a7df`; its CMake file fetches Signalsmith Linear 0.6.4. The UI embeds the Open Runde typeface (© 2023 Laurids Kern, SIL OFL 1.1; `Resources/Fonts/OFL.txt`). Dependency licenses remain applicable; see `third_party/signalsmith-stretch/` and the packaged license folder.

```sh
rtk cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
rtk cmake --build build --parallel 6
rtk ctest --test-dir build -C Release --output-on-failure
```

macOS builds standalone, VST3 and AU. The AU retains its existing audio-effect type and manufacturer/product codes. Windows builds and third-party DAW/hardware sessions remain unverified. Set `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build; this delivery is arm64 only.

`VALIDATION.txt` contains the final test run. Tests cover original crossover reconstruction, bypass latency, stereo monitoring, compression/distortion/EQ, both IR slots and invalid state, legacy migration, factory/file recall, MIDI learn/clear/host recall, transpose, tuner across four sample rates, metronome output, and actual VST3/AU bundle hosting. The AU test registers only inside its process and needs access to macOS Audio Component services. UI snapshots were reviewed at 1120×900 and 840×675.

Additional test utilities:

```sh
rtk build/LOWWTests --export-presets /absolute/output/folder
rtk build/LOWWTests --snapshot /absolute/output/editor.png 1120 900
rtk build/LOWWTests --demo /absolute/output/folder
rtk build/LOWWTests --logo /absolute/output/Icon.png 1024
```

`demo/` retains the earlier synthetic dry/processed bass example. It is not a real instrument or artist comparison. Formal audio-thread allocation/race instrumentation, loudness-matched real-DI listening, proprietary sound matching, measured microphone controls, AAX, installers and notarization remain outside this local release.

## Code map

- `Source/Engine.*`: band processing, oversampling, transpose, dual convolution, EQ and smoothing.
- `Source/Processor.*`: parameters, preset migration/files, IR mailboxes, MIDI, audio FIFOs and standalone click.
- `Source/Editor.*`: scalable native interface, spectrum, interactive handles, preset and cabinet controls.
- `Source/Tuner.h`: editor-side difference-function pitch detection.
- `Source/Standalone.cpp`, `Source/InstrumentPlayer.h`: explicit device setup, MIDI collection and mono/stereo monitoring.
- `Tests/AudioTests.cpp`, `Tests/MonitoringTests.h`: offline DSP and host/device regression checks.
