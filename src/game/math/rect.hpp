#pragma once

#include <algorithm>
#include <cmath>

// 2D vector / rectangle types. Replaces gvl::basic_vec / gvl::basic_rect.
// Kept in namespace `gvl` (and the legacy aliases `gvl::ivec2`, `gvl::rect`,
// `gvl::basic_vec`, `gvl::basic_rect` preserved) so the migration is a
// drop-in replacement — call sites do not need to change.

namespace gvl
{

template<typename T, int N>
struct basic_vec; // unused for N != 2

template<typename T>
struct basic_vec<T, 2>
{
	T x = T(0);
	T y = T(0);

	constexpr basic_vec() = default;
	constexpr basic_vec(T x_, T y_) : x(x_), y(y_) {}

	template<typename U>
	constexpr explicit basic_vec(basic_vec<U, 2> const& other)
		: x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

	constexpr void zero() { x = T(0); y = T(0); }

	constexpr basic_vec& operator+=(basic_vec const& r) { x += r.x; y += r.y; return *this; }
	constexpr basic_vec& operator-=(basic_vec const& r) { x -= r.x; y -= r.y; return *this; }
	constexpr basic_vec& operator*=(T s) { x *= s; y *= s; return *this; }
	constexpr basic_vec& operator/=(T s) { x /= s; y /= s; return *this; }

	friend constexpr basic_vec operator+(basic_vec a, basic_vec b) { return {a.x + b.x, a.y + b.y}; }
	friend constexpr basic_vec operator-(basic_vec a, basic_vec b) { return {a.x - b.x, a.y - b.y}; }
	friend constexpr basic_vec operator*(basic_vec a, T s) { return {a.x * s, a.y * s}; }
	friend constexpr basic_vec operator*(T s, basic_vec a) { return {a.x * s, a.y * s}; }
	friend constexpr basic_vec operator/(basic_vec a, T s) { return {a.x / s, a.y / s}; }
	friend constexpr basic_vec operator-(basic_vec a) { return {-a.x, -a.y}; }
	friend constexpr bool operator==(basic_vec a, basic_vec b) { return a.x == b.x && a.y == b.y; }
	friend constexpr bool operator!=(basic_vec a, basic_vec b) { return !(a == b); }
};

using ivec2 = basic_vec<int, 2>;
using fvec2 = basic_vec<float, 2>;

template<typename T>
struct basic_rect
{
	T x1 = T(0);
	T y1 = T(0);
	T x2 = T(0);
	T y2 = T(0);

	constexpr basic_rect() = default;
	constexpr basic_rect(T x1_, T y1_, T x2_, T y2_)
		: x1(x1_), y1(y1_), x2(x2_), y2(y2_) {}

	constexpr T width() const { return x2 - x1; }
	constexpr T height() const { return y2 - y1; }
	constexpr T center_x() const { return (x1 + x2) / T(2); }
	constexpr T center_y() const { return (y1 + y2) / T(2); }
	constexpr basic_vec<T, 2> center() const { return {center_x(), center_y()}; }
	constexpr basic_vec<T, 2> ul() const { return {x1, y1}; }

	constexpr bool inside(T vx, T vy) const {
		T dx = vx - x1, dy = vy - y1;
		return dx >= T(0) && dx < width() && dy >= T(0) && dy < height();
	}

	constexpr bool encloses(basic_vec<T, 2> v) const { return inside(v.x, v.y); }
	constexpr bool encloses(T vx, T vy) const { return inside(vx, vy); }

	constexpr bool valid() const { return x1 <= x2 && y1 <= y2; }

	bool intersect(basic_rect const& b) {
		x1 = std::max(x1, b.x1);
		y1 = std::max(y1, b.y1);
		x2 = std::min(x2, b.x2);
		y2 = std::min(y2, b.y2);
		return valid();
	}

	void join(basic_rect const& b) {
		x1 = std::min(x1, b.x1);
		y1 = std::min(y1, b.y1);
		x2 = std::max(x2, b.x2);
		y2 = std::max(y2, b.y2);
	}

	friend basic_rect operator|(basic_rect a, basic_rect const& b) { a.join(b); return a; }
	friend basic_rect operator&(basic_rect a, basic_rect const& b) { a.intersect(b); return a; }
};

using rect = basic_rect<int>;
using frect = basic_rect<float>;

}
