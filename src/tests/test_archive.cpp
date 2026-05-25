// Phase 0 lock-in tests for the homegrown binary archive
// (src/game/serialization/archive.hpp).
//
// These tests freeze the current behavior so the Phase 1–3 migration to
// cereal can be verified incrementally. They cover the primitive
// read/write API and the pointer-dedup mechanism of
// `ser::serialization_context`. They will be deleted in Phase 4 once
// the homegrown archive is gone.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "io/stream.hpp"
#include "serialization/archive.hpp"
#include "serialization/context.hpp"

namespace {

using InAr = ser::in_archive<io::MemReader, ser::default_serialization_context>;
using OutAr = ser::out_archive<io::VectorWriter, ser::default_serialization_context>;

// ----- Primitive round-trip -----

TEST_CASE("ser::archive primitives round-trip", "[archive]") {
	std::vector<uint8_t> buf;
	{
		ser::default_serialization_context ctx;
		io::VectorWriter w(buf);
		OutAr ar(w, ctx);
		ar.i32(int32_t(-12345))
		  .i32_le(int32_t(0x01020304))
		  .ui16(uint32_t(0xBEEF))
		  .ui16_le(uint32_t(0x1234))
		  .ui32(uint32_t(0xDEADBEEF))
		  .ui32_le(uint32_t(0xCAFEBABE))
		  .ui8(uint32_t(0x42))
		  .b(true)
		  .b(false)
		  .str(std::string("hello"))
		  .check();
	}

	ser::default_serialization_context ctx;
	io::MemReader r(buf);
	InAr ar(r, ctx);

	int32_t i32v = 0;
	int32_t i32lev = 0;
	uint32_t u16v = 0;
	uint32_t u16lev = 0;
	uint32_t u32v = 0;
	uint32_t u32lev = 0;
	uint32_t u8v = 0;
	bool b1 = false;
	bool b2 = true;
	std::string s;
	ar.i32(i32v)
	  .i32_le(i32lev)
	  .ui16(u16v)
	  .ui16_le(u16lev)
	  .ui32(u32v)
	  .ui32_le(u32lev)
	  .ui8(u8v)
	  .b(b1)
	  .b(b2)
	  .str(s)
	  .check();

	CHECK(i32v == -12345);
	CHECK(i32lev == 0x01020304);
	CHECK(u16v == 0xBEEFu);
	CHECK(u16lev == 0x1234u);
	CHECK(u32v == 0xDEADBEEFu);
	CHECK(u32lev == 0xCAFEBABEu);
	CHECK(u8v == 0x42u);
	CHECK(b1 == true);
	CHECK(b2 == false);
	CHECK(s == "hello");
}

// ----- Byte-compare fixture -----
//
// Freezes the exact on-wire layout for a representative primitive
// sequence. If this fixture breaks before Phase 4, something in the
// homegrown archive shifted unexpectedly. In Phase 4 it gets deleted
// alongside the homegrown archive.

TEST_CASE("ser::archive byte-compare fixture", "[archive]") {
	std::vector<uint8_t> buf;
	{
		ser::default_serialization_context ctx;
		io::VectorWriter w(buf);
		OutAr ar(w, ctx);
		ar.i32(int32_t(1))           // BE: 00 00 00 01
		  .i32_le(int32_t(2))        // LE: 02 00 00 00
		  .ui16(uint32_t(0x0304))    // BE: 03 04
		  .ui16_le(uint32_t(0x0506)) // LE: 06 05
		  .ui8(uint32_t(0xFF))       // FF
		  .b(true)                   // 01
		  .b(false)                  // 00
		  .str(std::string("AB"))    // len BE u32: 00 00 00 02, then 41 42
		  .check();                  // 12 34 56 78
	}
	std::vector<uint8_t> const expected{
		0x00, 0x00, 0x00, 0x01,
		0x02, 0x00, 0x00, 0x00,
		0x03, 0x04,
		0x06, 0x05,
		0xFF,
		0x01,
		0x00,
		0x00, 0x00, 0x00, 0x02, 0x41, 0x42,
		0x12, 0x34, 0x56, 0x78,
	};
	CHECK(buf == expected);
}

TEST_CASE("ser::archive check mismatch throws", "[archive]") {
	std::vector<uint8_t> buf{0xDE, 0xAD, 0xBE, 0xEF};
	ser::default_serialization_context ctx;
	io::MemReader r(buf);
	InAr ar(r, ctx);
	CHECK_THROWS_AS(ar.check(), ser::archive_check_error);
}

TEST_CASE("ser::archive pascal_str truncates and pads", "[archive]") {
	std::vector<uint8_t> buf;
	{
		ser::default_serialization_context ctx;
		io::VectorWriter w(buf);
		OutAr ar(w, ctx);
		ar.pascal_str(std::string("Hi"), 8);
	}
	REQUIRE(buf.size() == 8);
	CHECK(buf[0] == 2);
	CHECK(buf[1] == 'H');
	CHECK(buf[2] == 'i');
	for (int i = 3; i < 8; ++i)
		CHECK(buf[i] == 0);

	ser::default_serialization_context ctx;
	io::MemReader r(buf);
	InAr ar(r, ctx);
	std::string out;
	ar.pascal_str(out, 8);
	CHECK(out == "Hi");
}

// ----- Pointer dedup via serialization_context -----
//
// Two pointers to the same Node should serialize once (the first write
// emits the body; the second emits only the id). On the way back in,
// both references resolve to the same in-memory object.

struct Node {
	int32_t v = 0;
};

template <typename Archive>
void archive(Archive ar, Node& n) {
	ar.i32(n.v);
}

TEST_CASE("ser::archive obj() dedups repeated pointers", "[archive]") {
	std::vector<uint8_t> buf;
	{
		Node a;
		a.v = 11;
		Node* pa = &a;
		Node* pa_again = &a;
		ser::default_serialization_context ctx;
		io::VectorWriter w(buf);
		OutAr ar(w, ctx);
		ar.obj(pa).obj(pa_again);
	}

	// Wire layout: id(=1) + body i32(11) + id(=1 again, no body)
	// 4 bytes id + 4 bytes payload + 4 bytes id = 12 bytes.
	CHECK(buf.size() == 12);

	{
		Node* out1 = nullptr;
		Node* out2 = nullptr;
		ser::default_serialization_context ctx;
		io::MemReader r(buf);
		InAr ar(r, ctx);
		ar.obj(out1).obj(out2);

		REQUIRE(out1 != nullptr);
		CHECK(out1 == out2);
		CHECK(out1->v == 11);
		delete out1;
	}
}

TEST_CASE("ser::archive obj() handles null", "[archive]") {
	std::vector<uint8_t> buf;
	{
		Node* p = nullptr;
		ser::default_serialization_context ctx;
		io::VectorWriter w(buf);
		OutAr ar(w, ctx);
		ar.obj(p);
	}
	// Null gets id=0, no body.
	REQUIRE(buf.size() == 4);
	CHECK(buf[0] == 0);
	CHECK(buf[3] == 0);

	Node sentinel;
	Node* out = &sentinel;
	ser::default_serialization_context ctx;
	io::MemReader r(buf);
	InAr ar(r, ctx);
	ar.obj(out);
	CHECK(out == nullptr);
}

}  // namespace
