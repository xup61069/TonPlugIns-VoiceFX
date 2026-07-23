# NVIDIA Audio Effects SDK feature update

This branch wires additional NVIDIA Maxine / Audio Effects (AFX) SDK features
into VoiceFX. It was prepared **without a build/run environment**, so the code is
written to match the existing style and the AFX headers/docs, but it has **not**
been compiled or tested. Please build and test it inside the TonPlugIns build
framework on a machine with a supported NVIDIA GPU before shipping.

## What was added

### 1. Safe internals (no new UI)
- `nvidia::afx::effect::clear()` now calls the official **`NvAFX_Reset`** to reset
  each effect's state, instead of flooding it with silence. Falls back to the old
  behaviour if `NvAFX_Reset` is unavailable.
- Fixed a latent bug in `nvidia::afx::afx::model_path()` where
  `dereverb_denoiser` pointed at the denoiser model.
- Fixed two latent bugs in `processor::step_resample_out()`: it used the *input*
  resampler and a shadowed, do-nothing lock. It now uses the output resampler and
  locks the real `_out_lock`.
- Fixed `process()` reading only the first changed parameter per block (the loop
  bound was a boolean).

### 2. Super Resolution (new "Super Resolution" on/off parameter)
- New `enable_superres()` on the effect. When on, the plugin selects the chained
  effect that cleans up at 16 kHz and rebuilds a 48 kHz signal, created with
  **`NvAFX_CreateChainedEffect`** and fed two model files via
  **`NvAFX_SetStringList`**.
- The processor now resamples the **input and output sides independently**
  (`_resample_in` / `_resample_out`), because Super Resolution has a 16 kHz input
  and a 48 kHz output. When input and output rates are equal (all classic modes),
  the behaviour is identical to before.
- The inner effect loop and `step_process()` now handle **different input and
  output block sizes** (output can be larger than input).

### 3. Studio Voice & Speaker Focus (two new "Mode" entries)
- New `enable_studio_voice()` / `enable_speaker_focus()` on the effect, exposed as
  extra entries in the existing "Mode" list.
- These effects only exist in the **AFX 2.x** runtime. The selector strings are
  declared with `#ifndef` guards so they compile today and defer to the official
  macros once you drop in a newer `nvAudioEffects.h`.

### State / preset compatibility
- New fields are appended to the end of the saved state. Older presets (three
  fields) still load; the new fields default to off. See `get/setState` and the
  controller's `setComponentState`.

## What you MUST verify against your installed redistributable

The exact model file names differ between SDK versions (some builds add `_v2` or
a `sm_XX/` sub-folder). Search the `models` folder of your NVIDIA redistributable
and confirm/adjust the names marked `// VERIFY` in
`source/nvidia-afx-effect.cpp` (`load()`):

| Feature | Selector | Model file(s) used | Confidence |
|---|---|---|---|
| Denoise | `denoiser` | `denoiser_48k.trtpkg` | existing |
| Dereverb | `dereverb` | `dereverb_48k.trtpkg` | existing |
| Both | `dereverb_denoiser` | `dereverb_denoiser_48k.trtpkg` | existing |
| Denoise + SuperRes | `denoiser16k_superres16kto48k` | `denoiser_16k.trtpkg`, `superres_16kto48k.trtpkg` | needs check |
| Dereverb + SuperRes | `dereverb16k_superres16kto48k` | `dereverb_16k.trtpkg`, `superres_16kto48k.trtpkg` | needs check |
| Both + SuperRes | `dereverb_denoiser16k_superres16kto48k` | `dereverb_denoiser_16k.trtpkg`, `superres_16kto48k.trtpkg` | needs check |
| Studio Voice | `studio_voice_high_quality` | `studio_voice_48k.trtpkg` | needs SDK 2.x |
| Speaker Focus | `speaker_focus` | `speaker_focus_48k.trtpkg` | needs SDK 2.x |

Effect selector strings come from NVIDIA's AFX 2.1.0 "Type Definitions"
reference. `speaker_focus` is a best guess for the selector string — confirm it in
your header.

## Known limitations / not done
- **Acoustic Echo Cancellation (AEC)** is intentionally not wired in: it needs a
  separate far-end/reference audio stream, which a single-input post-processing
  VST cannot provide.
- **Voice Font** is not wired in (needs a reference speaker model).
- `effect::delay()` still reports the classic 48 kHz latency (~82 ms). Latency
  reporting for the Super Resolution chained path may be slightly off; audio is
  still correct.
- The reported sample rates for Studio Voice / Speaker Focus assume 48 kHz in and
  out. The processor adapts to whatever the loaded effect reports, but the
  `SetU32(INPUT/OUTPUT_SAMPLE_RATE)` values may need adjusting for those effects.
