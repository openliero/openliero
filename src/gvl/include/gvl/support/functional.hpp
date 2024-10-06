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
}
