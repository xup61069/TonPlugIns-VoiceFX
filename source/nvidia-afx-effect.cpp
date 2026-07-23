// Copyright 2020 Michael Fabian 'Xaymar' Dirks <info@xaymar.com>
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

#include "nvidia-afx-effect.hpp"
#include "lib.hpp"

#include "warning-disable.hpp"
#include <nvAudioEffects.h>
#include "warning-enable.hpp"

// ---------------------------------------------------------------------------
// Forward-compatible effect selectors for the NVIDIA AFX SDK 2.x.
//
// The nvAudioEffects.h header vendored in this repository (2022) predates the
// Studio Voice and Speaker Focus effects, so their selector strings are not
// defined there. We declare them here, guarded by #ifndef, so that:
//   * this file keeps compiling against the currently bundled header, and
//   * once you drop in NVIDIA's newer header (which defines these macros), the
//     official values automatically win and these fall away.
//
// The string values were taken from NVIDIA's AFX 2.1.0 "Type Definitions"
// reference. The matching model files ship with the 2.x redistributable; the
// filenames below are best-effort and MUST be verified against the "models"
// folder of the redistributable you install (search for the *.trtpkg files).
// ---------------------------------------------------------------------------
#ifndef NVAFX_EFFECT_STUDIO_VOICE_HIGH_QUALITY
#define NVAFX_EFFECT_STUDIO_VOICE_HIGH_QUALITY "studio_voice_high_quality"
#endif
#ifndef NVAFX_EFFECT_SPEAKER_FOCUS
#define NVAFX_EFFECT_SPEAKER_FOCUS "speaker_focus" // VERIFY against your SDK header.
#endif

nvidia::afx::effect::effect() : _lock(), _model_path(), _model_path_str()
{
	D_LOG_LOUD("");
	_nvafx = ::nvidia::afx::afx::instance();

	// Set up initial state.
	channels(1);
#ifndef TONPLUGINS_DEMO
	enable_denoise(true);
	enable_dereverb(false);
	enable_superres(false);
	enable_studio_voice(false);
	enable_speaker_focus(false);
#endif

#ifndef TONPLUGINS_DEMO
	intensity(0.67);
	voice_activity_detection(false);
#endif

	load();
}

nvidia::afx::effect::~effect()
{
	D_LOG_LOUD("");
	_fx.clear();
	_nvafx.reset();
}

template<>
uint32_t nvidia::afx::effect::get(NvAFX_ParameterSelector key)
{
	uint32_t val;
	if (auto res = _nvafx->GetU32(_fx[0].get(), key, &val); res != NVAFX_STATUS_SUCCESS) {
		throw_log("%s(%s) failed: 0x%08" PRIX32 ".", __FUNCTION_SIG__, key, res);
	}
	return uint32_t(val);
}

template<>
bool nvidia::afx::effect::get(NvAFX_ParameterSelector key)
{
	return get<uint32_t>(key) > 0 ? true : false;
}

template<>
float nvidia::afx::effect::get(NvAFX_ParameterSelector key)
{
	float val;
	if (auto res = _nvafx->GetFloat(_fx[0].get(), key, &val); res != NVAFX_STATUS_SUCCESS) {
		throw_log("%s(%s) failed: 0x%08" PRIX32 ".", __FUNCTION_SIG__, key, res);
	}
	return float(val);
}

template<>
void nvidia::afx::effect::set(NvAFX_ParameterSelector key, uint32_t value)
{
	for (size_t ch = 0; ch < _fx_channels; ch++) {
		if (auto res = _nvafx->SetU32(_fx[ch].get(), key, value); res != NVAFX_STATUS_SUCCESS) {
			throw_log("%s(%s, %" PRIu32 ") failed: 0x%08" PRIX32 ".", __FUNCTION_SIG__, key, value, res);
		}
	}
}

template<>
void nvidia::afx::effect::set(NvAFX_ParameterSelector key, bool value)
{
	set<uint32_t>(key, value ? 1 : 0);
}

template<>
void nvidia::afx::effect::set(NvAFX_ParameterSelector key, float value)
{
	for (size_t ch = 0; ch < _fx_channels; ch++) {
		if (auto res = _nvafx->SetFloat(_fx[ch].get(), key, value); res != NVAFX_STATUS_SUCCESS) {
			throw_log("%s(%s, %f) failed: 0x%08" PRIX32 ".", __FUNCTION_SIG__, key, value, res);
		}
	}
}

template<>
void nvidia::afx::effect::set(NvAFX_ParameterSelector key, const char* value)
{
	for (size_t ch = 0; ch < _fx_channels; ch++) {
		if (auto res = _nvafx->SetString(_fx[ch].get(), key, value); res != NVAFX_STATUS_SUCCESS) {
			throw_log("%s(%s, '%s') failed: 0x%08" PRIX32 ".", __FUNCTION_SIG__, key, value, res);
		}
	}
}

void nvidia::afx::effect::set_model_paths(std::vector<std::string> const& paths)
{
	if (paths.empty()) {
		throw_log("No model file was selected for the effect.");
	}

	// A single model is the classic case (denoiser, dereverb, ...).
	if (paths.size() == 1) {
		set<const char*>(NVAFX_PARAM_MODEL_PATH, paths[0].c_str());
		return;
	}

	// Multiple models means a chained effect (e.g. denoiser -> super resolution),
	// which is only available on newer runtimes via NvAFX_SetStringList.
	if (!_nvafx->SetStringList) {
		throw_log("Chained effects (Super Resolution / Speaker Focus) need a newer NVIDIA runtime. Please update NVIDIA Broadcast or the Audio Effects redistributable.");
	}

	std::vector<const char*> ptrs;
	ptrs.reserve(paths.size());
	for (auto const& p : paths) {
		ptrs.push_back(p.c_str());
	}

	for (size_t ch = 0; ch < _fx_channels; ch++) {
		if (auto res = _nvafx->SetStringList(_fx[ch].get(), NVAFX_PARAM_MODEL_PATH, ptrs.data(), static_cast<unsigned int>(ptrs.size())); res != NVAFX_STATUS_SUCCESS) {
			throw_log("Setting %zu chained model paths failed: 0x%08" PRIX32 ".", ptrs.size(), res);
		}
	}
}

uint32_t nvidia::afx::effect::input_samplerate()
{
	std::unique_lock<decltype(_lock)> lock(_lock);
	return get<uint32_t>(NVAFX_PARAM_INPUT_SAMPLE_RATE);
}

uint32_t nvidia::afx::effect::output_samplerate()
{
	std::unique_lock<decltype(_lock)> lock(_lock);
	return get<uint32_t>(NVAFX_PARAM_OUTPUT_SAMPLE_RATE);
}

uint32_t nvidia::afx::effect::input_blocksize()
{
	std::unique_lock<decltype(_lock)> lock(_lock);
	return get<uint32_t>(NVAFX_PARAM_NUM_INPUT_SAMPLES_PER_FRAME);
}

uint32_t nvidia::afx::effect::output_blocksize()
{
	std::unique_lock<decltype(_lock)> lock(_lock);
	return get<uint32_t>(NVAFX_PARAM_NUM_OUTPUT_SAMPLES_PER_FRAME);
}

uint32_t nvidia::afx::effect::input_channels()
{
	std::unique_lock<decltype(_lock)> lock(_lock);
	return get<uint32_t>(NVAFX_PARAM_NUM_INPUT_CHANNELS);
}

uint32_t nvidia::afx::effect::output_channels()
{
	std::unique_lock<decltype(_lock)> lock(_lock);
	return get<uint32_t>(NVAFX_PARAM_NUM_OUTPUT_CHANNELS);
}

size_t nvidia::afx::effect::delay()
{
	// The initial documentation for the denoise effect stated a latency of 72ms, which in reality ended up being 82ms.
	// The new readme.txt in the model directory lists multiple window sizes, which appear to match observed delay.

	// Measured a delay of 4896 samples at 48kHz, which includes a 960 sample local delay. Real delay is 3936 samples.
	// With a "framesize" of 42.'6ms, it would be (2048 + 1888) samples. Seems like it is 82ms.
	return static_cast<size_t>(82 * 480 / 10);
}

uint8_t nvidia::afx::effect::channels()
{
	return _fx_channels;
}

void nvidia::afx::effect::channels(uint8_t v)
{
	D_LOG_LOUD("Adjusting channels to %" PRIu8 ".", v);
	if (v == 0) {
		throw_log("Can't set channel count to 0, illegal operation.");
	}

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_channels) {
		_fx_channels = v;
		_fx_dirty    = true;
	}
}

#ifndef TONPLUGINS_DEMO
bool nvidia::afx::effect::denoise_enabled()
{
	return _fx_denoise;
}

void nvidia::afx::effect::enable_denoise(bool v)
{
	D_LOG_LOUD("Setting denoising to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_denoise) {
		_fx_denoise = v;
		_fx_dirty   = true;
		_fx_model   = true;
	}
}

bool nvidia::afx::effect::dereverb_enabled()
{
	return _fx_dereverb;
}

void nvidia::afx::effect::enable_dereverb(bool v)
{
	D_LOG_LOUD("Setting dereverb to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_dereverb) {
		_fx_dereverb = v;
		_fx_dirty    = true;
		_fx_model    = true;
	}
}

bool nvidia::afx::effect::superres_enabled()
{
	return _fx_superres;
}

void nvidia::afx::effect::enable_superres(bool v)
{
	D_LOG_LOUD("Setting super resolution to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_superres) {
		_fx_superres = v;
		_fx_dirty    = true;
		_fx_model    = true;
	}
}

bool nvidia::afx::effect::studio_voice_enabled()
{
	return _fx_studiovoice;
}

void nvidia::afx::effect::enable_studio_voice(bool v)
{
	D_LOG_LOUD("Setting studio voice to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_studiovoice) {
		_fx_studiovoice = v;
		_fx_dirty       = true;
		_fx_model       = true;
	}
}

bool nvidia::afx::effect::speaker_focus_enabled()
{
	return _fx_speakerfocus;
}

void nvidia::afx::effect::enable_speaker_focus(bool v)
{
	D_LOG_LOUD("Setting speaker focus to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_speakerfocus) {
		_fx_speakerfocus = v;
		_fx_dirty        = true;
		_fx_model        = true;
	}
}

float nvidia::afx::effect::intensity()
{
	return _cfg_intensity;
}

void nvidia::afx::effect::intensity(float v)
{
	D_LOG_LOUD("Setting intensity to %f.", v);

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _cfg_intensity) {
		_cfg_intensity = v;
		_cfg_dirty     = true;
	}
}

bool nvidia::afx::effect::voice_activity_detection()
{
	return _cfg_vad;
}

void nvidia::afx::effect::voice_activity_detection(bool v)
{
	D_LOG_LOUD("Setting voice activity detection to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _cfg_vad) {
		_cfg_vad   = v;
		_cfg_dirty = true;
	}
}

#endif

void nvidia::afx::effect::load()
{
	D_LOG_LOUD("");
	char message_buffer[1024] = {0};

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (_fx_dirty) {
		D_LOG("Effect is dirty and must be reloaded.");

		std::shared_ptr<::nvidia::cuda::context_stack> cstk;
		if (auto ctx = _nvafx->cuda_context(); ctx) {
			cstk = ctx->enter();
		}

#ifdef WIN32
		// Fix the search paths if some other plugin messed with them.
		_nvafx->windows_fix_dll_search_paths();
#endif

		// Decide which effect and model file(s) to load.
		//
		// Classic modes (denoise / dereverb / both) run at 48kHz. Super Resolution
		// cleans up at 16kHz and then rebuilds a 48kHz signal, so it is a "chained"
		// effect with two model files. Studio Voice and Speaker Focus are stand-alone
		// 48kHz effects from the NVIDIA AFX 2.x SDK.
		NvAFX_EffectSelector     effect   = NVAFX_EFFECT_DENOISER;
		bool                     chained  = false;
		uint32_t                 in_rate  = 48000;
		uint32_t                 out_rate = 48000;
		std::vector<std::string> effect_models{"denoiser_48k.trtpkg"};
#ifndef TONPLUGINS_DEMO
		if (_fx_studiovoice) {
			effect        = NVAFX_EFFECT_STUDIO_VOICE_HIGH_QUALITY;
			effect_models = {"studio_voice_48k.trtpkg"}; // VERIFY against your redistributable's models folder.
		} else if (_fx_speakerfocus) {
			effect        = NVAFX_EFFECT_SPEAKER_FOCUS;
			effect_models = {"speaker_focus_48k.trtpkg"}; // VERIFY against your redistributable's models folder.
		} else if (_fx_superres) {
			// Cleanup at 16kHz, then super-resolve up to 48kHz (chained effect).
			chained = true;
			in_rate = 16000;
			if (_fx_denoise && _fx_dereverb) {
				effect        = NVAFX_CHAINED_EFFECT_DEREVERB_DENOISER_16k_SUPERRES_16k_TO_48k;
				effect_models = {"dereverb_denoiser_16k.trtpkg", "superres_16kto48k.trtpkg"}; // VERIFY names.
			} else if (_fx_dereverb) {
				effect        = NVAFX_CHAINED_EFFECT_DEREVERB_16k_SUPERRES_16k_TO_48k;
				effect_models = {"dereverb_16k.trtpkg", "superres_16kto48k.trtpkg"}; // VERIFY names.
			} else {
				effect        = NVAFX_CHAINED_EFFECT_DENOISER_16k_SUPERRES_16k_TO_48k;
				effect_models = {"denoiser_16k.trtpkg", "superres_16kto48k.trtpkg"}; // VERIFY names.
			}
		} else if (_fx_denoise && _fx_dereverb) {
			effect        = NVAFX_EFFECT_DEREVERB_DENOISER;
			effect_models = {"dereverb_denoiser_48k.trtpkg"};
		} else if (!_fx_denoise && _fx_dereverb) {
			effect        = NVAFX_EFFECT_DEREVERB;
			effect_models = {"dereverb_48k.trtpkg"};
		}
#endif
		{ // Build absolute paths to each model file.
			std::filesystem::path models_dir = std::filesystem::absolute(_nvafx->redistributable_path()) / "models";
			_model_path_strs.clear();
			for (auto const& file : effect_models) {
				_model_path_strs.push_back((models_dir / file).generic_string());
			}
			// Keep the legacy single-path fields pointing at the primary model.
			_model_path     = models_dir / effect_models.front();
			_model_path_str = _model_path_strs.front();
		}

		if (_fx_model) {
			// Unload all previous effects.
			_fx.clear();
		} else {
			// Clear all current effects to reset their state.
			clear();
		}

		// Resize the array to fit the new number of channels.
		_fx.resize(_fx_channels);

		for (size_t channel = 0; channel < _fx_channels; channel++) {
			auto& fx = _fx[channel];

			// If there's already an effect here, we don't need to do anything.
			if (fx) {
				continue;
			}

			{ // Otherwise, create a new one just for this.
				NvAFX_Handle pfx   = nullptr;
				NvAFX_Status error = NVAFX_STATUS_SUCCESS;
				if (chained) {
					if (!_nvafx->CreateChainedEffect) {
						throw_log("This effect needs a newer NVIDIA runtime that supports chained effects. Please update NVIDIA Broadcast or the Audio Effects redistributable.");
					}
					error = _nvafx->CreateChainedEffect(effect, &pfx);
				} else {
					error = _nvafx->CreateEffect(effect, &pfx);
				}
				if (error != NVAFX_STATUS_SUCCESS) {
					throw_log("Failed to create effect '%s'. (Code %08" PRIX32 ")", effect, error);
				}
				fx = std::shared_ptr<void>(pfx, [](NvAFX_Handle v) { ::nvidia::afx::afx::instance()->DestroyEffect(v); });
			}
		}

		// Set model path(s). Chained effects need more than one.
		set_model_paths(_model_path_strs);
		for (auto const& p : _model_path_strs) {
			D_LOG("Effect model path: '%s'.", p.c_str());
		}

		// Automatically let the effect pick the correct GPU.
		if (_nvafx->cuda_context()) {
			set<bool>(NVAFX_PARAM_USER_CUDA_CONTEXT, true);
			set<bool>(NVAFX_PARAM_USE_DEFAULT_GPU, false);
			D_LOG("Using custom CUDA context.");
		}

		// Sample Rate. Most effects are 48kHz in and out; Super Resolution takes a
		// 16kHz input and produces a 48kHz output.
		try {
			set<uint32_t>(NVAFX_PARAM_INPUT_SAMPLE_RATE, in_rate);
			set<uint32_t>(NVAFX_PARAM_OUTPUT_SAMPLE_RATE, out_rate);
		} catch (std::exception& ex) {
			D_LOG("Falling back to simple sample rate due error: %s", ex.what());
			try {
				// The deprecated single sample rate only makes sense when input and
				// output match (i.e. not for Super Resolution).
				set<uint32_t>(NVAFX_PARAM_SAMPLE_RATE, out_rate);
			} catch (std::exception& ex) {
				throw_log("Failed to set sample rate entirely: %s", ex.what());
			}
		}
		D_LOG("Sample rate is now %" PRIu32 " Hz in / %" PRIu32 " Hz out.", in_rate, out_rate);

		// Initialize the effect
		for (size_t channel = 0; channel < _fx_channels; channel++) {
			auto& fx = _fx[channel];
			if (auto error = _nvafx->Load(fx.get()); error != NVAFX_STATUS_SUCCESS) {
				throw_log("Failed to initialize effect. (Code %08" PRIX32 ").\0", error);
			}
		}

#ifndef TONPLUGINS_DEMO
		// Mark configuration as dirty to force an update to all effects.
		_cfg_dirty = true;
#endif
		_fx_dirty = false;
	}

#ifndef TONPLUGINS_DEMO
	if (_cfg_dirty) {
		std::shared_ptr<::nvidia::cuda::context_stack> cstk;
		if (auto ctx = _nvafx->cuda_context(); ctx) {
			cstk = ctx->enter();
		}

		set<float>(NVAFX_PARAM_INTENSITY_RATIO, _cfg_intensity);
		set<bool>(NVAFX_PARAM_ENABLE_VAD, _cfg_vad);
		_cfg_dirty = false;
	}
#endif
}

void nvidia::afx::effect::clear()
{
	D_LOG_LOUD("Clearing effect state.");

	// Prevent outside modifications while we're working.
	auto lock = std::unique_lock<decltype(_lock)>(_lock);

	// Prefer the SDK's official reset: it clears each effect's internal state
	// directly, instead of flooding it with silence to "warm" it back to zero.
	if (_nvafx->Reset) {
		bool all_reset = true;
		for (size_t ch = 0; ch < _fx_channels; ch++) {
			if (!_fx[ch]) {
				continue;
			}
			if (auto res = _nvafx->Reset(_fx[ch].get()); res != NVAFX_STATUS_SUCCESS) {
				D_LOG("NvAFX_Reset failed on channel %zu: 0x%08" PRIX32 "; falling back to buffer flush.", ch, res);
				all_reset = false;
				break;
			}
		}
		if (all_reset) {
			return;
		}
	}

	// Fallback for older runtimes: soft-clear by flooding the internal buffer.
	std::vector<float>  data(input_blocksize() * 10, 0.f);
	std::vector<float>  odata(input_blocksize() * 10, 0.f);
	std::vector<float*> channel_data(_fx_channels, data.data());
	std::vector<float*> channel_odata(_fx_channels, odata.data());
	process(const_cast<const float**>(channel_data.data()), channel_odata.data(), data.size());
}

void nvidia::afx::effect::process(const float** input, float** output, size_t samples)
{
	D_LOG_LOUD("Processing %zu samples", samples);

	// Safe-guard against bad usage.
	if ((samples % input_blocksize()) != 0) {
		throw_log("Sample data must be provided as a multiple of %" PRIu32 ".", input_blocksize());
	}

	process(input, samples, output, samples);
}

void nvidia::afx::effect::process(float const** inputs, size_t& input_samples, float** outputs, size_t& output_samples)
{
	try {
		D_LOG_LOUD("Processing %zu samples", input_samples);

		// Prevent outside modifications while we're working.
		auto lock = std::unique_lock<decltype(_lock)>(_lock);

		// Reload the effect
		if (_fx_dirty || _cfg_dirty) {
			load();
		}

		size_t in_blocksize  = input_blocksize();
		size_t samples_total = input_samples;
		size_t samples_left  = input_samples;

		// Clear so the caller doesn't get confused.
		input_samples  = 0;
		output_samples = 0;

		std::shared_ptr<::nvidia::cuda::context_stack> cstk;
		if (auto ctx = _nvafx->cuda_context(); ctx) {
			cstk = ctx->enter();
		}

		// Input and output can have different block sizes (Super Resolution takes
		// 16kHz in and gives 48kHz out), so advance the input and output cursors
		// independently instead of sharing one offset.
		size_t out_blocksize = output_blocksize();
		size_t in_offset     = 0;
		size_t out_offset    = 0;
		while (samples_left >= in_blocksize) {
			for (size_t ch = 0; ch < _fx_channels; ch++) {
				const float* in  = inputs[ch] + in_offset;
				float*       out = outputs[ch] + out_offset;

				if (auto error = _nvafx->Run(_fx[ch].get(), &in, &out, in_blocksize, 1); error != NVAFX_STATUS_SUCCESS) {
					throw_log("Failed to process audio. (Code %08" PRIX32 ").\0", error);
				}
			}

			in_offset += in_blocksize;
			out_offset += out_blocksize;
			samples_left -= in_blocksize;
			output_samples += out_blocksize;
		}
		input_samples = samples_total - samples_left;

		D_LOG_LOUD("Used %zu samples to generate %zu samples", input_samples, output_samples);
	} catch (std::exception const& ex) {
		throw_log("%s", ex.what());
	}
}
