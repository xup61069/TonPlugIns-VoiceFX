# VoiceFX — standalone VST3 build

This folder builds VoiceFX as a **self-contained VST3**, without the proprietary
TonPlugIns build framework. It reuses the effect / resampling / VST3 logic from the
main `source/` tree (including the Super Resolution work) and supplies small
compatibility shims for the few framework pieces that aren't public.

It was built and **validated on the target machine**: Windows 11, RTX 5080,
NVIDIA Audio Effects SDK 1.6.1.2, MSVC 2022, VST3 SDK 3.8.0. The Steinberg
`validator` reports **0 failures**.

## What it contains
- `compat/` — shims: `core.hpp` (logging), `ringbuffer.hpp` (float FIFO),
  `warning-disable/enable.hpp`.
- `src/` — the reused sources plus a standalone NVIDIA loader (`nvidia-afx.*`,
  default GPU, no custom CUDA/D3D context), CUDA stubs, `lib.cpp`, and the VST3
  factory (`vst3.cpp`).
- `resource/voicefx.uidesc` — a small flat-gray VSTGUI editor (see **UI** below).
- `CMakeLists.txt` — fetches nothing; points at a local VST3 SDK checkout.

## Effects (Mode)
- **Noise** — denoiser.
- **Reverb** — dereverb (removes room reverberation).
- **Both** — denoiser + dereverb.
- **Echo Cancel** — Acoustic Echo Cancellation (AEC). See **Echo Cancel (AEC)** below.

**Super Resolution** (checkbox) rebuilds high-frequency detail on top of the
Noise / Reverb / Both modes. It is ignored for Echo Cancel.

## Level (intensity)
**Level** sets how much of the cleaned signal you hear: 100% is fully processed,
0% is the untouched input, and the values in between are a straight blend of the
two.

It is a **stepped** control with 21 positions — 0, 5, 10 … 100% — so the slider
snaps and the read-out is always a whole multiple of 5. The mouse wheel moves one
step at a time.

The NVIDIA SDK only reads this value while it builds the effect — setting it on a
running effect is accepted but changes nothing. So the plugin rebuilds the effect
when you change Level, which takes about 85 ms. To keep that off every mouse-move,
it waits until the value has held still for **0.2 seconds**: drag freely, and the
new setting lands shortly after you let go, with one brief interruption.

Level only does anything for **Noise / Reverb / Both with Super Resolution off**:

- **Echo Cancel** has no intensity control at all.
- With **Super Resolution** on, the SDK ignores the value outright (verified on
  1.6.1.2: three effects built at 100% / 50% / 0% produce byte-identical audio,
  and the parameter cannot even be read back).

In those two cases the slider still moves, but the sound does not change, and the
plugin skips the pointless rebuild.

> Studio Voice and Speaker Focus were removed. They require the NVIDIA AFX **2.x**
> models, which are not available for Windows (the 1.6.1.2 redistributable ships
> no such models), so on this machine they never worked.

## Echo Cancel (AEC)
AEC removes the far-end / loudspeaker sound (the "echo") from the microphone. It
needs a **reference** signal — a copy of what is being played back — alongside the
mic. This build feeds that reference through the **right input channel**:

- **Left input channel = microphone**
- **Right input channel = reference** (the far-end / system playback / the sound
  coming out of your speakers)

The plugin runs AEC on that pair and outputs the single cleaned voice on **both**
output channels. So in your host, route the mic to the left and the playback you
want cancelled to the right of the same stereo input, then pick **Echo Cancel**.
(The other modes ignore the right channel and just clean each channel on its own.)

Internally AEC uses one NVIDIA effect handle that takes two input channels and
produces one; the classic modes use one handle per channel. Everything runs at
48 kHz internally and is resampled to/from the host rate (e.g. 44.1 kHz) as usual.

## UI
The plugin ships a compact gray editor built with VSTGUI, described entirely in
`resource/voicefx.uidesc` (no bitmaps — every control draws itself): a **Mode**
dropdown, a **Level** (intensity) slider with a numeric read-out, and a
**Super Res** checkbox.

VST3Editor binds each control to its parameter by matching the `control-tag`
values in the `.uidesc` to the plugin's FOURCC parameter IDs, so there is no
hand-written binding code — `createView()` just returns the editor. The `.uidesc`
is copied into the bundle at `Contents/Resources/voicefx.uidesc`, where VSTGUI
finds it at runtime.

Two build details make this work: VSTGUI's win32 bundle support needs the SDK's
`dllmain.cpp` (for the module handle used to locate the resource), and the
`.uidesc` is copied with our own post-build command because the SDK's resource
helper mishandles the space in the package name.

## Prerequisites
1. **Visual Studio 2022** (or Build Tools) with the C++ workload.
2. **NVIDIA Audio Effects SDK** installed (provides `NVAudioEffects.dll` + models).
   Default path `C:\Program Files\NVIDIA Corporation\NVIDIA Audio Effects SDK`;
   override at runtime with the `NVAFX_SDK` environment variable.
3. **Steinberg VST3 SDK** checked out locally, e.g.:
   ```
   git clone --recursive https://github.com/steinbergmedia/vst3sdk.git C:\vst3sdk
   ```

## Build
From this `standalone/` folder:
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DVST3_SDK_DIR=C:/vst3sdk
cmake --build build --config Release --target VoiceFX
```
The plugin is produced at:
```
build\VST3\Release\VoiceFX KDver.vst3
```

Optional — also build the Steinberg validator to test it:
```bat
cmake -S . -B build -DVOICEFX_BUILD_HOSTING_TOOLS=ON
cmake --build build --config Release --target validator
build\bin\Release\validator.exe "build\VST3\Release\VoiceFX KDver.vst3"
```

## Install / run
Copy the `VoiceFX KDver.vst3` bundle into a VST3 folder your host scans, e.g. your
user folder `%LOCALAPPDATA%\Programs\Common\VST3\` or the system
`C:\Program Files\Common Files\VST3\`, then rescan in Element.

> The install location for the paid original is
> `C:\Program Files\Common Files\VST3\TonPlugIns\VoiceFX.vst3`. Install this
> standalone build **next to it under a different name/folder** so you don't
> overwrite your licensed copy.

## Notes / limitations
- Runs on the default GPU (no multi-GPU selection).
- Ships a compact flat-gray VSTGUI editor (Mode / Level / Super Res); see **UI** above.
- **Echo Cancel (AEC)** expects the reference signal on the right input channel
  (left = mic); see **Echo Cancel (AEC)** above. With a mono input there is no
  reference channel, so AEC produces silence.
- **Super Res** applies to the Noise / Reverb / Both modes; AEC ignores it.
- The **Level** slider needs Super Resolution off, and applies ~0.2 s after you
  stop moving it; see **Level (intensity)** above.
