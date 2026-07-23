// Standalone stub: no custom CUDA context (the effect uses the default GPU).
// Provides just the types VoiceFX's effect code names.
#pragma once
#include <memory>

namespace nvidia::cuda {
	class context_stack {};

	class context {
		public:
		std::shared_ptr<context_stack> enter() { return {}; }
	};
} // namespace nvidia::cuda
