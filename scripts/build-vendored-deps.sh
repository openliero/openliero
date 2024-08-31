#!/usr/bin/env sh

# build SDL2
cmake -S libs/SDL -B tmp/SDL -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" -D BUILD_SHARED_LIBS=OFF -D CMAKE_BUILD_TYPE=Release -D CMAKE_POSITION_INDEPENDENT_CODE=ON -D SDL_TEST=OFF
cmake --build tmp/SDL --parallel 4
DESTDIR=$(pwd)/tmp cmake --install tmp/SDL

# build SDL2_image

cmake -S libs/SDL_image -B tmp/SDL_image -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" -D CMAKE_BUILD_TYPE= -D SDL2IMAGE_VENDORED=ON -D BUILD_SHARED_LIBS=OFF -D CMAKE_POSITION_INDEPENDENT_CODE=ON -D CMAKE_PREFIX_PATH=$(pwd)/tmp/usr/local
cmake --build tmp/SDL_image --parallel 4
DESTDIR=$(pwd)/tmp cmake --install tmp/SDL_image

# example of building openliero with above dependencies
cmake -S . -B build -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" -D CMAKE_PREFIX_PATH=$(pwd)/tmp/usr/local -D OPENLIERO_VENDORED_DEPS=ON
cmake --build build --parallel 4
