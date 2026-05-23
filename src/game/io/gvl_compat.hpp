#pragma once

// Throwaway adapters from gvl streams to io::Reader / io::Writer, so
// consumers can migrate one at a time while filesystem.hpp still
// returns gvl::source / gvl::sink. Delete this header once everything
// upstream is migrated.

#include <gvl/io2/stream.hpp>

#include "io/stream.hpp"

namespace io {

struct GvlReaderAdapter : Reader {
	explicit GvlReaderAdapter(gvl::octet_reader r) : r_(std::move(r)) {}

	uint8_t get() override {
		// gvl::octet_reader::get() throws runtime_error on EOF; let it.
		return r_.get();
	}

	std::size_t try_get(uint8_t* dst, std::size_t n) override {
		return r_.try_get(dst, n);
	}

	std::size_t try_skip(std::size_t n) override {
		return r_.try_skip(n);
	}

private:
	gvl::octet_reader r_;
};

// Constructs its own gvl::octet_writer in-place from a gvl::sink, avoiding
// the pre-existing bug in gvl::octet_writer's move ctor (gvl::shared_ptr's
// move ctor is gated behind GVL_CPP0X, off on GCC, so moves fall back to
// copy and leave the source's sink_ non-null — its destructor then runs
// flush() on a null buffer_).
struct GvlWriterAdapter : Writer {
	explicit GvlWriterAdapter(gvl::sink s) : w_(s) {}

	void put(uint8_t b) override { w_.put(b); }
	void put(uint8_t const* src, std::size_t n) override { w_.put(src, n); }
	void flush() override { w_.flush(); }

private:
	gvl::octet_writer w_;
};

}  // namespace io
