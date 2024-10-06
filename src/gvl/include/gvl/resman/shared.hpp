#pragma once

#include <algorithm>

namespace gvl
{

struct weak_ptr_common;

struct shared
{
	friend struct weak_ptr_common;

	shared()
	: _ref_count(1), _first(0)
	{

	}

	// const to allow shared_ptr<T const>
	void add_ref() const
	{
		++_ref_count;
	}

	// const to allow shared_ptr<T const>
	void release() const
	{
		--_ref_count;
		if(_ref_count == 0)
		{
			_clear_weak_ptrs();
			_delete();
		}
	}

	void swap(shared& b)
	{
		std::swap(_ref_count, b._ref_count);
		std::swap(_first, b._first);
	}

	int ref_count() const
	{ return _ref_count; }

	virtual ~shared()
	{
	}

private:
	void _delete() const
	{
		delete this;
	}

	void _clear_weak_ptrs() const
	{
	}

	mutable int _ref_count; // You should be able to have shared_ptr<T const>
	weak_ptr_common* _first;
};

}
