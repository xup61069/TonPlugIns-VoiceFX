# AFX smoke test

A tiny standalone program that validates the NVIDIA Audio Effects SDK calls this
branch adds, **without** needing the TonPlugIns build framework or the VST3 SDK.
It dynamically loads the installed `NVAudioEffects.dll` (like the plugin does) and
exercises `NvAFX_CreateChainedEffect`, `NvAFX_SetStringList`, the 16 kHz-in /
48 kHz-out Super Resolution path, and `NvAFX_Reset`.

## Build & run (Windows, x64 VS Build Tools)

From a "x64 Native Tools Command Prompt for VS 2022" (or after calling
`vcvars64.bat`), in the repository root:

```
cl /EHsc /std:c++17 /I third-party\nvidia-maxine-afx-sdk\nvafx\include ^
   tools\afx-smoketest\afx_smoketest.cpp /Fe:afx_smoketest.exe

afx_smoketest.exe
```

Pass a custom SDK directory as the first argument if it isn't installed in the
default location:

```
afx_smoketest.exe "C:\Program Files\NVIDIA Corporation\NVIDIA Audio Effects SDK"
```

## Verified result

Run on **NVIDIA GeForce RTX 5080**, driver 610.74, **AFX SDK 1.6.1.2**:

```
[TEST] Denoiser 48k (baseline)
    reported: in 48000 Hz / out 48000 Hz | in_blk 480 / out_blk 480 | in_ch 1 / out_ch 1
    processed 100 frames; total output energy = 204.8829 (AUDIO FLOWS OK)
    [PASS] Denoiser 48k (baseline)
[TEST] Denoiser16k + SuperRes 16k->48k (chained)
    reported: in 16000 Hz / out 48000 Hz | in_blk 160 / out_blk 480 | in_ch 1 / out_ch 1
    processed 100 frames; total output energy = 4.7924 (AUDIO FLOWS OK)
    [PASS] Denoiser16k + SuperRes 16k->48k (chained)
[TEST] Dereverb+Denoiser16k + SuperRes 16k->48k (chained)
    reported: in 16000 Hz / out 48000 Hz | in_blk 160 / out_blk 480 | in_ch 1 / out_ch 1
    processed 100 frames; total output energy = 0.0011 (AUDIO FLOWS OK)
    [PASS] Dereverb+Denoiser16k + SuperRes 16k->48k (chained)
=== RESULT: 3/3 tests passed ===
```

### What this proves for the plugin changes
- `NvAFX_CreateChainedEffect` + `NvAFX_SetStringList` (two model files) work, and
  the model file names picked in `nvidia-afx-effect.cpp` are correct.
- The Super Resolution chain reports **input 16 kHz / output 48 kHz** with
  **input block 160 / output block 480** (3x). This is the asymmetric case the
  branch adds handling for (separate in/out cursors in `effect::process`, output
  space reserved by output block size in `step_process`, and decoupled input/output
  resampling in the processor).
- Setting `input_sample_rate` / `output_sample_rate` on the chained effect is
  accepted (not immutable).
- `NvAFX_Reset` succeeds (Tier A).

### AEC (Acoustic Echo Cancellation) probe
The test also probes the `aec` effect, which is the basis for a second-input
feature. On SDK 1.6.1.2 it reports:

```
[AEC PROBE] model=aec_48k.trtpkg rate=48000
    AEC reports: in_ch=2 out_ch=1 in_blk=480 out_blk=480 in_sr=48000
    Run(num_input_channels=2) -> SUCCESS
```

So AEC takes **two input channels** (channel 0 = microphone, channel 1 =
reference / far-end) and produces **one** cleaned output channel, at 48 kHz with a
480-sample block. This is what a second "Reference" input bus on the plugin would
feed.

### Not covered here
- The VST3 glue (resampler, parameter handling, state) still needs the full
  TonPlugIns framework to build and test.
- **Studio Voice / Speaker Focus** need AFX SDK **2.x** models, which are not part
  of 1.6.1.2 (no `studio_voice_*` / `speaker_focus_*` in its `models` folder), so
  those paths were not run here.
