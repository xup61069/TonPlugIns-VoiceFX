// Standalone NVIDIA Audio Effects loader for the VoiceFX standalone VST3.
//
// Unlike the framework version, this one does NOT create a custom CUDA context or
// a D3D device. It loads NVAudioEffects.dll directly and lets the SDK use the
// default GPU (proven to work by tools/afx-smoketest). This removes the CUDA/D3D
// dependencies while keeping the exact interface the effect code expects.
#pragma once
#include <filesystem>
#include <memory>

#include "nvidia-cuda-context.hpp"

#include "warning-disable.hpp"
#include <nvAudioEffects.h>
#include "warning-enable.hpp"

namespace nvidia::afx {
	class afx {
		std::filesystem::path _redist_path;
		void*                 _dll = nullptr; // HMODULE

		afx();

		public:
		~afx();

		// Directory that contains NVAudioEffects.dll and the "models" folder.
		std::filesystem::path redistributable_path();

		// Always null in the standalone build (default GPU, no custom context).
		std::shared_ptr<::nvidia::cuda::context> cuda_context();

#ifdef _WIN32
		void windows_fix_dll_search_paths();
#endif

		// Resolved entry points (optional ones may stay null on older runtimes).
		decltype(NvAFX_GetEffectList)*       GetEffectList       = nullptr;
		decltype(NvAFX_CreateEffect)*        CreateEffect        = nullptr;
		decltype(NvAFX_CreateChainedEffect)* CreateChainedEffect = nullptr;
		decltype(NvAFX_DestroyEffect)*       DestroyEffect       = nullptr;
		decltype(NvAFX_SetU32)*              SetU32              = nullptr;
		decltype(NvAFX_SetString)*           SetString           = nullptr;
		decltype(NvAFX_SetStringList)*       SetStringList       = nullptr;
		decltype(NvAFX_SetFloat)*            SetFloat            = nullptr;
		decltype(NvAFX_GetU32)*              GetU32              = nullptr;
		decltype(NvAFX_GetString)*           GetString           = nullptr;
		decltype(NvAFX_GetFloat)*            GetFloat            = nullptr;
		decltype(NvAFX_Load)*                Load                = nullptr;
		decltype(NvAFX_GetSupportedDevices)* GetSupportedDevices = nullptr;
		decltype(NvAFX_Run)*                 Run                 = nullptr;
		decltype(NvAFX_Reset)*               Reset               = nullptr;

		static std::shared_ptr<::nvidia::afx::afx> instance();
	};
} // namespace nvidia::afx
