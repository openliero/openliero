// Phase 0 lock-in tests for the homegrown TOML adapter
// (src/game/serialization/toml_adapter.hpp).
//
// These tests freeze the current behavior so the Phase 1–3 migration
// to a custom cereal TOML archive can be verified incrementally. They
// exercise the primitive read/write methods and the structural methods
// (obj, arr, array_obj, ref, null) of `ser::toml::writer`/`reader`.
// They will be deleted in Phase 4 when the format changes intentionally.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "io/stream.hpp"
#include "serialization/toml_adapter.hpp"

namespace {

using Writer = ser::toml::writer<io::Writer>;
using Reader = ser::toml::reader<io::Reader>;

// Helper: writer -> std::string (the writer flushes on destruction).
std::string serialize(std::function<void(Writer&)> body) {
	std::string buf;
	io::StringWriter sw(buf);
	{
		Writer ar(sw);
		body(ar);
	}
	return buf;
}

// ----- Primitive round-trip via the top-level table -----

TEST_CASE("ser::toml primitive round-trip", "[toml]") {
	std::string buf = serialize([](Writer& ar) {
		int32_t i = -42;
		uint32_t u = 0xCAFEBABEu;
		bool b = true;
		std::string s = "hello world";
		ar.i32("i", i);
		ar.u32("u", u);
		ar.b("b", b);
		ar.str("s", s);
	});

	io::MemReader r(buf);
	Reader ar(r);
	int32_t i = 0;
	uint32_t u = 0;
	bool b = false;
	std::string s;
	ar.i32("i", i);
	ar.u32("u", u);
	ar.b("b", b);
	ar.str("s", s);

	CHECK(i == -42);
	CHECK(u == 0xCAFEBABEu);
	CHECK(b == true);
	CHECK(s == "hello world");
}

TEST_CASE("ser::toml missing key keeps default", "[toml]") {
	std::string const buf = "present = 5\n";
	io::MemReader r(buf);
	Reader ar(r);
	int32_t present = 0;
	int32_t absent = 999;  // should not change
	ar.i32("present", present);
	ar.i32("absent", absent);
	CHECK(present == 5);
	CHECK(absent == 999);
}

TEST_CASE("ser::toml wrong-type key keeps default", "[toml]") {
	std::string const buf = "x = \"not-a-number\"\n";
	io::MemReader r(buf);
	Reader ar(r);
	int32_t x = 7;
	ar.i32("x", x);
	CHECK(x == 7);
}

// ----- Sub-tables via obj() -----

TEST_CASE("ser::toml obj() nests under a named table", "[toml]") {
	std::string buf = serialize([](Writer& ar) {
		int32_t a = 1;
		int32_t b = 2;
		ar.obj("inner", [&]() {
			ar.i32("a", a);
			ar.i32("b", b);
		});
	});

	io::MemReader r(buf);
	Reader ar(r);
	int32_t a = 0;
	int32_t b = 0;
	ar.obj("inner", [&]() {
		ar.i32("a", a);
		ar.i32("b", b);
	});
	CHECK(a == 1);
	CHECK(b == 2);
}

// ----- Array of primitives via arr() -----

TEST_CASE("ser::toml arr() of u32", "[toml]") {
	std::vector<uint32_t> in{1, 2, 3, 4, 5};
	std::string buf = serialize([&](Writer& ar) {
		ar.arr("xs", in, [&](uint32_t& v) { ar.u32(v); });
	});

	io::MemReader r(buf);
	Reader ar(r);
	std::vector<uint32_t> out;
	ar.arr("xs", out, [&](uint32_t& v) { ar.u32(nullptr, v); });
	CHECK(out == in);
}

// ----- Array of objects via array_obj() -----

struct Pair {
	int32_t k = 0;
	int32_t v = 0;
};

TEST_CASE("ser::toml array_obj() round-trip", "[toml]") {
	std::vector<Pair> in{{1, 10}, {2, 20}, {3, 30}};
	std::string buf = serialize([&](Writer& ar) {
		ar.array_obj("pairs", in, [&](Pair& p) {
			ar.i32("k", p.k);
			ar.i32("v", p.v);
		});
	});

	io::MemReader r(buf);
	Reader ar(r);
	std::vector<Pair> out;
	ar.array_obj("pairs", out, [&](Pair& p) {
		ar.i32("k", p.k);
		ar.i32("v", p.v);
	});
	REQUIRE(out.size() == in.size());
	for (size_t i = 0; i < in.size(); ++i) {
		CHECK(out[i].k == in[i].k);
		CHECK(out[i].v == in[i].v);
	}
}

// ----- null omission via ref() -----
//
// The `null()` method is used by ref resolvers when the referent is
// absent. The writer must omit the key entirely (TOML has no null).
// The reader must leave the default in place when the key is missing.

struct StringResolver {
	template <typename W>
	void v2r(W& ar, std::string const& v) const {
		if (v.empty()) {
			ar.null(nullptr);
		} else {
			std::string copy = v;
			ar.str(nullptr, copy);
		}
	}

	template <typename T>
	void r2v(T& v) const {
		v = "DEFAULT";
	}

	template <typename T>
	void r2v(T& v, std::string const& s) const {
		v = s;
	}
};

TEST_CASE("ser::toml ref() omits key when null", "[toml]") {
	std::string buf = serialize([](Writer& ar) {
		std::string empty;
		std::string filled = "yes";
		ar.ref("empty", empty, StringResolver{});
		ar.ref("filled", filled, StringResolver{});
	});

	CHECK(buf.find("empty") == std::string::npos);
	CHECK(buf.find("filled") != std::string::npos);

	io::MemReader r(buf);
	Reader ar(r);
	std::string a = "preserve";
	std::string b = "ignored";
	ar.ref("empty", a, StringResolver{});
	ar.ref("filled", b, StringResolver{});
	CHECK(a == "DEFAULT");  // resolver's r2v(v) called when absent
	CHECK(b == "yes");
}

}  // namespace
