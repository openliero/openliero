#include <cassert>
#include <climits>
#include <cstdint>

#include "gvl/support/platform.hpp"

void gvl_test_platform()
{
	static_assert(sizeof(uint64_t)*CHAR_BIT == 64);
	static_assert(sizeof(uint32_t)*CHAR_BIT == 32);
	static_assert(sizeof(uint16_t)*CHAR_BIT == 16);
	static_assert(sizeof(uint8_t)*CHAR_BIT == 8);
	static_assert(sizeof(int64_t)*CHAR_BIT == 64);
	static_assert(sizeof(int32_t)*CHAR_BIT == 32);
	static_assert(sizeof(int16_t)*CHAR_BIT == 16);
	static_assert(sizeof(int8_t)*CHAR_BIT == 8);

	// Test endianness
	uint32_t v = 0xAABBCCDD;
	uint8_t first = reinterpret_cast<uint8_t*>(&v)[0];
	uint8_t second = reinterpret_cast<uint8_t*>(&v)[1];
#if GVL_BIG_ENDIAN
	assert(first == 0xAA && second == 0xBB);
#else
	assert(first == 0xDD && second == 0xCC);
#endif

	// Test integer assumptions
	static_assert((-1>>31) == -1); // Signed right-shift must duplicate sign bit
	static_assert((-1/2) == 0); // Division must round towards 0

	// Do this last since it may crash the process
#if GVL_UNALIGNED_ACCESS
	uint8_t volatile x[150] = {};

	for(int i = 0; i < 100; ++i)
		*(uint32_t volatile*)(x + i) = 1;

	uint32_t sum = 0;
	for(int i = 0; i < 100; ++i)
		sum += *(uint32_t volatile*)(x + i);
#endif
}
