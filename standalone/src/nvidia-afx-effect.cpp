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
	enable_aec(false);
#endif

#ifndef TONPLUGINS_DEMO
	// Must match the Intensity parameter's default in the controller (100%),
	// otherwise the editor shows one value while the effect runs at another until
	// the slider is touched.
	intensity(1.0);
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
	// Loop over live handles (AEC uses a single handle even when _fx_channels > 1).
	for (size_t ch = 0; ch < _fx.size(); ch++) {
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
	for (size_t ch = 0; ch < _fx.size(); ch++) {
		if (auto res = _nvafx->SetFloat(_fx[ch].get(), key, value); res != NVAFX_STATUS_SUCCESS) {
			throw_log("%s(%s, %f) failed: 0x%08" PRIX32 ".", __FUNCTION_SIG__, key, value, res);
		}
	}
}

template<>
void nvidia::afx::effect::set(NvAFX_ParameterSelector key, const char* value)
{
	for (size_t ch = 0; ch < _fx.size(); ch++) {
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

	for (size_t ch = 0; ch < _fx.size(); ch++) {
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
	}
}

bool nvidia::afx::effect::aec_enabled()
{
	return _fx_aec;
}

void nvidia::afx::effect::enable_aec(bool v)
{
	D_LOG_LOUD("Setting AEC to %s.", v ? "enabled" : "disabled");

	auto lock = std::unique_lock<decltype(_lock)>(_lock);
	if (v != _fx_aec) {
		_fx_aec   = v;
		_fx_dirty = true;
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
		_cfg_intensity  = v;
		_cfg_changed_at = std::chrono::steady_clock::now().time_since_epoch().count();
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
		_cfg_vad        = v;
		_cfg_changed_at = std::chrono::steady_clock::now().time_since_epoch().count();
	}
}

#endif

#ifndef TONPLUGINS_DEMO
// How long the Level value has to hold still before the effect is rebuilt.
// A rebuild costs roughly 85ms on the audio thread, so it must not run on every
// mouse-move event while the slider is being dragged -- only once the user lets go.
static constexpr std::chrono::milliseconds intensity_settle_time{200};

bool nvidia::afx::effect::config_applies() const
{
	// Only the plain 48kHz effects have an intensity ratio (and VAD).
	//
	// Echo Cancel exposes neither; setting them would fail. The chained Super
	// Resolution effects accept NvAFX_SetFloat but ignore the value outright --
	// measured on SDK 1.6.1.2, three handles loaded at 1.0 / 0.5 / 0.0 produce
	// byte-identical audio, and NvAFX_GetFloat cannot even read the parameter
	// back. In both cases Level does nothing, so it must not trigger a rebuild.
	return !_fx_aec && !_fx_superres;
}

void nvidia::afx::effect::apply_config()
{
	if (config_applies()) {
		set<float>(NVAFX_PARAM_INTENSITY_RATIO, _cfg_intensity);
		set<bool>(NVAFX_PARAM_ENABLE_VAD, _cfg_vad);
	}

	// Recorded as applied either way, so a Level change in a mode that ignores it
	// doesn't leave the effect looking permanently out of date.
	_fx_intensity = _cfg_intensity.load();
	_fx_vad       = _cfg_vad.load();
}

bool nvidia::afx::effect::config_reload_due() const
{
	if (!config_applies()) {
		return false;
	}

	if ((_cfg_intensity.load() == _fx_intensity.load()) && (_cfg_vad.load() == _fx_vad.load())) {
		return false;
	}

	std::chrono::steady_clock::time_point changed_at{std::chrono::steady_clock::duration{_cfg_changed_at.load()}};
	return (std::chrono::steady_clock::now() - changed_at) >= intensity_settle_time;
}
#endif

void nvidia::afx::effect::load()
{
	D_LOG_LOUD("");
	char message_buffer[1024] = {0};

	auto lock = std::unique_lock<decltype(_lock)>(_lock);

#ifndef TONPLUGINS_DEMO
	// NvAFX_Load bakes the intensity ratio and VAD flag into the effect. Setting
	// them on a loaded effect is accepted (NvAFX_SetFloat returns SUCCESS and
	// NvAFX_GetFloat even reports the new value) but the audio keeps using the
	// value from load time -- which is why the Level slider used to do nothing.
	//
	// Measured on SDK 1.6.1.2: a second NvAFX_Load fails with MODEL_LOAD_FAILED,
	// and NvAFX_Reset does not re-read the parameters either -- it leaves the
	// effect no longer denoising at all. So the only way to change them is to
	// build the effect from scratch, which config_reload_due() defers until the
	// user has stopped moving the slider.
	if (config_reload_due()) {
		_fx_dirty = true;
	}
#endif

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
		// effect with two model files. AEC is a stand-alone 48kHz effect that takes
		// two input channels (mic + reference) and produces one cleaned channel.
		NvAFX_EffectSelector     effect   = NVAFX_EFFECT_DENOISER;
		bool                     chained  = false;
		uint32_t                 in_rate  = 48000;
		uint32_t                 out_rate = 48000;
		std::vector<std::string> effect_models{"denoiser_48k.trtpkg"};
#ifndef TONPLUGINS_DEMO
		if (_fx_aec) {
			// Acoustic Echo Cancellation. Single handle, 2-in (mic + reference) / 1-out.
			effect        = NVAFX_EFFECT_AEC;
			effect_models = {"aec_48k.trtpkg"};
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

		// Unload all previous effects. A handle can only be loaded once -- a second
		// NvAFX_Load returns MODEL_LOAD_FAILED and the parameters set before the
		// first one stay in force -- so a rebuild always starts from fresh handles.
		_fx.clear();

		// One effect handle per channel for the classic per-channel effects, but a
		// SINGLE handle for AEC (it consumes mic + reference together and emits one
		// cleaned channel). Resize the array accordingly.
		size_t handle_count = _fx_aec ? 1u : static_cast<size_t>(_fx_channels);
		_fx.resize(handle_count);

		for (size_t channel = 0; channel < _fx.size(); channel++) {
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
		} else {
			// Standalone build: no custom CUDA context, so use the default GPU.
			set<bool>(NVAFX_PARAM_USE_DEFAULT_GPU, true);
			D_LOG("Using the default GPU.");
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

#ifndef TONPLUGINS_DEMO
		// Intensity and VAD have to be in place BEFORE NvAFX_Load; that call is what
		// bakes them into the effect.
		apply_config();
		D_LOG("Building the effect with intensity %f.", _cfg_intensity.load());
#endif

		// Initialize the effect
		for (size_t channel = 0; channel < _fx.size(); channel++) {
			auto& fx = _fx[channel];
			if (auto error = _nvafx->Load(fx.get()); error != NVAFX_STATUS_SUCCESS) {
				throw_log("Failed to initialize effect. (Code %08" PRIX32 ").\0", error);
			}
		}

		_fx_dirty = false;
	}
}

// There used to be a clear() here that soft-reset the effect through NvAFX_Reset.
// It is gone: on SDK 1.6.1.2 NvAFX_Reset reports success but leaves the effect no
// longer denoising (measured: output energy jumps from 0.10 to 800 on a signal it
// had been cleaning), so it is worse than useless. Anything that needs a clean
// effect has to rebuild it -- see load().

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

		// Reload the effect. config_reload_due() covers a settled Level change,
		// which can only be applied by rebuilding.
		if (_fx_dirty || config_reload_due()) {
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
#ifndef TONPLUGINS_DEMO
			if (_fx_aec) {
				// AEC: one handle, two input channels (0 = mic, 1 = reference) into a
				// single cleaned output channel. The caller must provide at least two
				// input buffers; only outputs[0] is written (the VST layer mirrors it).
				const float* in[2]  = {inputs[0] + in_offset, inputs[1] + in_offset};
				float*       out[1] = {outputs[0] + out_offset};
				if (auto error = _nvafx->Run(_fx[0].get(), in, out, static_cast<unsigned>(in_blocksize), 2); error != NVAFX_STATUS_SUCCESS) {
					throw_log("Failed to process AEC audio. (Code %08" PRIX32 ").\0", error);
				}
			} else
#endif
			{
				for (size_t ch = 0; ch < _fx_channels; ch++) {
					const float* in  = inputs[ch] + in_offset;
					float*       out = outputs[ch] + out_offset;

					if (auto error = _nvafx->Run(_fx[ch].get(), &in, &out, static_cast<unsigned>(in_blocksize), 1); error != NVAFX_STATUS_SUCCESS) {
						throw_log("Failed to process audio. (Code %08" PRIX32 ").\0", error);
					}
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
