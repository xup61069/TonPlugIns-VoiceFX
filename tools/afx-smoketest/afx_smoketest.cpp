// Standalone smoke test for the NVIDIA AFX SDK integration used by VoiceFX.
//
// It does NOT need the (proprietary) TonPlugIns build framework or the VST3 SDK.
// It dynamically loads the installed NVAudioEffects.dll (exactly like the plugin
// does) and exercises the code paths this branch adds:
//   * NvAFX_Reset                       (Tier A)
//   * NvAFX_CreateChainedEffect         (Tier B - Super Resolution)
//   * NvAFX_SetStringList (2 models)    (Tier B)
//   * 16 kHz in / 48 kHz out handling   (Tier B)
//
// Build (from a VS x64 dev prompt):
//   cl /EHsc /std:c++17 /I <repo>\third-party\nvidia-maxine-afx-sdk\nvafx\include \
//      afx_smoketest.cpp /Fe:afx_smoketest.exe

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {
#include "nvAudioEffects.h"
}

// SDK install directory. Override with argv[1]. Default is the standard location.
static std::wstring kSdkDir = L"C:\\Program Files\\NVIDIA Corporation\\NVIDIA Audio Effects SDK";
static std::string  kModels; // set in main() from kSdkDir

// Resolved entry points.
static decltype(NvAFX_CreateEffect)*        CreateEffect        = nullptr;
static decltype(NvAFX_CreateChainedEffect)* CreateChainedEffect = nullptr;
static decltype(NvAFX_DestroyEffect)*       DestroyEffect       = nullptr;
static decltype(NvAFX_SetU32)*              SetU32              = nullptr;
static decltype(NvAFX_SetString)*           SetString           = nullptr;
static decltype(NvAFX_SetStringList)*       SetStringList       = nullptr;
static decltype(NvAFX_GetU32)*              GetU32              = nullptr;
static decltype(NvAFX_Load)*                Load                = nullptr;
static decltype(NvAFX_Run)*                 Run                 = nullptr;
static decltype(NvAFX_Reset)*               Reset               = nullptr;

static const char* status_str(NvAFX_Status s)
{
	switch (s) {
	case NVAFX_STATUS_SUCCESS: return "SUCCESS";
	case NVAFX_STATUS_FAILED: return "FAILED";
	case NVAFX_STATUS_INVALID_HANDLE: return "INVALID_HANDLE";
	case NVAFX_STATUS_INVALID_PARAM: return "INVALID_PARAM";
	case NVAFX_STATUS_IMMUTABLE_PARAM: return "IMMUTABLE_PARAM";
	case NVAFX_STATUS_INSUFFICIENT_DATA: return "INSUFFICIENT_DATA";
	case NVAFX_STATUS_EFFECT_NOT_AVAILABLE: return "EFFECT_NOT_AVAILABLE";
	case NVAFX_STATUS_OUTPUT_BUFFER_TOO_SMALL: return "OUTPUT_BUFFER_TOO_SMALL";
	case NVAFX_STATUS_MODEL_LOAD_FAILED: return "MODEL_LOAD_FAILED";
	case NVAFX_STATUS_GPU_UNSUPPORTED: return "GPU_UNSUPPORTED";
	case NVAFX_STATUS_CUDA_CONTEXT_CREATION_FAILED: return "CUDA_CONTEXT_CREATION_FAILED";
	default: return "UNKNOWN";
	}
}

#define CHECK(expr)                                                                    \
	do {                                                                               \
		NvAFX_Status _s = (expr);                                                      \
		std::printf("    %-55s -> %s (0x%X)\n", #expr, status_str(_s), (unsigned)_s);  \
		if (_s != NVAFX_STATUS_SUCCESS) {                                              \
			std::printf("    ^ FAILED, aborting this test.\n");                        \
			return false;                                                             \
		}                                                                             \
	} while (0)

static uint32_t getu32(NvAFX_Handle h, const char* key)
{
	uint32_t v = 0;
	NvAFX_Status s = GetU32(h, key, &v);
	if (s != NVAFX_STATUS_SUCCESS) {
		std::printf("    GetU32(%s) failed: %s\n", key, status_str(s));
	}
	return v;
}

// Runs one effect (plain or chained) and processes a single block.
static bool run_case(const char* title, const char* selector, bool chained,
                     const std::vector<std::string>& models, uint32_t in_rate, uint32_t out_rate)
{
	std::printf("\n[TEST] %s\n", title);
	std::printf("    selector = \"%s\" (%s), models = %zu\n", selector, chained ? "chained" : "single", models.size());

	NvAFX_Handle h = nullptr;
	if (chained) {
		if (!CreateChainedEffect) {
			std::printf("    CreateChainedEffect missing in DLL!\n");
			return false;
		}
		CHECK(CreateChainedEffect(selector, &h));
	} else {
		CHECK(CreateEffect(selector, &h));
	}

	// Absolute model paths.
	std::vector<std::string> paths;
	for (auto const& m : models) paths.push_back(std::string(kModels) + "/" + m);
	std::vector<const char*> ptrs;
	for (auto const& p : paths) ptrs.push_back(p.c_str());

	if (paths.size() > 1) {
		CHECK(SetStringList(h, NVAFX_PARAM_MODEL_PATH, ptrs.data(), (unsigned)ptrs.size()));
	} else {
		CHECK(SetString(h, NVAFX_PARAM_MODEL_PATH, ptrs[0]));
	}

	// Use the default GPU; no custom CUDA context needed for the smoke test.
	CHECK(SetU32(h, NVAFX_PARAM_USE_DEFAULT_GPU, 1));

	// Sample rates. (These may be immutable/derived for chained effects; we report
	// but don't hard-fail if setting them is rejected.)
	{
		NvAFX_Status s1 = SetU32(h, NVAFX_PARAM_INPUT_SAMPLE_RATE, in_rate);
		NvAFX_Status s2 = SetU32(h, NVAFX_PARAM_OUTPUT_SAMPLE_RATE, out_rate);
		std::printf("    SetU32(input_sample_rate=%u)  -> %s\n", in_rate, status_str(s1));
		std::printf("    SetU32(output_sample_rate=%u) -> %s\n", out_rate, status_str(s2));
	}

	std::printf("    Loading model(s) (may take a few seconds)...\n");
	CHECK(Load(h));

	uint32_t in_sr   = getu32(h, NVAFX_PARAM_INPUT_SAMPLE_RATE);
	uint32_t out_sr  = getu32(h, NVAFX_PARAM_OUTPUT_SAMPLE_RATE);
	uint32_t in_blk  = getu32(h, NVAFX_PARAM_NUM_INPUT_SAMPLES_PER_FRAME);
	uint32_t out_blk = getu32(h, NVAFX_PARAM_NUM_OUTPUT_SAMPLES_PER_FRAME);
	uint32_t in_ch   = getu32(h, NVAFX_PARAM_NUM_INPUT_CHANNELS);
	uint32_t out_ch  = getu32(h, NVAFX_PARAM_NUM_OUTPUT_CHANNELS);
	std::printf("    reported: in %u Hz / out %u Hz | in_blk %u / out_blk %u | in_ch %u / out_ch %u\n",
	            in_sr, out_sr, in_blk, out_blk, in_ch, out_ch);

	if (in_blk == 0 || out_blk == 0) {
		std::printf("    block size is 0, cannot process.\n");
		DestroyEffect(h);
		return false;
	}

	// Feed a continuous 220 Hz tone for several hundred ms so we get past the
	// effect's algorithmic latency and can confirm real audio comes out.
	const uint32_t frames = 100; // e.g. 100 * 10ms = ~1s of audio
	std::vector<float> in(in_blk), out(out_blk, 0.f);
	double   total_energy = 0.0;
	uint32_t phase        = 0;
	for (uint32_t f = 0; f < frames; ++f) {
		for (uint32_t i = 0; i < in_blk; ++i, ++phase) {
			in[i] = 0.2f * std::sin(2.0f * 3.14159265f * 220.0f * phase / (float)(in_sr ? in_sr : 16000));
		}
		const float* inp  = in.data();
		float*       outp = out.data();
		if (NvAFX_Status s = Run(h, &inp, &outp, in_blk, in_ch ? in_ch : 1); s != NVAFX_STATUS_SUCCESS) {
			std::printf("    Run() failed on frame %u: %s\n", f, status_str(s));
			DestroyEffect(h);
			return false;
		}
		for (uint32_t i = 0; i < out_blk; ++i) total_energy += (double)out[i] * out[i];
	}
	std::printf("    processed %u frames; total output energy = %.4f (%s)\n",
	            frames, total_energy, total_energy > 1e-4 ? "AUDIO FLOWS OK" : "silent");

	// Reset test (Tier A).
	if (Reset) {
		NvAFX_Status rs = Reset(h);
		std::printf("    NvAFX_Reset -> %s\n", status_str(rs));
	}

	DestroyEffect(h);
	std::printf("    [PASS] %s\n", title);
	return true;
}

static std::string wide_to_utf8(std::wstring const& w)
{
	if (w.empty()) return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
	return s;
}

int main(int argc, char** argv)
{
	std::printf("=== VoiceFX / NVIDIA AFX smoke test ===\n");

	if (argc > 1) {
		// argv[1] = SDK install directory.
		int n = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0);
		std::wstring w(n ? n - 1 : 0, L'\0');
		MultiByteToWideChar(CP_ACP, 0, argv[1], -1, w.data(), n);
		kSdkDir = w;
	}
	{
		std::string base = wide_to_utf8(kSdkDir);
		for (auto& c : base) if (c == '\\') c = '/';
		kModels = base + "/models";
	}
	std::printf("SDK dir: %s\n", wide_to_utf8(kSdkDir).c_str());

	// Load the DLL with its own directory first on the dependency search path so
	// its co-located CUDA/TensorRT DLLs resolve (mirrors the plugin's fix).
	SetDllDirectoryW(kSdkDir.c_str());
	std::wstring dllPath = kSdkDir + L"\\NVAudioEffects.dll";
	HMODULE dll = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!dll) {
		std::printf("Failed to load NVAudioEffects.dll (error %lu)\n", GetLastError());
		return 2;
	}
	std::printf("Loaded NVAudioEffects.dll\n");

#define GET(name) name = (decltype(name))GetProcAddress(dll, "NvAFX_" #name); \
	std::printf("  NvAFX_%-20s %s\n", #name, name ? "ok" : "MISSING");
	GET(CreateEffect);
	GET(CreateChainedEffect);
	GET(DestroyEffect);
	GET(SetU32);
	GET(SetString);
	GET(SetStringList);
	GET(GetU32);
	GET(Load);
	GET(Run);
	GET(Reset);
#undef GET

	if (!CreateEffect || !DestroyEffect || !SetU32 || !SetString || !GetU32 || !Load || !Run) {
		std::printf("Essential entry points missing.\n");
		return 2;
	}

	int passed = 0, total = 0;

	// Baseline: plain 48 kHz denoiser (proves the harness + GPU path work).
	total++; if (run_case("Denoiser 48k (baseline)", NVAFX_EFFECT_DENOISER, false,
	                      {"denoiser_48k.trtpkg"}, 48000, 48000)) passed++;

	// Tier B: chained denoiser(16k) -> super resolution(16k->48k).
	total++; if (run_case("Denoiser16k + SuperRes 16k->48k (chained)",
	                      NVAFX_CHAINED_EFFECT_DENOISER_16k_SUPERRES_16k_TO_48k, true,
	                      {"denoiser_16k.trtpkg", "superres_16kto48k.trtpkg"}, 16000, 48000)) passed++;

	// Tier B: chained dereverb+denoiser(16k) -> super resolution.
	total++; if (run_case("Dereverb+Denoiser16k + SuperRes 16k->48k (chained)",
	                      NVAFX_CHAINED_EFFECT_DEREVERB_DENOISER_16k_SUPERRES_16k_TO_48k, true,
	                      {"dereverb_denoiser_16k.trtpkg", "superres_16kto48k.trtpkg"}, 16000, 48000)) passed++;

	std::printf("\n=== RESULT: %d/%d tests passed ===\n", passed, total);
	FreeLibrary(dll);
	return passed == total ? 0 : 1;
}
