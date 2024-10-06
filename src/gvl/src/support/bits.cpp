#include "gvl/support/bits.hpp"

#if !defined(_MSC_VER)

/* Taken mostly from http://graphics.stanford.edu/~seander/bithacks.html */
int gvl_bottom_bit(uint32_t v)
{
	if(!v)
		return -1;

	static const int MultiplyDeBruijnBitPosition[32] =
	{
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};

	return MultiplyDeBruijnBitPosition[((uint32_t)((v & -v) * 0x077CB531UL)) >> 27];
}
#endif
