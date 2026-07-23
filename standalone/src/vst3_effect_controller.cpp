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
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <warning-enable.hpp>

#include <cstring>

vst3::effect::controller::controller()
{
	D_LOG_LOUD("");
	D_LOG("(0x%08" PRIxPTR ") Initializing...", this);

#ifndef TONPLUGINS_DEMO
	{
		// Keep the first three entries (Noise/Echo/Both) in their original order so
		// that presets saved by older versions keep meaning the same thing. The two
		// new entries need the NVIDIA AFX 2.x runtime and models.
		auto p = new Steinberg::Vst::StringListParameter(STR("Mode"), PARAMETER_MODE, STR("Removal"));
		p->appendString(STR("Noise"));
		p->appendString(STR("Echo"));
		p->appendString(STR("Both"));
		p->appendString(STR("Studio Voice"));
		p->appendString(STR("Speaker Focus"));
		parameters.addParameter(p);
	}
	{
		auto p = new Steinberg::Vst::RangeParameter(STR("Intensity"), PARAMETER_INTENSITY, STR("%"), 0.0, 100.0, 100.0, 0, Steinberg::Vst::ParameterInfo::ParameterFlags::kCanAutomate);
		//p->setPrecision(2);
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

	Steinberg::IBStreamer streamer(state, kLittleEndian);
#ifndef TONPLUGINS_DEMO
	if (!streamer.readBool(_enable_echo_removal)) {
		return kResultFalse;
	}
	if (!streamer.readBool(_enable_reverb_removal)) {
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
	if (!streamer.readBool(_enable_studio_voice)) {
		_enable_studio_voice = false;
	}
	if (!streamer.readBool(_enable_speaker_focus)) {
		_enable_speaker_focus = false;
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
