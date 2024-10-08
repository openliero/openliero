#pragma once

#include "itemBehavior.hpp"

struct Common;
struct Menu;

struct IntegerBehavior : ItemBehavior
{
	IntegerBehavior(Common& common, int& v, int min, int max, int step = 1, bool percentage = false)
	: common(common), v(v)
	, min(min), max(max), step(step)
	, scrollInterval(5)
	, percentage(percentage)
	, allowEntry(true)
	{
	}

	bool onLeftRight(Menu& menu, MenuItem& item, int dir);
	int onEnter(Menu& menu, MenuItem& item);
	void onUpdate(Menu& menu, MenuItem& item);

	Common& common;
	int& v;
	int min, max, step;
	int scrollInterval;
	bool percentage;
	bool allowEntry;
};
