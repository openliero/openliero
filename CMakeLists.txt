cmake_minimum_required(VERSION 3.21)

project(openliero
  DESCRIPTION "A brutal subterranean shooter game"
  HOMEPAGE_URL https://github.com/openliero/openliero
  LANGUAGES C CXX
)

# options at top so they're easy to find
option(OPENLIERO_BUILD_TCTOOL "Build tctool" OFF)
option(OPENLIERO_BUILD_VIDEOTOOL "Build videotool" OFF)
option(OPENLIERO_VENDORED_DEPS "Use vendored dependencies" OFF)

# sane CMake defaults
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)
set(CMAKE_BUILD_TYPE Release)

# find dependencies
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
add_subdirectory(libs/miniz)

# TODO get rid of this kludgy hack
include_directories(
  src
  src/game
)

set(SOURCES
  src/game/bobject.cpp
  src/game/bonus.cpp
  src/game/common.cpp
  src/game/console.cpp
  src/game/constants.cpp
  src/game/filesystem.cpp
  src/game/game.cpp
  src/game/level.cpp
  src/game/math.cpp
  src/game/ninjarope.cpp
  src/game/nobject.cpp
  src/game/rand.cpp
  src/game/settings.cpp
  src/game/sobject.cpp
  src/game/spectatorviewport.cpp
  src/game/stats_recorder.cpp
  src/game/text.cpp
  src/game/viewport.cpp
  src/game/weapon.cpp
  src/game/worm.cpp
  src/game/ai/dijkstra.cpp
  src/game/ai/predictive_ai.cpp
  src/game/gfx/blit.cpp
  src/game/gfx/font.cpp
  src/game/gfx/palette.cpp
  src/game/gfx/renderer.cpp
  src/game/gfx/sprite.cpp
  src/game/mixer/mixer.cpp
  src/game/mixer/player.cpp
  src/gvl/system/system.c
  src/gvl/support/bits.c
  src/gvl/support/debug.cpp
  src/gvl/support/platform.cpp
  src/gvl/support/profile.cpp
  src/gvl/io2/stream.cpp
  src/gvl/io2/convert.cpp
  src/gvl/containers/list.cpp
)

set(GAME_SOURCES ${SOURCES}
  src/game/gfx.cpp
  src/game/keys.cpp
  src/game/main.cpp
  src/game/replay.cpp
  src/game/sfx.cpp
  src/game/weapsel.cpp
  src/game/controller/commonController.cpp
  src/game/controller/localController.cpp
  src/game/controller/replayController.cpp
  src/game/controller/stats_presenter.cpp
  src/game/menu/booleanSwitchBehavior.cpp
  src/game/menu/enumBehavior.cpp
  src/game/menu/hiddenMenu.cpp
  src/game/menu/integerBehavior.cpp
  src/game/menu/itemBehavior.cpp
  src/game/menu/mainMenu.cpp
  src/game/menu/menu.cpp
  src/game/menu/menuItem.cpp
  src/game/menu/timeBehavior.cpp
)

# game
add_library(game ${GAME_SOURCES})
if(OPENLIERO_VENDORED_DEPS)
  target_link_libraries(game
    SDL2::SDL2-static
    SDL2_image::SDL2_image-static
  )
else()
  target_link_libraries(game
    SDL2::SDL2
    SDL2_image::SDL2_image
  )
endif()

target_link_libraries(game
  miniz
)

if(APPLE)
  add_executable(openliero src/game/sdlmain.cpp)
elseif(WIN32)
  add_executable(openliero WIN32 src/game/sdlmain.cpp pkg/win32_icon.rc)
else()
  add_executable(openliero src/game/main.cpp)
endif()

target_link_libraries(openliero PRIVATE game)

if(OPENLIERO_BUILD_TCTOOL)
  set(TC_TOOL_SOURCES ${SOURCES}
    src/tc_tool/common_exereader.cpp
    src/tc_tool/common_writer.cpp
    src/tc_tool/tc_tool_main.cpp
  )

  add_library(tc ${TC_TOOL_SOURCES})

  if(OPENLIERO_VENDORED_DEPS)
    target_link_libraries(tc
      SDL2::SDL2-static
    )
  else()
    target_link_libraries(tc
      SDL2::SDL2
    )
  endif()

  target_link_libraries(tc
    miniz
  )

  add_executable(tctool ${TC_TOOL_SOURCES})
  target_link_libraries(tctool PRIVATE tc)
endif()

if(OPENLIERO_BUILD_VIDEOTOOL)
  set(VIDEO_TOOL_SOURCES ${SOURCES}
    src/game/replay.cpp
    src/video_tool/replay_to_video.cpp
    src/video_tool/tools_main.cpp
    src/video_tool/video_recorder.c
  )
  add_library(vt ${VIDEO_TOOL_SOURCES})

  if(OPENLIERO_VENDORED_DEPS)
  target_link_libraries(vt
    SDL2::SDL2-static
  )
  else()
  target_link_libraries(vt
    SDL2::SDL2
  )
  endif()

  target_link_libraries(vt
    miniz
    avcodec
    avformat
    avutil
    dl
    pthread
    swresample
    swscale
    x264
  )
  target_include_directories(vt ffmpeg)
  target_link_directories(vt
    ffmpeg/libavcodec
    ffmpeg/libavformat
    ffmpeg/libavutil
    ffmpeg/libswresample
    ffmpeg/libswscale
    x264
  )
  add_executable(videotool ${VIDEO_TOOL_SOURCES})
  target_link_libraries(videotool PRIVATE vt)
endif()
