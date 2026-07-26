# Level (intensity) regression test

Drives the plugin's own `nvidia::afx::effect` class the way the VST3 processor does
when the **Level** slider moves, and checks that the audio actually changes.

## Why this exists

The Level slider silently did nothing for the entire life of the standalone build.
The failure is invisible from the calling side:

```
NvAFX_SetFloat(handle, "intensity_ratio", 0.0f)   -> SUCCESS
NvAFX_GetFloat(handle, "intensity_ratio", &v)     -> SUCCESS, v == 0.0
... and the audio is bit-identical to intensity 1.0
```

The SDK only reads `intensity_ratio` while `NvAFX_Load` builds the effect. The old
code set it *after* `NvAFX_Load`, on every path, so the effect always ran at
whatever the SDK defaults to.

## What was measured (RTX 5080, AFX SDK 1.6.1.2)

Test signal: 220 Hz tone + broadband noise, 48000 samples. Input energy ~1320.

| what | result |
|---|---|
| intensity set **before** `NvAFX_Load` | works: 1.0 -> 0.0964, 0.5 -> 309.75, 0.0 -> 1227.97 |
| intensity set **after** `NvAFX_Load` | ignored (audio identical to load-time value) |
| second `NvAFX_Load` on the same handle | `MODEL_LOAD_FAILED`, 0 ms, nothing applied |
| `NvAFX_Reset` to re-arm the effect | **breaks it** — energy jumps 0.0964 -> 800.61 with *no* intensity change at all |
| full rebuild (destroy + create + set + load) | works, ~84 ms |
| `intensity_ratio` on chained Super Resolution | ignored outright — handles built at 1.0 / 0.5 / 0.0 give byte-identical audio, and `NvAFX_GetFloat` cannot read it back |

Two consequences, both implemented in `standalone/src/nvidia-afx-effect.cpp`:

- The settings are applied **before** `NvAFX_Load`, and a change means rebuilding
  the effect — deferred until the value has held still for 200 ms, so dragging the
  slider doesn't rebuild once per mouse-move.
- `NvAFX_Reset` is not used anywhere (the old `effect::clear()` that relied on it
  is gone), and Level is skipped entirely for Echo Cancel and Super Resolution.

The measured dry-path delay is **3360 samples (70 ms, correlation 1.0000)** for all
three 48 kHz effects, and `intensity_ratio` is exactly a linear dry/wet blend
(best-fit dry amount at 0.5 is 0.5000, residual 0.00%). That is what an in-plugin
blend would need if the 85 ms rebuild ever becomes a problem — but it cannot cover
Super Resolution, whose dry path is resampled 16k -> 48k inside the SDK.

## Build & run

From a "x64 Native Tools Command Prompt for VS 2022", in the repository root:

```
cl /EHsc /std:c++17 /MT /DNOMINMAX /DQUIET ^
   /I standalone\src /I standalone\compat ^
   /I third-party\nvidia-maxine-afx-sdk\nvafx\include ^
   tools\intensity-test\intensity_test.cpp ^
   standalone\src\nvidia-afx-effect.cpp standalone\src\nvidia-afx.cpp ^
   standalone\src\lib.cpp /Fe:intensity_test.exe

intensity_test.exe
```

Needs a GPU and the NVIDIA Audio Effects SDK installed; it loads the real
`NVAudioEffects.dll` and runs the real models. Set `VOICEFX_LOG` to keep the
plugin's log out of `C:\VoiceFX\voicefx-kdver.log`.

Exit code 0 means everything passed. Expected output:

```
[Noise / denoiser 48k]
    Level 100% (initial)               energy = 0.0964
    Level 0% (settled)                 energy = 1227.9678
    Level 100% again (settled)         energy = 0.0964
    Level 50% (settled)                energy = 309.7497
  [PASS] Level 100% denoises
  [PASS] Level 0% lets the noise through
  [PASS] Level 100% again denoises just like before
  [PASS] Level 50% lands between the two

[Dragging the slider]
    40 changes + audio took 618 ms (a rebuild each would be >3000 ms)
  [PASS] dragging does not rebuild on every step
    after letting go at Level 22%      energy = 748.9766
  [PASS] letting go applies the final value

[Both + Super Resolution / chained]
    rates: 16000 Hz in / 48000 Hz out, blocks 160 / 480
    100 blocks: 42.3 ms normally, 44.9 ms right after a Level change
  [PASS] Super Resolution does not rebuild for Level

[Echo Cancel / AEC]
    20 AEC blocks after a Level change took 7.2 ms
  [PASS] AEC does not rebuild for a Level change

=== ALL CHECKS PASSED ===
```
