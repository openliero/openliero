#pragma once

namespace gvl
{
struct noncopyable
{
	noncopyable() {}
	~noncopyable() {}
	noncopyable(const noncopyable&) = delete;
	noncopyable& operator=(const noncopyable&) = delete;
};
}
