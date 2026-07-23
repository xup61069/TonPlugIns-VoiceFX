#include "lib.hpp"

namespace voicefx {
	std::shared_ptr<tonplugins::core> core;

	void initialize()
	{
		static bool initialized = false;
		if (initialized) {
			return;
		}
		core        = tonplugins::core::instance(std::string{product_name});
		initialized = true;
	}
} // namespace voicefx

namespace {
	// Make sure the logger exists before the plugin factory creates any objects.
	struct standalone_init {
		standalone_init() { voicefx::initialize(); }
	} _standalone_init;
} // namespace
