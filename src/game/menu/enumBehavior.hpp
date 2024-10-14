#pragma once

#include "itemBehavior.hpp"

#include <cstdint>

struct Common;
struct Menu;

struct EnumBehavior : ItemBehavior {
  EnumBehavior(
      Common& common,
      uint32_t& v,
      uint32_t min,
      uint32_t max,
      bool brokenLeftRight = false)
      : common(common),
        v(v),
        min(min),
        max(max),
        brokenLeftRight(brokenLeftRight) {}

  bool onLeftRight(Menu& menu, MenuItem& item, int dir) override;
  int onEnter(Menu& menu, MenuItem& item) override;
  void onUpdate(Menu& menu, MenuItem& item) override;

  void change(Menu& menu, const MenuItem& item, int dir);

  Common& common;
  uint32_t& v;
  uint32_t min, max;
  bool brokenLeftRight;
};
