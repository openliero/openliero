#pragma once

#include <cstdint>

// Big-endian and little-endian integer helpers on any type with a
// uint8_t get() / put(uint8_t) interface (io::Reader, io::Writer, ReaderFile).

namespace io {

template<typename Reader>
inline uint16_t read_uint16(Reader& r) {
	uint8_t hi = r.get();
	uint8_t lo = r.get();
	return static_cast<uint16_t>((hi << 8) | lo);
}

template<typename Reader>
inline uint16_t read_uint16_le(Reader& r) {
	uint8_t lo = r.get();
	uint8_t hi = r.get();
	return static_cast<uint16_t>((hi << 8) | lo);
}

template<typename Reader>
inline uint32_t read_uint32(Reader& r) {
	uint32_t b0 = r.get();
	uint32_t b1 = r.get();
	uint32_t b2 = r.get();
	uint32_t b3 = r.get();
	return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

template<typename Reader>
inline uint32_t read_uint32_le(Reader& r) {
	uint32_t b0 = r.get();
	uint32_t b1 = r.get();
	uint32_t b2 = r.get();
	uint32_t b3 = r.get();
	return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

template<typename Writer>
inline void write_uint16(Writer& w, uint16_t v) {
	w.put(static_cast<uint8_t>((v >> 8) & 0xff));
	w.put(static_cast<uint8_t>(v & 0xff));
}

template<typename Writer>
inline void write_uint16_le(Writer& w, uint16_t v) {
	w.put(static_cast<uint8_t>(v & 0xff));
	w.put(static_cast<uint8_t>((v >> 8) & 0xff));
}

template<typename Writer>
inline void write_uint32(Writer& w, uint32_t v) {
	w.put(static_cast<uint8_t>((v >> 24) & 0xff));
	w.put(static_cast<uint8_t>((v >> 16) & 0xff));
	w.put(static_cast<uint8_t>((v >> 8) & 0xff));
	w.put(static_cast<uint8_t>(v & 0xff));
}

template<typename Writer>
inline void write_uint32_le(Writer& w, uint32_t v) {
	w.put(static_cast<uint8_t>(v & 0xff));
	w.put(static_cast<uint8_t>((v >> 8) & 0xff));
	w.put(static_cast<uint8_t>((v >> 16) & 0xff));
	w.put(static_cast<uint8_t>((v >> 24) & 0xff));
}

inline int32_t uint32_as_int32(uint32_t v) {
	return static_cast<int32_t>(v);
}

}  // namespace io
