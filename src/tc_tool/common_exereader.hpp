#pragma once

#include "common.hpp"

struct ReaderFile;

void loadFromExe(
    Common& common,
    ReaderFile& exe,
    ReaderFile& gfx,
    ReaderFile& snd);
void loadSfx(std::vector<sfx_sound*>& sounds, ReaderFile& snd);
