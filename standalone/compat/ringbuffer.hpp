// Standalone shim for tonplugins::memory::float_ring_t.
//
// A simple growable FIFO of floats that guarantees peek()/poke() return a
// CONTIGUOUS region (it compacts and, if needed, grows). This matches how the
// VoiceFX processor uses it:
//   used()/free(), peek(n), poke(n), read(n, dst), write(n, src)
#pragma once
#include <cstring>
#include <vector>

namespace tonplugins::memory {
	class float_ring_t {
		std::vector<float> _buf;
		size_t             _r = 0; // read cursor
		size_t             _w = 0; // write cursor (_r <= _w <= _buf.size())

		void ensure_space(size_t n)
		{
			if (_buf.size() - _w >= n) {
				return; // already contiguous at the end
			}
			// Compact live data [_r,_w) to the front.
			size_t u = _w - _r;
			if (_r > 0) {
				if (u > 0) {
					std::memmove(_buf.data(), _buf.data() + _r, u * sizeof(float));
				}
				_r = 0;
				_w = u;
			}
			if (_buf.size() - _w < n) {
				_buf.resize(_w + n); // grow to fit
			}
		}

		public:
		explicit float_ring_t(size_t capacity) : _buf(capacity ? capacity : 1) {}

		size_t used() const { return _w - _r; }
		size_t free() const { return (_buf.size() >= (_w - _r)) ? (_buf.size() - (_w - _r)) : 0; }

		// Pointer to the next n readable samples (caller ensures n <= used()).
		float* peek(size_t /*n*/) { return _buf.data() + _r; }

		// Pointer to n writable samples, guaranteed contiguous.
		float* poke(size_t n)
		{
			ensure_space(n);
			return _buf.data() + _w;
		}

		// Consume n samples, optionally copying them to dst first.
		void read(size_t n, float* dst)
		{
			if (n > used()) {
				n = used();
			}
			if (dst && n > 0) {
				std::memcpy(dst, _buf.data() + _r, n * sizeof(float));
			}
			_r += n;
			if (_r == _w) {
				_r = _w = 0;
			}
		}

		// Append n samples; copy from src, or commit previously poke()'d data.
		void write(size_t n, const float* src)
		{
			ensure_space(n);
			if (src && n > 0) {
				std::memcpy(_buf.data() + _w, src, n * sizeof(float));
			}
			_w += n;
		}
	};
} // namespace tonplugins::memory
