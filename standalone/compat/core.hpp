// Standalone shim for the TonPlugIns "core" logging facility.
// Provides just enough of tonplugins::core for VoiceFX's D_LOG macros.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef __FUNCTION_SIG__
#ifdef _MSC_VER
#define __FUNCTION_SIG__ __FUNCSIG__
#else
#define __FUNCTION_SIG__ __PRETTY_FUNCTION__
#endif
#endif

namespace tonplugins {
	class core {
		std::string _name;
		std::mutex  _mtx;
		explicit core(std::string name) : _name(std::move(name)) {}

		public:
		// Singleton; the name is set on first call.
		static std::shared_ptr<core> instance(std::string name)
		{
			static std::shared_ptr<core> inst(new core(std::move(name)));
			return inst;
		}

		void log(const char* fmt, ...)
		{
			char    buf[2048];
			va_list ap;
			va_start(ap, fmt);
			std::vsnprintf(buf, sizeof(buf), fmt, ap);
			va_end(ap);

			std::lock_guard<std::mutex> lk(_mtx);
#ifdef _WIN32
			std::string line = "[" + _name + "] " + buf + "\n";
			OutputDebugStringA(line.c_str());
#endif
			std::fprintf(stderr, "[%s] %s\n", _name.c_str(), buf);
		}
	};
} // namespace tonplugins
