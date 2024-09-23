#pragma once

namespace gvl
{

struct default_delete
{
	template<typename T>
	void operator()(T* p) const
	{
		delete p;
	}
};

struct dummy_delete
{
	template<typename T>
	void operator()(T const&) const
	{
		// Do nothing
	}
};

struct default_compare
{
	template<typename T>
	int operator()(T const& a, T const& b) const
	{
		if(a < b)
			return -1;
		else if(b < a)
			return 1;
		else
			return 0;
	}
};
}
