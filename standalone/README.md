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
  factory (`vst3.cpp`). The custom VSTGUI editor is dropped; the host draws a
  generic UI from the parameters.
- `CMakeLists.txt` — fetches nothing; points at a local VST3 SDK checkout.

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
build\VST3\Release\VoiceFX.vst3
```

Optional — also build the Steinberg validator to test it:
```bat
cmake -S . -B build -DVOICEFX_BUILD_HOSTING_TOOLS=ON
cmake --build build --config Release --target validator
build\bin\Release\validator.exe build\VST3\Release\VoiceFX.vst3
```

## Install / run
Copy the `VoiceFX.vst3` bundle into a VST3 folder your host scans, e.g. your
user folder `%LOCALAPPDATA%\Programs\Common\VST3\` or the system
`C:\Program Files\Common Files\VST3\`, then rescan in Element.

> The install location for the paid original is
> `C:\Program Files\Common Files\VST3\TonPlugIns\VoiceFX.vst3`. Install this
> standalone build **next to it under a different name/folder** so you don't
> overwrite your licensed copy.

## Notes / limitations
- Runs on the default GPU (no multi-GPU selection).
- No custom editor UI yet (generic parameter view).
- Studio Voice / Speaker Focus modes need the NVIDIA AFX **2.x** models, which are
  not part of SDK 1.6.1.2; selecting them will fail to create the effect until a
  2.x runtime + models are installed.
