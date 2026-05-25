// Phase 1 smoke test: proves cereal is wired in and PortableBinaryArchive
// round-trips an int and a std::string. Once Phase 4 starts converting
// real types this test becomes redundant, but keep it as a build canary
// for the cereal dependency.

#include <catch2/catch_test_macros.hpp>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>

#include <cstdint>
#include <sstream>
#include <string>

TEST_CASE("cereal PortableBinaryArchive round-trips int and string", "[cereal]") {
	std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
	{
		cereal::PortableBinaryOutputArchive out(ss);
		int32_t i = -12345;
		std::string s = "hello cereal";
		out(i, s);
	}
	{
		cereal::PortableBinaryInputArchive in(ss);
		int32_t i = 0;
		std::string s;
		in(i, s);
		CHECK(i == -12345);
		CHECK(s == "hello cereal");
	}
}
