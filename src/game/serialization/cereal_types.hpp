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
#include "worm.hpp"

template <class Archive, typename T>
void serialize(Archive& ar, BasicVec<T, 2>& v) {
  ar(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y));
}

template <class Archive, typename T>
void serialize(Archive& ar, BasicRect<T>& r) {
  ar(cereal::make_nvp("x1", r.x1), cereal::make_nvp("y1", r.y1),
     cereal::make_nvp("x2", r.x2), cereal::make_nvp("y2", r.y2));
}

// ---- Worm::ControlState ----
// Single bitfield; serialized through the underlying uint32_t.
template <class Archive>
void serialize(Archive& ar, Worm::ControlState& cs) {
  ar(cereal::make_nvp("istate", cs.istate));
}

// ---- Ninjarope ----
// `anchor` (Worm*) is intentionally NOT serialized here: pointer dedup and
// worm-graph reconstruction belong to the enclosing Worm/Game scope, which
// will serialize the anchor by index (mirroring lastKilledByIdx from
// Phase 3). Everything else is plain data.
template <class Archive>
void serialize(Archive& ar, Ninjarope& n) {
  ar(cereal::make_nvp("out", n.out),
     cereal::make_nvp("attached", n.attached),
     cereal::make_nvp("pos", n.pos),
     cereal::make_nvp("vel", n.vel),
     cereal::make_nvp("length", n.length),
     cereal::make_nvp("curLen", n.curLen));
}

// ---- WormWeapon ----
// `type` (Weapon const*) is intentionally NOT serialized here: it's looked
// up by index in Common::weapons at the enclosing scope (same pattern as
// the existing replay archive). Plain numeric fields only.
template <class Archive>
void serialize(Archive& ar, WormWeapon& w) {
  ar(cereal::make_nvp("ammo", w.ammo),
     cereal::make_nvp("delayLeft", w.delayLeft),
     cereal::make_nvp("loadingLeft", w.loadingLeft));
}
