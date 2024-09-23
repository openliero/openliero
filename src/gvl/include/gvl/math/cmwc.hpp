#pragma once

#include <cstddef>

#include "gvl/support/cstdint.hpp"
#include "gvl/math/random.hpp"

namespace gvl
{

// NOTE: This generator only generates
// numbers in [0, 2^32-1)
template<int R, uint32_t A>
struct cmwc // : prng_common<cmwc<R, A>, uint32_t>
{
	//using prng_common<cmwc<R, A>, uint32_t>::operator();

	template<typename Archive, int R2, uint32_t A2>
	friend void archive(Archive ar, cmwc<R2, A2>& x);

	cmwc()
	: p(R - 1)
	, c(0x1337)
	{

	}

	void seed(uint32_t v)
	{
		seed(&v, 1);
	}

	void seed(uint32_t const* v, std::size_t count)
	{
		// Seeding based on Mersenne twister's

		c = 0x1337;

		if(count > R)
			count = R;

		std::size_t i = 0;
		for(; i < count; ++i)
			Q[i] = v[i];

		for(; i < R; ++i)
		{
			uint32_t prev = Q[i - count];
			Q[i] = (1812433253UL * (prev ^ (prev >> 30)) + i);
		}
	}

	uint32_t operator()()
	{
		p = (p+1) & (R - 1);
		uint64_t t = uint64_t(Q[p])*A + c;

		c = uint32_t(t >> 32);
		uint64_t x = (t & 0xffffffff) + c;

		uint32_t overflow = uint32_t(x >> 32);

		c += overflow;

		return (Q[p] = 0xfffffffe - uint32_t(x & 0xffffffff) - overflow);
	}

private:
	uint32_t Q[R];
	uint32_t c;
	uint32_t p;
};

template<typename Archive, int R, uint32_t A>
void archive(Archive ar, cmwc<R, A>& x)
{
	ar.ui32(x.c);
	for(int i = 0; i < R; ++i)
		ar.ui32(x.Q[i]);
}

struct mwc : prng_common<mwc, uint32_t>
{
	using prng_common<mwc, uint32_t>::operator();

	mwc(uint32_t seed_new = 0x1337)
	{
		seed(seed_new);
	}

	uint32_t x, c;

	bool operator==(mwc const& b)
	{
		return x == b.x && c == b.c;
	}

	bool operator!=(mwc const& b)
	{
		return !operator==(b);
	}

	void seed(uint32_t seed_new)
	{
		x = seed_new;
		c = 9413207;
	}

	uint32_t operator()()
	{
		uint64_t t = uint64_t(2083801278)*x + c;
		c = uint32_t(t>>32);
		x = uint32_t(t&0xffffffff);
		return x;
	}
};

template<typename Archive>
void archive(Archive ar, mwc& x)
{
	ar.ui32(x.c);
	ar.ui32(x.x);
}

template<int A, int B, int C>
struct xorshift : prng_common<xorshift<A, B, C>, uint32_t>
{
	using prng_common<xorshift<A, B, C>, uint32_t>::operator();

	uint32_t x;

	xorshift(uint32_t seed)
	: x(seed)
	{

	}

	uint32_t operator()()
	{
		uint32_t v = x;

		v ^= v << A;
		v ^= v >> B;
		v ^= v << C;
		return (x = v);
	}
};

#define ROT32(x, n) (((x)<<(n))|((x)>>(32-(n))))

struct countergen
{
	uint32_t k, x;

	void skip(int n = 1)
	{
		x += n;
	}

	uint32_t operator()()
	{
		uint32_t a = 0x12345678, b = x, c = k, d = 0xabcdef0;

		a += b; d ^= a; d = ROT32(d, 16);
		c += d; b ^= c; b = ROT32(b, 12);
		a += b; d ^= a; d = ROT32(d, 8);
		c += d; b ^= c; b = ROT32(b, 7);

		skip();

		return b + d;
	}
};

#undef ROT32

typedef cmwc<4096, 18782> cmwc18782;
typedef cmwc<4, 987654978> cmwc987654978;
typedef xorshift<2, 9, 15> default_xorshift;

}
