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

#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "nvidia-afx.hpp"
#include "nvidia-cuda-context.hpp"
#include "nvidia-cuda-stream.hpp"
#include "nvidia-cuda.hpp"

namespace nvidia::afx {
	class effect {
		std::shared_ptr<::nvidia::afx::afx> _nvafx;

		std::recursive_mutex  _lock;
		std::filesystem::path _model_path;
		std::string           _model_path_str;
		// Full model-file paths for the currently selected effect. Chained effects
		// (e.g. Super Resolution) need more than one, passed via NvAFX_SetStringList.
		// Kept as members so the strings outlive the SDK call.
		std::vector<std::string> _model_path_strs;

		// Every flag below is read before it is first written (the setters only act on
		// a real change), so they need an explicit initial value -- reading an
		// indeterminate std::atomic is undefined behaviour.
		std::vector<std::shared_ptr<void>> _fx;
		std::atomic_uint8_t                _fx_channels{0};
		std::atomic_bool                   _fx_dirty{true};
#ifndef TONPLUGINS_DEMO
		std::atomic_bool _fx_denoise{false};
		std::atomic_bool _fx_dereverb{false};
		std::atomic_bool _fx_superres{false}; // Super Resolution (adds high-frequency detail).
		// Acoustic Echo Cancellation. Unlike the other effects it takes TWO input
		// channels (0 = microphone, 1 = reference / far-end) and produces ONE cleaned
		// channel, so it uses a single effect handle instead of one per channel.
		std::atomic_bool _fx_aec{false};
#endif

#ifndef TONPLUGINS_DEMO
		// What the user asked for...
		std::atomic<float> _cfg_intensity{1.f};
		std::atomic_bool   _cfg_vad{false};
		// ...and what is actually baked into the effect that is currently loaded.
		// These only differ while the Level slider is being dragged; see
		// config_reload_due().
		std::atomic<float> _fx_intensity{1.f};
		std::atomic_bool   _fx_vad{false};
		// steady_clock tick count of the last change, for the settle timer.
		std::atomic<int64_t> _cfg_changed_at{0};
#endif

		public:
		effect();
		~effect();

		protected:
		template<typename T>
		T get(NvAFX_ParameterSelector key);

		template<typename T>
		void set(NvAFX_ParameterSelector key, T value);

		// Applies one or more model files to every channel's effect handle.
		// A single file uses NvAFX_SetString; multiple files (chained effects)
		// use NvAFX_SetStringList.
		void set_model_paths(std::vector<std::string> const& paths);

		// Whether the currently selected effect has an intensity ratio / VAD at all.
		bool config_applies() const;

		// Pushes the user-facing settings (intensity, VAD) onto every effect handle
		// and records them as the loaded ones. Only has any effect before
		// NvAFX_Load; see load().
		void apply_config();

		// True once the user has stopped moving the Level slider and the value the
		// running effect was built with is out of date. Applying a new value means
		// rebuilding the effect (~85ms), so it must not happen on every mouse-move.
		bool config_reload_due() const;

		public /* Effect Information */:
		uint32_t input_samplerate();
		uint32_t output_samplerate();

		uint32_t input_blocksize();
		uint32_t output_blocksize();

		uint32_t input_channels();
		uint32_t output_channels();

		static size_t delay();

		public /* Wrapper Information */:
		uint8_t channels();
		void    channels(uint8_t v);

#ifndef TONPLUGINS_DEMO
		bool denoise_enabled();
		void enable_denoise(bool v);

		bool dereverb_enabled();
		void enable_dereverb(bool v);

		// Super Resolution: runs the cleanup at 16kHz and rebuilds a 48kHz signal
		// with more high-frequency detail. Only combines with denoise/dereverb.
		bool superres_enabled();
		void enable_superres(bool v);

		// Acoustic Echo Cancellation: cancels the far-end/loudspeaker signal from the
		// microphone. Needs a reference channel (channel 1). Mutually exclusive with
		// the other effects; when on, denoise/dereverb/superres are ignored.
		bool aec_enabled();
		void enable_aec(bool v);
#endif

#ifndef TONPLUGINS_DEMO
		float intensity();
		void  intensity(float v);

		bool voice_activity_detection();
		void voice_activity_detection(bool v);
#endif

		void load();

		void process(const float** input, float** output, size_t samples);

		void process(float const** inputs, size_t& input_samples, float** outputs, size_t& output_samples);
	};
} // namespace nvidia::afx
