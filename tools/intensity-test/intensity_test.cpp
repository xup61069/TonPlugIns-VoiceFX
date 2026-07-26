// Regression test for the Level (intensity) parameter.
//
// This exercises the plugin's own nvidia::afx::effect class -- not a copy of it --
// driving it the way vst3::effect::processor does when the Level slider moves.
//
// It exists because the failure it guards against is invisible: NvAFX_SetFloat on
// a loaded effect returns SUCCESS and NvAFX_GetFloat reports the new value, while
// the audio keeps using whatever was set before NvAFX_Load. The plugin therefore
// has to rebuild the effect, deferred until the slider stops moving.
//
// Build (from a VS x64 dev prompt, in the repository root):
//   cl /EHsc /std:c++17 /MT /DNOMINMAX /DQUIET ^
//      /I standalone\src /I standalone\compat ^
//      /I third-party\nvidia-maxine-afx-sdk\nvafx\include ^
//      tools\intensity-test\intensity_test.cpp ^
//      standalone\src\nvidia-afx-effect.cpp standalone\src\nvidia-afx.cpp ^
//      standalone\src\lib.cpp /Fe:intensity_test.exe
//
// One effect instance is reused throughout, like a plugin instance does. (The afx
// loader is a weak_ptr singleton, so destroying every effect unloads
// NVAudioEffects.dll; reloading it in one process is unrelated trouble.)

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "lib.hpp"
#include "nvidia-afx-effect.hpp"

// stderr, so nothing is lost to buffering if a test takes the process down.
#define SAY(...)                            \
	do {                                    \
		std::fprintf(stderr, __VA_ARGS__);  \
		std::fflush(stderr);                \
	} while (0)

// Comfortably longer than the effect's own 200ms settle time.
static const auto settle = std::chrono::milliseconds(260);

// Tone plus broadband noise, identical on every run, so energies are comparable.
static void fill_input(std::vector<float>& buf, unsigned frame, unsigned blk, unsigned rate)
{
	unsigned seed = 12345u + frame * 7919u;
	for (unsigned i = 0; i < blk; ++i) {
		unsigned phase = frame * blk + i;
		float    tone  = 0.20f * std::sin(2.0f * 3.14159265f * 220.0f * phase / (float)rate);
		seed           = seed * 1664525u + 1013904223u;
		float noise    = 0.15f * (((seed >> 9) & 0x7FFF) / 16383.5f - 1.0f);
		buf[i]         = tone + noise;
	}
}

// Feeds whole blocks, like processor::step_process, and sums the output energy.
static double pump(nvidia::afx::effect& fx, unsigned frames)
{
	unsigned           in_blk  = fx.input_blocksize();
	unsigned           out_blk = fx.output_blocksize();
	unsigned           rate    = fx.input_samplerate();
	std::vector<float> in(in_blk), out(out_blk, 0.f);
	double             energy = 0.0;

	for (unsigned f = 0; f < frames; ++f) {
		fill_input(in, f, in_blk, rate);
		const float* inp  = in.data();
		float*       outp = out.data();
		size_t       ins = in_blk, outs = 0;
		fx.process(&inp, ins, &outp, outs);
		for (size_t i = 0; i < outs; ++i) energy += (double)out[i] * out[i];
	}
	return energy;
}

static double measure(nvidia::afx::effect& fx, const char* what)
{
	double e = pump(fx, 100);
	SAY("    %-34s energy = %.4f\n", what, e);
	return e;
}

static double elapsed_ms(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static int failures = 0;

static void check(const char* name, bool ok)
{
	SAY("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok) failures++;
}

int main()
{
	voicefx::initialize();
	SAY("=== Level (intensity) regression test ===\n");

	auto  fxp = std::make_unique<nvidia::afx::effect>();
	auto& fx  = *fxp;

	// --- Noise: Level has to change the audio, in both directions -----------
	{
		SAY("\n[Noise / denoiser 48k]\n");
		fx.channels(1);
		fx.enable_denoise(true);
		fx.enable_dereverb(false);
		fx.enable_superres(false);
		fx.enable_aec(false);
		fx.intensity(1.0f);
		fx.load();

		double full = measure(fx, "Level 100% (initial)");

		fx.intensity(0.0f);
		std::this_thread::sleep_for(settle);
		double zero = measure(fx, "Level 0% (settled)");

		fx.intensity(1.0f);
		std::this_thread::sleep_for(settle);
		double back = measure(fx, "Level 100% again (settled)");

		fx.intensity(0.5f);
		std::this_thread::sleep_for(settle);
		double half = measure(fx, "Level 50% (settled)");

		check("Level 100% denoises", full < 10.0);
		check("Level 0% lets the noise through", zero > full * 100.0);
		check("Level 100% again denoises just like before", std::fabs(back - full) < 0.01);
		check("Level 50% lands between the two", half > full * 100.0 && half < zero * 0.75);
	}

	// --- A drag must not rebuild on every step ------------------------------
	{
		SAY("\n[Dragging the slider]\n");
		fx.intensity(1.0f);
		std::this_thread::sleep_for(settle);
		pump(fx, 10);

		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < 40; ++i) {
			fx.intensity(1.0f - i * 0.02f);
			pump(fx, 2);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		double drag_ms = elapsed_ms(t0);
		SAY("    40 changes + audio took %.0f ms (a rebuild each would be >3000 ms)\n", drag_ms);
		check("dragging does not rebuild on every step", drag_ms < 1500.0);

		std::this_thread::sleep_for(settle);
		double after = measure(fx, "after letting go at Level 22%");
		check("letting go applies the final value", after > 100.0);
	}

	// --- Super Resolution ignores Level, so it must not cost a rebuild ------
	{
		SAY("\n[Both + Super Resolution / chained]\n");
		fx.enable_denoise(true);
		fx.enable_dereverb(true);
		fx.enable_superres(true);
		fx.intensity(1.0f);
		fx.load();
		SAY("    rates: %u Hz in / %u Hz out, blocks %u / %u\n", fx.input_samplerate(),
		    fx.output_samplerate(), fx.input_blocksize(), fx.output_blocksize());

		pump(fx, 100); // warm up
		auto t0 = std::chrono::steady_clock::now();
		pump(fx, 100);
		double baseline_ms = elapsed_ms(t0);

		fx.intensity(0.0f);
		std::this_thread::sleep_for(settle);

		t0 = std::chrono::steady_clock::now();
		pump(fx, 100);
		double after_ms = elapsed_ms(t0);

		SAY("    100 blocks: %.1f ms normally, %.1f ms right after a Level change\n", baseline_ms, after_ms);
		check("Super Resolution does not rebuild for Level", after_ms < baseline_ms + 40.0);
	}

	// --- Echo Cancel has no intensity either --------------------------------
	{
		SAY("\n[Echo Cancel / AEC]\n");
		try {
			fx.enable_superres(false);
			fx.channels(2);
			fx.enable_aec(true);
			fx.intensity(1.0f);
			fx.load();

			unsigned           blk = fx.input_blocksize();
			std::vector<float> mic(blk), ref(blk), out(blk, 0.f);
			fill_input(mic, 0, blk, fx.input_samplerate());
			fill_input(ref, 1, blk, fx.input_samplerate());

			auto one_block = [&]() {
				const float* ins[2]  = {mic.data(), ref.data()};
				float*       outs[2] = {out.data(), out.data()};
				size_t       i = blk, o = 0;
				fx.process(ins, i, outs, o);
			};

			one_block();
			fx.intensity(0.25f);
			std::this_thread::sleep_for(settle);

			auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < 20; ++i) one_block();
			double ms = elapsed_ms(t0);
			SAY("    20 AEC blocks after a Level change took %.1f ms\n", ms);
			check("AEC does not rebuild for a Level change", ms < 200.0);
		} catch (std::exception const& ex) {
			SAY("    EXCEPTION: %s\n", ex.what());
			check("AEC survives a Level change", false);
		}
	}

	SAY("\n=== %s ===\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");

	fxp.release(); // leaked on purpose: see the note at the top about the DLL unload
	return failures == 0 ? 0 : 1;
}
