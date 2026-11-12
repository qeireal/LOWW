# LOWW user guide

[Back to the README](../README.md)

Paths below are relative to the repository root.

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
- The editor scales from 1040×836 to 1680×1350, keeping its proportions; the minimum keeps all text at 11 px or larger. The standalone adds its audio-routing bar. Input/output peak meters and compression reduction are visible.

## Cabinet slots

A and B each have an IR loader, stock response, enable, level, pan and polarity inversion. B starts disabled. Both affect mids/highs only. Panning is stereo balance with unity centre; mono output ignores pan. Two equal IRs at equal levels and opposite polarity cancel the upper-band cabinet signal.

WAV/AIFF IRs may have one or two channels, run at 8–192 kHz, and last up to two seconds. Both slots embed imported samples in saved state for portable recall. Factory tone selection preserves the chosen IRs **and all cabinet controls**, including enable states. Exported factory files use the original synthetic stock response.

The stock response is an original short filtered impulse with reflections, not a measured cabinet. No measured microphone bank, mic-position interpolation, or copied commercial cabinet assets are included. See the research notes for licensed-data leads and the requirements for adding real virtual microphones.

## Presets

The bank contains **11 original tones**:

- Original mode: Foundation, Clean weight, Pick attack, Split fury.
- Parallel rig studies: Deep Edge, Progressive Grind, Mid Focus, Dual Amp Growl, Modern Articulation, Clean Funk, Midrange Roar.

The new recipes draw on documented rig approaches associated with Geddy Lee, Chris Squire, Justin Chancellor, Tim Commerford, Nolly Getgood, Flea and Lemmy. They are original, unendorsed starting points; they do not reproduce an artist's recorded sound. The UI identifies the inspiration. Numerical LOWW settings are inferred, not transcribed artist settings. See [artist-rig-research.md](artist-rig-research.md) for sources, uncertainties, complete recipes and cabinet licensing.

The selected name remains visible; `*` indicates an edit. Previous/next cycle through factory and user presets. **Save** updates the opened user file; if the host has since recalled a different plugin state, **Save** asks for a new file instead. **Save as...** writes a new file; **Open** imports a `.loww` or legacy `.trifold` file. The **Folder** menu refreshes the library, opens its directory, resets the tone to Foundation, or moves the current user file to Trash. Factory entries cannot be deleted through this menu.

The user library is `~/Documents/LOWW Presets/`. Subfolders appear as relative paths in the browser. Standalone does not automatically reopen the last preset. MIDI mappings belong to host state rather than portable tone files. Imported IR samples embedded in a preset may carry redistribution restrictions; the bundled bank contains no third-party IR audio.

