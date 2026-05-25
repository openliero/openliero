#pragma once

// Cereal serialize() glue for openliero gameplay types.
//
// Each type's existing header stays free of cereal so include-graph cost
// is paid only by code that actually serializes. Include this header from
// replay.cpp / settings.cpp and from cereal-specific test files.
//
// Convention: non-member serialize() templated on Archive so the same
// function works for cereal::PortableBinary{In,Out}putArchive and
// ser::Toml{In,Out}putArchive.

#include <cereal/cereal.hpp>

#include "math/rect.hpp"

template <class Archive, typename T>
void serialize(Archive& ar, BasicVec<T, 2>& v) {
  ar(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y));
}

template <class Archive, typename T>
void serialize(Archive& ar, BasicRect<T>& r) {
  ar(cereal::make_nvp("x1", r.x1), cereal::make_nvp("y1", r.y1),
     cereal::make_nvp("x2", r.x2), cereal::make_nvp("y2", r.y2));
}
