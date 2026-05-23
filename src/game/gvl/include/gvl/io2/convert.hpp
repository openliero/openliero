#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <climits>
#include <cstdint>

#include "gvl/support/debug.hpp"
#include "gvl/meta/as_unsigned.hpp"

namespace gvl
{

template<typename DerivedT>
struct basic_text_writer
{
	DerivedT& derived()
	{ return *static_cast<DerivedT*>(this); }

	DerivedT const& derived() const
	{ return *static_cast<DerivedT const*>(this); }
};

uint8_t const no_caps[] = "0123456789abcdefghijklmnopqrstuvwxyz";
uint8_t const caps[]    = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

template<uint32_t Base, typename Writer, typename T>
int uint_to_ascii_base(Writer& writer, T x, int min_digits = 1, bool uppercase = false)
{
	std::size_t const buf_size = sizeof(T) * CHAR_BIT;
	uint8_t digits[buf_size];
	uint8_t* e = digits + buf_size;
	uint8_t* p = e;

	uint8_t const* names = uppercase ? caps : no_caps;

	while(min_digits-- > 0 || x > 0)
	{
		int n = x % Base;
		*--p = names[n];
		x /= Base;
	}

	writer.put(p, e - p);
	return 0;
}

template<uint32_t Base, typename Writer, typename T>
void int_to_ascii_base(Writer& writer, T x, int min_digits = 1, bool uppercase = false)
{
	typedef typename as_unsigned<T>::type unsigned_t;

	if(x < 0)
	{
		writer.put('-');
		uint_to_ascii_base<Base>(writer, unsigned_t(-x), min_digits, uppercase);
	}
	else
	{
		uint_to_ascii_base<Base>(writer, unsigned_t(x), min_digits, uppercase);
	}
}

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, uint32_t x)
{
	D& self = self_.derived();
	uint_to_ascii_base<10>(self, x);
	return self;
}

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, int32_t x)
{
	D& self = self_.derived();
	int_to_ascii_base<10>(self, x);
	return self;
}

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, char const* str)
{
	D& self = self_.derived();
	self.put(reinterpret_cast<uint8_t const*>(str), std::strlen(str));
	return self;
}

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, char ch)
{
	D& self = self_.derived();
	self.put(static_cast<uint8_t>(ch));
	return self;
}

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, std::string const& str)
{
	D& self = self_.derived();
	self.put(reinterpret_cast<uint8_t const*>(str.data()), str.size());
	return self;
}

struct endl_tag_ {};
inline void endl(endl_tag_) {}

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, void (*)(endl_tag_))
{
	D& self = self_.derived();
	self.put('\n');
	self.flush();
	return self;
}

struct cell : basic_text_writer<cell>
{
	enum placement
	{
		left, center, right
	};

	cell()
	: text_placement(left)
	, width(-1)
	{
	}

	cell(placement text_placement_init)
	: text_placement(text_placement_init)
	, width(-1)
	{
	}

	cell(int width_init, placement text_placement_init)
	: text_placement(text_placement_init)
	, width(width_init)
	{
	}

	void put(uint8_t x)
	{ buffer.push_back(x); }

	void put(uint8_t const* p, std::size_t len)
	{
		for(std::size_t i = 0; i < len; ++i)
		{
			buffer.push_back(p[i]);
		}
	}

	cell& ref()
	{
		return *this;
	}

	std::vector<uint8_t> buffer;
	placement text_placement;
	int width;
};

template<typename D>
inline D& operator<<(basic_text_writer<D>& self_, cell& c)
{
	D& self = self_.derived();
	if(c.buffer.size() > c.width)
	{
		int allowed = std::max(int(c.buffer.size()) - 2, 0);
		self.put(&c.buffer[0], &c.buffer[0] + allowed);
		if(allowed != int(c.buffer.size()))
			self << "..";
	}
	return self;
}

}
