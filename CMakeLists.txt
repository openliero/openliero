cmake_minimum_required(VERSION 3.21)

project(openliero
  DESCRIPTION "A brutal subterranean shooter game"
  HOMEPAGE_URL https://github.com/openliero/openliero
  LANGUAGES C CXX
)

# options at top so they're easy to find
option(OPENLIERO_BUILD_TCTOOL "Build tctool" OFF)
option(OPENLIERO_BUILD_VIDEOTOOL "Build videotool" OFF)

# sane CMake defaults
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
endif()

if(NOT DEFINED CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 99)
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# find dependencies
find_package(SDL2 REQUIRED)
find_package(SDL2_Image REQUIRED)
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

if(APPLE)
  set(GAME_SOURCES ${GAME_SOURCES} src/game/SDLmain.m)

  # Populate RESOURCE_FILES with files to copy into the .app Resources/ directory.
  file(GLOB_RECURSE RESOURCE_FILES pkg/*.cfg pkg/TC/* pkg/*.icns)
else()
  set(GAME_SOURCES ${GAME_SOURCES} src/game/sdlmain.cpp)
endif()

# game
add_library(game ${GAME_SOURCES})
target_link_libraries(game SDL2::SDL2 SDL2_image::SDL2_image miniz)
if(APPLE)
  add_executable(openliero MACOSX_BUNDLE src/game/main.cpp)
elseif(WIN32)
  add_executable(openliero WIN32 src/game/sdlmain.cpp pkg/win32_icon.rc)
else()
  add_executable(openliero src/game/main.cpp)
endif()
target_link_libraries(openliero game)

if(OPENLIERO_BUILD_TCTOOL)
  set(TC_TOOL_SOURCES ${SOURCES}
    src/tc_tool/common_exereader.cpp
    src/tc_tool/common_writer.cpp
    src/tc_tool/tc_tool_main.cpp
  )

  add_library(tc ${TC_TOOL_SOURCES})
  target_link_libraries(tc SDL2::SDL2 miniz)
  add_executable(tctool ${TC_TOOL_SOURCES})
  target_link_libraries(tctool tc)
endif()

if(OPENLIERO_BUILD_VIDEOTOOL)
  set(VIDEO_TOOL_SOURCES ${SOURCES}
    src/game/replay.cpp
    src/video_tool/replay_to_video.cpp
    src/video_tool/tools_main.cpp
    src/video_tool/video_recorder.c
  )
  add_library(vt ${VIDEO_TOOL_SOURCES})
  target_link_libraries(vt SDL2::SDL2 avcodec avformat avutil dl pthread swresample swscale x264 miniz)
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
  target_link_libraries(videotool vt)
endif()

if(APPLE)
  set_target_properties(openliero PROPERTIES MACOSX_BUNDLE TRUE)
  set_target_properties(openliero PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
  set(MACOSX_BUNDLE_ICON_FILE liero)

  # Preserve directory structure when copying contents of pkg/ to the .app Resources/ directory.
  foreach(absolute_path ${RESOURCE_FILES})
    file(RELATIVE_PATH relative_path "${CMAKE_CURRENT_SOURCE_DIR}/pkg" ${absolute_path})
    get_filename_component(relative_path ${relative_path} DIRECTORY)
    set_source_files_properties(${absolute_path}
      PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${relative_path}")
  endforeach(absolute_path)
endif()
