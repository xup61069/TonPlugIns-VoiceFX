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

#include "vst3_effect_processor.hpp"
#include "vst3_effect_controller.hpp"

#include "warning-disable.hpp"
#include <base/source/fstreamer.h>
#include <cstring>
#include <filesystem>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include "warning-enable.hpp"

#if WIN32
#include "warning-disable.hpp"
#include <Windows.h>
#include "warning-enable.hpp"
#endif

vst3::effect::processor::processor() : _dirty(true), _channels(0), _samplerate(0), _resample_in(false), _resample_out(false), _delay(0), _local_delay(0), _in_lock(), _in_unresampled(), _in_resampler(), _in_resampled(), _fx(), _out_lock(), _out_unresampled(), _out_resampler(), _out_resampled(), _lock(), _worker(), _worker_cv(), _worker_quit(false), _worker_signal(false)
{
	D_LOG_LOUD("");
	try {
		D_LOG("Initializing...", this);

		// Assign the proper controller
		setControllerClass(vst3::effect::controller_uid);

		// Require some things from the host.
		processContextRequirements.needContinousTimeSamples();
		processContextRequirements.needSamplesToNextClock();

		{ // Initialize worker thread for audio processing.
			std::unique_lock<std::mutex> lock(_lock);
			_worker_quit   = false;
			_worker_signal = false;
			_worker        = std::thread([this]() { this->worker(); });
		}

		{ // Allocate the necessary resources for starting off.
			std::unique_lock<std::mutex> lock(_lock);
			_fx = std::make_shared<::nvidia::afx::effect>();
		}
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

vst3::effect::processor::~processor()
{
	D_LOG_LOUD("");
	try {
		{
			std::unique_lock<std::mutex> lock(_lock);
			_worker_quit = true;
			_worker_cv.notify_all();
		}
		if (_worker.joinable())
			_worker.join();
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

tresult PLUGIN_API vst3::effect::processor::initialize(FUnknown* context)
{
	D_LOG_LOUD("");
	try {
		if (auto res = AudioEffect::initialize(context); res != kResultOk) {
			D_LOG("Initialization failed with error code 0x%" PRIx32 ".", static_cast<int32_t>(res));
			return res;
		}

		// Add audio input and output which default to mono.
		addAudioInput(STR16("In"), SpeakerArr::kStereo);
		addAudioOutput(STR16("Out"), SpeakerArr::kStereo);

		// Reset the channel layout to the defined one.
		set_channel_count(2);

		D_LOG("Initialized.", this);
		return kResultOk;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

tresult PLUGIN_API vst3::effect::processor::canProcessSampleSize(int32 symbolicSampleSize)
{
	D_LOG_LOUD("");
	return (symbolicSampleSize == kSample32) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API vst3::effect::processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts)
{
	D_LOG_LOUD("");
	try {
		if (numIns < 0 || numOuts < 0) {
			D_LOG("Host called setBusArrangement with no inputs or outputs!", this);
			return kInvalidArgument;
		}

		if (numIns > static_cast<int32>(audioInputs.size()) || numOuts > static_cast<int32>(audioOutputs.size())) {
			D_LOG("Host called setBusArrangement with more than the maximum allowed inputs or outputs!", this);
			return kResultFalse;
		}

		for (int32 index = 0; index < static_cast<int32>(audioInputs.size()); ++index) {
			if (index >= numIns) {
				break;
			}
			FCast<Vst::AudioBus>(audioInputs[index].get())->setArrangement(inputs[index]);
			if (index < static_cast<int32>(audioOutputs.size())) {
				FCast<Vst::AudioBus>(audioOutputs[index].get())->setArrangement(inputs[index]);
			}
		}

		{ // Update our own channel information.
			// Count channels.
			SpeakerArrangement arr      = inputs[0];
			size_t             channels = 0;
			while (arr > 0) {
				if ((arr & 0x1) != 0) {
					channels++;
				}
				arr >>= 1;
			}

			set_channel_count(channels);
		}

		return kResultTrue;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

uint32 PLUGIN_API vst3::effect::processor::getLatencySamples()
{
	D_LOG_LOUD("");
	return (uint32)std::min(_delay, std::numeric_limits<decltype(_delay)>::max());
}

uint32 PLUGIN_API vst3::effect::processor::getTailSamples()
{
	D_LOG_LOUD("");
	return (uint32)std::min(_delay, std::numeric_limits<decltype(_delay)>::max());
}

tresult PLUGIN_API vst3::effect::processor::setupProcessing(ProcessSetup& newSetup)
{
	D_LOG_LOUD("");
	try {
		std::unique_lock<std::mutex> lock(_lock);
		// Copy non-important stuff.
		processSetup.maxSamplesPerBlock = newSetup.maxSamplesPerBlock;
		processSetup.processMode        = newSetup.processMode;

		// Check that this is the appropriate sample size.
		if (canProcessSampleSize(newSetup.symbolicSampleSize) != kResultTrue)
			return kResultFalse;
		processSetup.symbolicSampleSize = newSetup.symbolicSampleSize;

		// Check if we need to reset things due to configuration changes.
		_samplerate = static_cast<int64_t>(round(newSetup.sampleRate));
		if (processSetup.sampleRate != newSetup.sampleRate) {
			processSetup.sampleRate = newSetup.sampleRate;
			_dirty                  = true;
		}

		calculate_delay();

		// TODO: Are we able to modify the host here?
		return kResultOk;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

tresult PLUGIN_API vst3::effect::processor::setProcessing(TBool state)
{
	D_LOG_LOUD("");
	try {
		if ((state == TBool(true)) && _dirty) {
			reset();
		}

		return kResultOk;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

tresult PLUGIN_API vst3::effect::processor::process(ProcessData& data)
{
	D_LOG_LOUD("Processing %ld samples", data.numSamples);
	try {
		// Exit-early if there is nothing to process.
		if ((data.numInputs == 0) || (data.numOutputs == 0)) {
			return kResultOk;
		}

		// Exit-early if there is a mismatch between input and output channels.
		if (data.inputs[0].numChannels != data.outputs[0].numChannels) {
			return kResultFalse;
		}

		// Exit-early if the inputs mismatch our configuration.
		if ((data.inputs[0].numChannels != _channels) || (data.outputs[0].numChannels != _channels)) {
			D_LOG("BAIL channel mismatch: in=%d out=%d configured=%zu", data.inputs[0].numChannels, data.outputs[0].numChannels, _channels);
			return kNotInitialized;
		}

		// If we haven't been initialised yet, do it now. Some hosts call process()
		// before setProcessing(true); self-initialise rather than staying silent.
		if (_dirty) {
			if (_channels > 0 && _samplerate > 0) {
				D_LOG("process() found dirty state; self-initialising (rate=%lld channels=%zu).", (long long)_samplerate, _channels);
				reset();
			}
			if (_dirty) {
				D_LOG("BAIL still dirty after reset attempt.");
				return kNotInitialized;
			}
		}

		// Exit-early if host application ignores our delay request.
		if ((_local_delay == _delay) && (data.numSamples < _delay)) {
			D_LOG(
				"Host only provided %zu samples of the required %zu samples to overcome latency. Further behavior is "
				"undefined.",
				data.numSamples, _delay);
		}

		{ // Throttled confirmation that audio is actually flowing through process().
			static std::atomic<int> _dbg{0};
			if ((_dbg++ % 400) == 0) {
				D_LOG("process running: numSamples=%d channels=%zu delay=%lld in_used=%zu out_used=%zu", data.numSamples, _channels, (long long)_delay, _in_unresampled.size() ? _in_unresampled[0]->used() : 0, _out_resampled.size() ? _out_resampled[0]->used() : 0);
			}
		}

// If there were any parameter changes, handle them.
#ifndef TONPLUGINS_DEMO
		bool need_reset = false;
		if (data.inputParameterChanges) {
			for (Steinberg::int32 idx = 0, edx = data.inputParameterChanges->getParameterCount(); idx < edx; ++idx) {
				auto param = data.inputParameterChanges->getParameterData(idx);
				if (param) {
					Steinberg::Vst::ParamValue value;
					Steinberg::int32           sample_offset;
					if (Steinberg::int32 points = param->getPointCount(); points > 0) {
						switch (param->getParameterId()) {
						case PARAMETER_MODE:
							if (param->getPoint(points - 1, sample_offset, value) == kResultTrue) {
								// Normalized -> discrete index across the four modes:
								// 0=Noise, 1=Reverb, 2=Both, 3=Echo Cancel (AEC).
								uint32_t mode = static_cast<uint32_t>(std::lround(value * 3.0));
								if (mode > 3) {
									mode = 3;
								}
								bool aec = (mode == 3);
								_fx->enable_aec(aec);
								_fx->enable_denoise(!aec && (mode == 0 || mode == 2));
								_fx->enable_dereverb(!aec && (mode == 1 || mode == 2));
								if (aec) {
									// AEC is a stand-alone effect; it can't chain with
									// Super Resolution.
									_fx->enable_superres(false);
								}
								// A mode switch can change the effect's sample rate, so
								// rebuild the pipeline to be safe.
								need_reset = true;
							}
							break;
						case PARAMETER_SUPERRES:
							if (param->getPoint(points - 1, sample_offset, value) == kResultTrue) {
								// Ignored while AEC is active (mutually exclusive).
								_fx->enable_superres(value >= 0.5 && !_fx->aec_enabled());
								// Super Resolution changes the effect's input sample rate.
								need_reset = true;
							}
							break;
						case PARAMETER_INTENSITY:
							if (param->getPoint(points - 1, sample_offset, value) == kResultTrue) {
								_fx->intensity(value);
							}
							break;
						}
					}
				}
			}
		}
		if (need_reset) {
			// Rebuild resamplers/buffers for the effect's (possibly new) rates.
			_dirty = true;
			reset();
		}
#endif

		// process Thread:
		// 1. We write each channel's data into the corresponding in_unresampled entry.
		// 2. We check if out_resampled has been signaled, or has enough data.
		// 3. If not, we return an empty buffer or partial buffer.
		//
		// worker Thread:
		// - Resample from in_unresampled to in_resampled.
		// - Apply NVIDIA AFX effect to in_resampled, with output stored in out_unresampled.
		// - Resample data into out_resampled if necessary.
		//
		// Either 2 or 4 threads. 2 seems sane for now, so let's go with that.

		for (size_t idx = 0; idx < _channels; idx++) {
			D_LOG_LOUD("[%zu] %8zu %8zu %8zu %8zu %8ld %lld", idx, _in_unresampled[idx]->used(), _in_resampled.size() > 0 ? _in_resampled[idx]->used() : 0, _out_unresampled.size() > 0 ? _out_unresampled[idx]->used() : 0, _out_resampled[idx]->used(), data.numSamples, _local_delay);
		}

		// Push all data into the unresampled buffer.
		step_copy_in((const float**)(float**)data.inputs[0].channelBuffers32, _in_unresampled, data.numSamples);

		for (size_t idx = 0; idx < _channels; idx++) {
			D_LOG_LOUD("[%zu] %8zu %8zu %8zu %8zu %8ld %lld", idx, _in_unresampled[idx]->used(), _in_resampled.size() > 0 ? _in_resampled[idx]->used() : 0, _out_unresampled.size() > 0 ? _out_unresampled[idx]->used() : 0, _out_resampled[idx]->used(), data.numSamples, _local_delay);
		}

		if (false) {
			// Listen to signal
			{
				std::unique_lock<std::mutex> lock(_lock);
				_worker_signal = true;
				_worker_cv.notify_all();
			}
		} else {
			decltype(_in_unresampled)* ins  = &_in_unresampled;
			decltype(_in_unresampled)* outs = &_out_resampled;

			// Resample input if necessary.
			if (_resample_in) {
				ins  = &_in_unresampled;
				outs = &_in_resampled;

				step_resample_in(*ins, *outs);

				for (size_t idx = 0; idx < _channels; idx++) {
					D_LOG_LOUD("[%zu] %8zu %8zu %8zu %8zu %8ld %lld", idx, _in_unresampled[idx]->used(), _in_resampled.size() > 0 ? _in_resampled[idx]->used() : 0, _out_unresampled.size() > 0 ? _out_unresampled[idx]->used() : 0, _out_resampled[idx]->used(), data.numSamples, _local_delay);
				}

				// Send the processed audio to the output-resampler's input buffer,
				// or straight to the host-rate buffer if no output resampling is
				// needed (e.g. Super Resolution already outputs at the host rate).
				ins  = outs;
				outs = _resample_out ? &_out_unresampled : &_out_resampled;
			} else {
				ins  = &_in_unresampled;
				outs = &_out_resampled;
			}

			step_process(*ins, *outs);

			// Resample output if necessary.
			if (_resample_out) {
				ins  = outs;
				outs = &_out_resampled;

				for (size_t idx = 0; idx < _channels; idx++) {
					D_LOG_LOUD("[%zu] %8zu %8zu %8zu %8zu %8ld %lld", idx, _in_unresampled[idx]->used(), _in_resampled.size() > 0 ? _in_resampled[idx]->used() : 0, _out_unresampled.size() > 0 ? _out_unresampled[idx]->used() : 0, _out_resampled[idx]->used(), data.numSamples, _local_delay);
				}

				step_resample_out(*ins, *outs);
			}
		}

		for (size_t idx = 0; idx < _channels; idx++) {
			D_LOG_LOUD("[%zu] %8zu %8zu %8zu %8zu %8ld %lld", idx, _in_unresampled[idx]->used(), _in_resampled.size() > 0 ? _in_resampled[idx]->used() : 0, _out_unresampled.size() > 0 ? _out_unresampled[idx]->used() : 0, _out_resampled[idx]->used(), data.numSamples, _local_delay);
		}

		step_copy_out(_out_resampled, (float**)data.outputs[0].channelBuffers32, data.numSamples);

		for (size_t idx = 0; idx < _channels; idx++) {
			D_LOG_LOUD("[%zu] %8zu %8zu %8zu %8zu %8ld %lld", idx, _in_unresampled[idx]->used(), _in_resampled.size() > 0 ? _in_resampled[idx]->used() : 0, _out_unresampled.size() > 0 ? _out_unresampled[idx]->used() : 0, _out_resampled[idx]->used(), data.numSamples, _local_delay);
		}

		return kResultOk;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

tresult PLUGIN_API vst3::effect::processor::setState(IBStream* state)
{
	D_LOG_LOUD("");
	try {
		if (state == nullptr) {
			return kResultFalse;
		}

		IBStreamer streamer(state, kLittleEndian);
#ifndef TONPLUGINS_DEMO
		if (bool value = 0; streamer.readBool(value) == true) {
			_fx->enable_denoise(value);
		} else {
			return kResultFalse;
		}
		if (bool value = 0; streamer.readBool(value) == true) {
			_fx->enable_dereverb(value);
		} else {
			return kResultFalse;
		}
		if (float value = 0; streamer.readFloat(value) == true) {
			_fx->intensity(value);
		} else {
			return kResultFalse;
		}
		// Fields appended after the original format. Presets saved by older
		// versions stop here, so a failed read just means "use the default".
		if (bool value = 0; streamer.readBool(value) == true) {
			_fx->enable_superres(value);
		}
		if (bool value = 0; streamer.readBool(value) == true) {
			_fx->enable_aec(value);
		}
		// Force the pipeline to rebuild on the next start so the restored mode's
		// sample rate is picked up.
		_dirty = true;
#endif

		return kResultOk;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

tresult PLUGIN_API vst3::effect::processor::getState(IBStream* state)
{
	D_LOG_LOUD("");
	try {
		if (state == nullptr) {
			return kResultFalse;
		}

		IBStreamer streamer(state, kLittleEndian);
#ifndef TONPLUGINS_DEMO
		streamer.writeBool(_fx->denoise_enabled());
		streamer.writeBool(_fx->dereverb_enabled());
		streamer.writeFloat(_fx->intensity());
		// Fields appended after the original format. Kept at the end so that older
		// hosts/presets that expect only the first three keep working.
		streamer.writeBool(_fx->superres_enabled());
		streamer.writeBool(_fx->aec_enabled());
#endif

		return kResultOk;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		return kInternalError;
	}
}

void vst3::effect::processor::reset()
{
	D_LOG_LOUD("");

	try {
		// Early-exit if the effect isn't "dirty".
		if (!_dirty) {
			return;
		}

		D_LOG("Resetting...", this);
		std::unique_lock<std::mutex> lock(_lock);
		std::unique_lock<std::mutex> ilock(_in_lock);
		std::unique_lock<std::mutex> olock(_out_lock);

		// Reset Effect
		D_LOG_LOUD("Resetting effect...");
		_fx->channels(_channels);
		_fx->load();

		// The effect may use different input and output sample rates (Super
		// Resolution takes 16kHz in and gives 48kHz out), so decide each side
		// independently. When they are equal (all classic modes at 48kHz), this
		// behaves exactly like the previous single-flag logic.
		uint32_t fx_in_rate  = _fx->input_samplerate();
		uint32_t fx_out_rate = _fx->output_samplerate();
		_resample_in         = (static_cast<uint32_t>(_samplerate) != fx_in_rate);
		_resample_out        = (static_cast<uint32_t>(_samplerate) != fx_out_rate);

		// Allocate Buffers
		D_LOG("reset: host %" PRIu64 " Hz, effect %" PRIu32 " Hz in / %" PRIu32 " Hz out, channels=%zu, resample in/out=%d/%d.", _samplerate, fx_in_rate, fx_out_rate, _channels, (int)_resample_in, (int)_resample_out);
		_in_unresampled.resize(_channels);
		_in_unresampled.shrink_to_fit();
		_out_resampled.resize(_channels);
		_out_resampled.shrink_to_fit();
		if (_resample_in) {
			_in_resampled.resize(_channels);
			_in_resampled.shrink_to_fit();
		} else {
			_in_resampled.clear();
		}
		if (_resample_out) {
			_out_unresampled.resize(_channels);
			_out_unresampled.shrink_to_fit();
		} else {
			_out_unresampled.clear();
		}
		for (size_t idx = 0; idx < _channels; idx++) {
			_in_unresampled[idx] = std::make_shared<tonplugins::memory::float_ring_t>(_samplerate);
			_out_resampled[idx]  = std::make_shared<tonplugins::memory::float_ring_t>(_samplerate);
			if (_resample_in) {
				_in_resampled[idx] = std::make_shared<tonplugins::memory::float_ring_t>(fx_in_rate);
			}
			if (_resample_out) {
				_out_unresampled[idx] = std::make_shared<tonplugins::memory::float_ring_t>(fx_out_rate);
			}
		}

		// Input resampler: host rate -> effect input rate.
		if (_resample_in) {
			D_LOG_LOUD("Resetting input resampler (%" PRIu64 " -> %" PRIu32 ")...", _samplerate, fx_in_rate);
			if (!_in_resampler) {
				_in_resampler = std::make_shared<::voicefx::resampler>();
			}
			_in_resampler->channels(_channels);
			_in_resampler->ratio(_samplerate, fx_in_rate);
			_in_resampler->clear();
			_in_resampler->load();
		} else {
			_in_resampler.reset();
		}

		// Output resampler: effect output rate -> host rate.
		if (_resample_out) {
			D_LOG_LOUD("Resetting output resampler (%" PRIu32 " -> %" PRIu64 ")...", fx_out_rate, _samplerate);
			if (!_out_resampler) {
				_out_resampler = std::make_shared<::voicefx::resampler>();
			}
			_out_resampler->channels(_channels);
			_out_resampler->ratio(fx_out_rate, _samplerate);
			_out_resampler->clear();
			_out_resampler->load();
		} else {
			_out_resampler.reset();
		}

		calculate_delay();
		_dirty = false;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::set_channel_count(size_t num)
{
	D_LOG("Adjusting effect channels to %zu...", num);
	try {
		std::unique_lock<std::mutex> lock(_lock);
		_dirty    = true;
		_channels = num;
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::step_copy_in(const float** ins, buffer_container_t& outs, size_t samples)
{
	D_LOG_LOUD("");
	try {
		std::unique_lock<std::mutex> ilock(_in_lock);
		for (size_t idx = 0; idx < _channels; idx++) {
			outs[idx]->write(samples, ins[idx]);
		}
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::step_resample_in(buffer_container_t& ins, buffer_container_t& outs)
{
	D_LOG_LOUD("");
	try {
		std::vector<float const*> inptrs  = {_channels, nullptr};
		std::vector<float*>       outptrs = {_channels, nullptr};

		// Prepare reads/writes
		{
			std::unique_lock<std::mutex> lock(_in_lock);
			for (size_t idx = 0; idx < _channels; idx++) {
				inptrs[idx]  = ins[idx]->peek(ins[idx]->used());
				outptrs[idx] = outs[idx]->poke(outs[idx]->free());
			}

			// Resample
			size_t samples_read    = 0;
			size_t samples_written = 0;
			_in_resampler->process(inptrs.data(), ins[0]->used(), samples_read, outptrs.data(), outs[0]->free(), samples_written);

			// Confirm reads/writes
			for (size_t idx = 0; idx < _channels; idx++) {
				ins[idx]->read(samples_read, nullptr);
				outs[idx]->write(samples_written, nullptr);
			}
		}
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::step_process(buffer_container_t& ins, buffer_container_t& outs)
{
	D_LOG_LOUD("");
	try {
		std::vector<float const*> inptrs  = {_channels, nullptr};
		std::vector<float*>       outptrs = {_channels, nullptr};

		// Feed whole effect input blocks only. The output can be larger than the
		// input (Super Resolution upsamples), so reserve output space based on the
		// effect's output block size rather than assuming it equals the input.
		size_t in_blocksize  = _fx->input_blocksize();
		size_t out_blocksize = _fx->output_blocksize();
		size_t blocks        = ins[0]->used() / in_blocksize;
		size_t samples       = blocks * in_blocksize;

#ifndef TONPLUGINS_DEMO
		// AEC reads two input channels (mic + reference). If the host runs us with a
		// single channel there is no reference to work with, so drain the input and
		// emit nothing rather than read past the input array.
		if (_fx->aec_enabled() && _channels < 2) {
			for (size_t idx = 0; idx < _channels; idx++) {
				ins[idx]->read(ins[idx]->used(), nullptr);
			}
			return;
		}
#endif

		if (samples > 0) {
			size_t out_needed = blocks * out_blocksize;

			// Prepare reads/writes
			for (size_t idx = 0; idx < _channels; idx++) {
				inptrs[idx]  = ins[idx]->peek(samples);
				outptrs[idx] = outs[idx]->poke(out_needed);
			}

			// Consumes the exact amount of input provided; out_samples may differ.
			size_t in_samples  = samples;
			size_t out_samples = 0;
			_fx->process(inptrs.data(), in_samples, outptrs.data(), out_samples);

#ifndef TONPLUGINS_DEMO
			// AEC collapses (mic, reference) into a single cleaned channel written to
			// outptrs[0]. Mirror it into the remaining output channels so the rest of
			// the pipeline (resample/copy-out) sees a full, uniform set of channels.
			if (_fx->aec_enabled() && out_samples > 0) {
				for (size_t idx = 1; idx < _channels; idx++) {
					if (outptrs[idx] && outptrs[0]) {
						memcpy(outptrs[idx], outptrs[0], out_samples * sizeof(float));
					}
				}
			}
#endif

			// Confirm reads/writes
			for (size_t idx = 0; idx < _channels; idx++) {
				ins[idx]->read(in_samples, nullptr);
				outs[idx]->write(out_samples, nullptr);
			}
		}
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::step_resample_out(buffer_container_t& ins, buffer_container_t& outs)
{
	D_LOG_LOUD("");
	try {
		std::vector<float const*> inptrs  = {_channels, nullptr};
		std::vector<float*>       outptrs = {_channels, nullptr};

		{
			// Was previously a no-op default-constructed lock that shadowed the
			// member mutex; lock the real one so the output buffers are protected.
			std::unique_lock<std::mutex> lock(_out_lock);
			// Prepare reads/writes
			for (size_t idx = 0; idx < _channels; idx++) {
				inptrs[idx]  = ins[idx]->peek(ins[idx]->used());
				outptrs[idx] = outs[idx]->poke(outs[idx]->free());
			}

			// Resample (effect output rate -> host rate). This must use the output
			// resampler; it previously used the input resampler by mistake.
			size_t samples_read    = 0;
			size_t samples_written = 0;
			_out_resampler->process(inptrs.data(), ins[0]->used(), samples_read, outptrs.data(), outs[0]->free(), samples_written);

			// Confirm reads/writes
			for (size_t idx = 0; idx < _channels; idx++) {
				ins[idx]->read(samples_read, nullptr);
				outs[idx]->write(samples_written, nullptr);
			}
		}
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::step_copy_out(buffer_container_t& ins, float** outs, size_t samples)
{
	D_LOG_LOUD("");
	try {
		std::vector<float const*> inptrs  = {_channels, nullptr};
		std::vector<float*>       outptrs = {_channels, nullptr};

		// Require that the thread is done writing to the output buffers.
		std::unique_lock lock(_out_lock);
		size_t           avail = ins[0]->used();

		D_LOG_LOUD("Local Delay at %" PRId64 " samples.", _local_delay);
		if (_local_delay < samples) {
			size_t real_avail = std::min(samples - _local_delay, avail);

			if (real_avail < samples) {
				// Return full or partial data.
				size_t offset = samples - real_avail;
				for (size_t idx = 0; idx < _channels; idx++) {
					if (offset > 0) {
						memset(outs[idx], 0, offset * sizeof(float));
					}

					ins[idx]->read(real_avail, outs[idx] + offset);
				}
			} else {
				for (size_t idx = 0; idx < _channels; idx++) {
					ins[idx]->read(samples, outs[idx]);
				}
			}

		} else {
			for (size_t idx = 0; idx < _channels; idx++) {
				memset(outs[idx], 0, samples * sizeof(float));
			}
		}
		_local_delay = std::max<int64_t>(0, _local_delay - samples);

	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

void vst3::effect::processor::worker()
{
	D_LOG_LOUD("");
	try {
#if WIN32
		SetThreadPriority(GetCurrentThread(), HIGH_PRIORITY_CLASS);
		SetThreadPriorityBoost(GetCurrentThread(), false);
		SetProcessPriorityBoost(GetCurrentProcess(), false);
#endif

		std::unique_lock<std::mutex> lock(_lock);
		do {
			_worker_cv.wait(lock, [this] { return _worker_quit || _worker_signal; });

			while (_worker_signal) {
				_worker_signal = false; // Set this as early as possible.

				decltype(_in_unresampled)& ins  = _in_unresampled;
				decltype(_in_unresampled)& outs = _out_resampled;

				// NOTE: This worker path is currently unused (process() runs the
				// pipeline synchronously). It is kept building and in sync with the
				// decoupled input/output resampling for possible future use.
				// Resample input if necessary.
				if (_resample_in) {
					ins  = _in_unresampled;
					outs = _in_resampled;

					step_resample_in(ins, outs);

					// Swap things so the next step works.
					ins  = outs;
					outs = _resample_out ? _out_unresampled : _out_resampled;
				} else {
					ins  = _in_unresampled;
					outs = _out_resampled;
				}

				if (_resample_in || _resample_out) {
					step_process(ins, outs);
				} else { // Couldn't figure out how to skip this without an if/else duplication. :/
					std::unique_lock<std::mutex> ilock(_in_lock);
					std::unique_lock<std::mutex> ulock(_out_lock);
					step_process(ins, outs);
				}

				// Resample output if necessary.
				if (_resample_out) {
					ins  = outs;
					outs = _out_resampled;

					step_resample_out(ins, outs);
				}
			}
		} while (!_worker_quit);
	} catch (std::exception const& ex) {
		D_LOG("EXCEPTION: %s", ex.what());
		throw;
	}
}

FUnknown* vst3::effect::processor::create(void* data)
{
	D_LOG_STATIC_LOUD("");
	try {
		return static_cast<IAudioProcessor*>(new processor());
	} catch (std::exception const& ex) {
		D_LOG_STATIC("Exception in create: %s", ex.what());
		return nullptr;
	} catch (...) {
		D_LOG_STATIC("Unknown exception in create.");
		return nullptr;
	}
}

void vst3::effect::processor::calculate_delay()
{
	size_t in_delay  = 0;
	size_t out_delay = 0;
	if (_resample_in) {
		in_delay = ::voicefx::resampler::calculate_delay(_samplerate, _fx->input_samplerate());
	}
	if (_resample_out) {
		out_delay = ::voicefx::resampler::calculate_delay(_fx->output_samplerate(), _samplerate);
	}
	D_LOG_LOUD("In/Out resampler delay %zu %zu", in_delay, out_delay);

	_local_delay = _fx->input_blocksize();
	if (_resample_in || _resample_out) {
		_local_delay += in_delay + out_delay;
		_local_delay *= 2;
	}
	D_LOG("Processing latency appears to be %" PRId64 " samples.", _local_delay);

	// Calculate absolute effect delay
	_delay = _fx->delay();
	_delay += _local_delay;
	_delay += in_delay + out_delay;
	D_LOG("Latency is estimated to be %" PRId64 " samples.", _delay);
}

void vst3::effect::processor::calculate_local_delay() {}
