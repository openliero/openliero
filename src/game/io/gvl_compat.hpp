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

struct GvlWriterAdapter : Writer {
	explicit GvlWriterAdapter(gvl::octet_writer w) : w_(std::move(w)) {}

	void put(uint8_t b) override { w_.put(b); }
	void put(uint8_t const* src, std::size_t n) override { w_.put(src, n); }
	void flush() override { w_.flush(); }

private:
	gvl::octet_writer w_;
};

}  // namespace io
