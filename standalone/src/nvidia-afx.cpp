#include "nvidia-afx.hpp"
#include "lib.hpp"

#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#include "warning-disable.hpp"
#include <windows.h>
#include "warning-enable.hpp"
#endif

static std::filesystem::path find_sdk_dir()
{
	// Allow an explicit override.
	if (const char* e = std::getenv("NVAFX_SDK")) {
		std::filesystem::path p(e);
		if (std::filesystem::exists(p / "NVAudioEffects.dll")) {
			return p;
		}
	}
	// Standard installation location of the NVIDIA Audio Effects SDK redistributable.
	std::filesystem::path def = "C:/Program Files/NVIDIA Corporation/NVIDIA Audio Effects SDK";
	return def;
}

nvidia::afx::afx::afx() : _redist_path(find_sdk_dir())
{
	D_LOG("Using NVIDIA Audio Effects redistributable at '%s'.", _redist_path.string().c_str());

#ifdef _WIN32
	// Make the redistributable directory the first search path so its co-located
	// CUDA / TensorRT DLLs resolve.
	SetDllDirectoryW(_redist_path.wstring().c_str());

	std::filesystem::path dllp = _redist_path / "NVAudioEffects.dll";
	HMODULE               h    = LoadLibraryExW(dllp.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) {
		throw_log("Failed to load '%s' (error %lu). Is the NVIDIA Audio Effects SDK installed?", dllp.string().c_str(), (unsigned long)GetLastError());
	}
	_dll = reinterpret_cast<void*>(h);

#define LOADSYM(V) V = reinterpret_cast<decltype(V)>(GetProcAddress(h, "NvAFX_" #V))
	LOADSYM(GetEffectList);
	LOADSYM(CreateEffect);
	LOADSYM(CreateChainedEffect);
	LOADSYM(DestroyEffect);
	LOADSYM(SetU32);
	LOADSYM(SetString);
	LOADSYM(SetStringList);
	LOADSYM(SetFloat);
	LOADSYM(GetU32);
	LOADSYM(GetString);
	LOADSYM(GetFloat);
	LOADSYM(Load);
	LOADSYM(GetSupportedDevices);
	LOADSYM(Run);
	LOADSYM(Reset);
#undef LOADSYM

	if (!CreateEffect || !DestroyEffect || !SetU32 || !GetU32 || !SetString || !Load || !Run) {
		throw_log("NVAudioEffects.dll is missing required entry points.");
	}
#else
	throw_log("The VoiceFX standalone build only supports Windows.");
#endif
}

nvidia::afx::afx::~afx()
{
#ifdef _WIN32
	if (_dll) {
		FreeLibrary(reinterpret_cast<HMODULE>(_dll));
		_dll = nullptr;
	}
#endif
}

std::filesystem::path nvidia::afx::afx::redistributable_path()
{
	return _redist_path;
}

std::shared_ptr<::nvidia::cuda::context> nvidia::afx::afx::cuda_context()
{
	return {}; // no custom CUDA context; the effect uses the default GPU
}

#ifdef _WIN32
void nvidia::afx::afx::windows_fix_dll_search_paths()
{
	SetDllDirectoryW(_redist_path.wstring().c_str());
}
#endif

std::shared_ptr<::nvidia::afx::afx> nvidia::afx::afx::instance()
{
	static std::mutex               guard;
	static std::weak_ptr<afx>       weak;
	std::lock_guard<std::mutex>     lock(guard);
	if (auto s = weak.lock()) {
		return s;
	}
	std::shared_ptr<afx> s(new afx());
	weak = s;
	return s;
}
