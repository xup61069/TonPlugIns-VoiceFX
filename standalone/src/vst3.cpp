#include "vst3.hpp"
#include "lib.hpp"
#include "vst3_effect_controller.hpp"
#include "vst3_effect_processor.hpp"

#include "warning-disable.hpp"
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <public.sdk/source/main/pluginfactory.h>
#include "warning-enable.hpp"

#define VOICEFX_VERSION_STR "2.0.0.0"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("Xaymar", "https://xaymar.com/", "mailto:support@xaymar.com")

DEF_CLASS2(INLINE_UID_FROM_FUID(vst3::effect::processor_uid), PClassInfo::kManyInstances, kVstAudioEffectClass, "VoiceFX",
           Vst::kDistributable, Vst::PlugType::kFxRestoration, VOICEFX_VERSION_STR, kVstVersionString,
           vst3::effect::processor::create)

DEF_CLASS2(INLINE_UID_FROM_FUID(vst3::effect::controller_uid), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "VoiceFX Controller", 0, "", VOICEFX_VERSION_STR, kVstVersionString, vst3::effect::controller::create)

END_FACTORY
