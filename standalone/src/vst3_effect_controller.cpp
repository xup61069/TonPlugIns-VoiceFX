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

#include "vst3_effect_controller.hpp"

#include <warning-disable.hpp>
#include <base/source/fstreamer.h>
#include <pluginterfaces/base/ustring.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <warning-enable.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
	// Level is a stepped percentage: 21 positions at 0, 5, 10 ... 100 %
	// (stepCount = 20). Plain RangeParameter cannot express that, because of two
	// details in public.sdk/source/vst/vstparameters.cpp:
	//
	//  * As soon as stepCount > 1, toPlain()/toNormalized() switch to "min + step
	//    index" and ignore max completely, so a 0..100 range would read out as
	//    0..20 -- and RangeParameter's constructor uses toNormalized() on the
	//    default value, which turns a default of 100 % into a normalized 5.0.
	//  * setNormalized() only clamps to 0..1; nothing snaps to a step, and
	//    RangeParameter does not override it. The slider would look like it snaps
	//    while the processor, host automation and the saved state still received
	//    continuous values -- the sound would not be stepped at all.
	//
	// This subclass keeps min/max meaningful as a percentage and does the snapping
	// itself.
	class stepped_percent_parameter : public Steinberg::Vst::RangeParameter {
		public:
		stepped_percent_parameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID tag, const Steinberg::Vst::TChar* units, Steinberg::Vst::ParamValue min_plain, Steinberg::Vst::ParamValue max_plain, Steinberg::Vst::ParamValue default_plain, Steinberg::int32 step_count, Steinberg::int32 flags) : RangeParameter(title, tag, units, min_plain, max_plain, default_plain, step_count, flags)
		{
			// The base constructor normalized the default with its own (stepped)
			// toNormalized(), so redo it with ours.
			info.defaultNormalizedValue = valueNormalized = toNormalized(default_plain);
		}

		// Snapping here is what makes the steps real: everything downstream (the
		// processor, automation, saved state) gets one of the 21 values. VST3Editor
		// calls performEdit(getParamNormalized(id)) right after setParamNormalized(),
		// so this is also what makes the on-screen slider jump between positions.
		bool setNormalized(Steinberg::Vst::ParamValue value) override
		{
			return RangeParameter::setNormalized(quantize(value));
		}

		Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue value) const override
		{
			return getMin() + quantize(value) * (getMax() - getMin());
		}

		Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plain) const override
		{
			if (getMax() <= getMin()) {
				return 0.;
			}
			return quantize((plain - getMin()) / (getMax() - getMin()));
		}

		void toString(Steinberg::Vst::ParamValue value, Steinberg::Vst::String128 string) const override
		{
			char text[32] = {};
			snprintf(text, sizeof(text), "%d %%", static_cast<int>(std::lround(toPlain(value))));
			Steinberg::UString(string, static_cast<Steinberg::int32>(str16BufferSize(Steinberg::Vst::String128))).fromAscii(text);
		}

		// For hosts that let the user type a value. Accepts both a bare number and
		// the "55 %" form we print ourselves; the result is snapped like any other.
		bool fromString(const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& value) const override
		{
			if (string == nullptr) {
				return false;
			}

			char text[64] = {};
			Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(string), Steinberg::tstrlen(string)).toAscii(text, static_cast<Steinberg::int32>(sizeof(text)));

			char*  end   = nullptr;
			double plain = std::strtod(text, &end);
			if (end == text) {
				return false;
			}

			value = toNormalized(std::clamp(plain, getMin(), getMax()));
			return true;
		}

		private:
		Steinberg::Vst::ParamValue quantize(Steinberg::Vst::ParamValue value) const
		{
			value = std::clamp(value, 0., 1.);
			if (info.stepCount < 1) {
				return value;
			}
			double steps = static_cast<double>(info.stepCount);
			return std::round(value * steps) / steps;
		}
	};
} // namespace

vst3::effect::controller::controller()
{
	D_LOG_LOUD("");
	D_LOG("(0x%08" PRIxPTR ") Initializing...", this);

#ifndef TONPLUGINS_DEMO
	{
		// Noise = denoiser, Reverb = dereverb, Both = denoiser+dereverb.
		// Echo Cancel = Acoustic Echo Cancellation (needs a reference signal on the
		// right input channel; see the README). Studio Voice / Speaker Focus were
		// removed: they require the NVIDIA AFX 2.x models, which aren't available on
		// Windows, so they never worked here.
		auto p = new Steinberg::Vst::StringListParameter(STR("Mode"), PARAMETER_MODE, STR("Removal"));
		p->appendString(STR("Noise"));
		p->appendString(STR("Reverb"));
		p->appendString(STR("Both"));
		p->appendString(STR("Echo Cancel"));
		parameters.addParameter(p);
	}
	{
		// 21 steps of 5 %: 0, 5, 10 ... 100. See stepped_percent_parameter above for
		// why this is not a plain RangeParameter.
		auto p = new stepped_percent_parameter(STR("Intensity"), PARAMETER_INTENSITY, STR("%"), 0.0, 100.0, 100.0, PARAMETER_INTENSITY_STEPS, Steinberg::Vst::ParameterInfo::ParameterFlags::kCanAutomate);
		parameters.addParameter(p);
	}
	{
		// Super Resolution on/off. Adds high-frequency detail on top of the
		// Noise/Echo/Both modes (ignored for Studio Voice / Speaker Focus).
		auto p = new Steinberg::Vst::StringListParameter(STR("Super Resolution"), PARAMETER_SUPERRES);
		p->appendString(STR("Off"));
		p->appendString(STR("On"));
		parameters.addParameter(p);
	}
#endif
}

vst3::effect::controller::~controller() {}

tresult PLUGIN_API vst3::effect::controller::initialize(FUnknown* context)
{
	D_LOG_LOUD("");
	if (tresult result = EditControllerEx1::initialize(context); result != kResultOk) {
		D_LOG("(0x%08" PRIxPTR ") Initialization failed with error code 0x%" PRIx32 ".", this, static_cast<int32_t>(result));
		return result;
	}

	D_LOG("(0x%08" PRIxPTR ") Initialized.", this);
	return kResultOk;
}

tresult PLUGIN_API vst3::effect::controller::setComponentState(IBStream* state)
{
	D_LOG_LOUD("");
	if (state == nullptr) {
		return kResultFalse;
	}

	// Must match the byte layout written by the processor's getState():
	// bool denoise, bool dereverb, float intensity, bool superres, bool aec.
	Steinberg::IBStreamer streamer(state, kLittleEndian);
#ifndef TONPLUGINS_DEMO
	if (!streamer.readBool(_enable_denoise)) {
		return kResultFalse;
	}
	if (!streamer.readBool(_enable_dereverb)) {
		return kResultFalse;
	}
	if (!streamer.readFloat(_intensity)) {
		return kResultFalse;
	}
	// Fields added later. Presets saved by older versions won't have them, so a
	// failed read just means "use the default" instead of being an error.
	if (!streamer.readBool(_enable_superres)) {
		_enable_superres = false;
	}
	if (!streamer.readBool(_enable_aec)) {
		_enable_aec = false;
	}
#endif

	return kResultOk;
}

tresult PLUGIN_API vst3::effect::controller::setChannelContextInfos(IAttributeList* list)
{
	D_LOG_LOUD("");
	return kResultOk;
}

FUnknown* vst3::effect::controller::create(void* data)
{
	D_LOG_STATIC_LOUD("");
	return static_cast<IEditController*>(new controller());
}

Steinberg::IPlugView* PLUGIN_API vst3::effect::controller::createView(Steinberg::FIDString name)
{
	D_LOG_LOUD("");
	// Simple flat gray editor described entirely in resource/voicefx.uidesc.
	// VST3Editor binds each control to its parameter by matching the control-tag
	// values in the .uidesc to our FOURCC parameter IDs, so no extra wiring is
	// needed here. The .uidesc is shipped in the bundle's Contents/Resources.
	if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
		return new VSTGUI::VST3Editor(this, "view", "voicefx.uidesc");
	}
	return nullptr;
}
