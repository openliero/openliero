#pragma once

#include "controller.hpp"

struct CommonController : Controller {
  CommonController();
  bool process() override;

  int frameSkip;
  bool inverseFrameSkip;
  int cycles;
};
