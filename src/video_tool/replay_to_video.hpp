#pragma once

#include <memory>
#include <string>
#include "common.hpp"

void replayToVideo(
    std::shared_ptr<Common> const& common,
    bool spectator,
    std::string const& fullPath,
    std::string const& replayVideoName);
