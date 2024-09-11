cmake_minimum_required(VERSION 3.21)

# sane CMake defaults
# these variables get set if they are not already defined
set(CMAKE_C_STANDARD 99 CACHE STRING "The C standard to use" FORCE)
set(CMAKE_CXX_STANDARD 11 CACHE STRING "The C++ standard to use" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "The type of build to use" FORCE)

# tell cmake we have goodies in the cmake/ directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GenerateVersion)

project(openliero
  DESCRIPTION "A brutal subterranean shooter game"
  HOMEPAGE_URL https://github.com/openliero/openliero
  LANGUAGES C CXX
  VERSION "${VERSION_GIT_TAG}"
)

# options at top so they're easy to find
option(OPENLIERO_BUILD_TCTOOL "Build tctool" OFF)
option(OPENLIERO_BUILD_VIDEOTOOL "Build videotool" OFF)
option(OPENLIERO_VENDORED_DEPS "Use vendored dependencies" OFF)

# find dependencies
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
add_subdirectory(libs/miniz)
add_subdirectory(src/gvl)

# TODO get rid of this kludgy hack
include_directories(
  src
  src/game
)

# embed version metadata in the code
configure_file(
  "${PROJECT_SOURCE_DIR}/pkg/metadata.cpp.in"
  "${PROJECT_SOURCE_DIR}/src/game/metadata.cpp"
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
  src/game/metadata.cpp
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
  gvl
)

if(APPLE)
  add_executable(openliero src/game/sdlmain.cpp)
# Commented out code creates a proper bundle, but the app does not run for ... unknown reasons?
# Anyway, enjoy your weird CLI app, macOS users. :(
#  # https://stackoverflow.com/questions/41121000/cmake-os-x-bundle-recursively-copy-directory-to-resources/65519828#65519828
#  # CC-BY-SA-4.0
#  function(resource VAR SOURCE_PATH DESTINATION PATTERN)
#    file(GLOB_RECURSE _LIST CONFIGURE_DEPENDS ${SOURCE_PATH}/${PATTERN})
#    foreach (RESOURCE ${_LIST})
#      get_filename_component(_PARENT ${RESOURCE} DIRECTORY)
#      if (${_PARENT} STREQUAL ${SOURCE_PATH})
#        set(_DESTINATION ${DESTINATION})
#      else ()
#        file(RELATIVE_PATH _DESTINATION ${SOURCE_PATH} ${_PARENT})
#        set(_DESTINATION ${DESTINATION}/${_DESTINATION})
#      endif ()
#      set_property(SOURCE ${RESOURCE} PROPERTY MACOSX_PACKAGE_LOCATION ${_DESTINATION})
#    endforeach (RESOURCE)
#    set(${VAR} ${_LIST} PARENT_SCOPE)
#  endfunction()
#
#  resource(PROFILES "${CMAKE_CURRENT_SOURCE_DIR}/Profiles" Resources/Profiles *.lpf)
#  resource(RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Resources" Resources/Resources *.png)
#  resource(SETUPS "${CMAKE_CURRENT_SOURCE_DIR}/Setups" Resources/Setups *.cfg)
#  resource(TC "${CMAKE_CURRENT_SOURCE_DIR}/TC" Resources/TC *)
#  resource(ICON "${CMAKE_CURRENT_SOURCE_DIR}/pkg" Resources liero.icns)
#
#  set(RESOURCE_FILES ${PROFILES} ${RESOURCES} ${SETUPS} ${TC} ${ICON})
#
#  add_executable(openliero MACOSX_BUNDLE src/game/sdlmain.cpp "${RESOURCE_FILES}")
elseif(WIN32)
  add_executable(openliero WIN32 src/game/sdlmain.cpp pkg/win32_icon.rc)
else()
  add_executable(openliero src/game/sdlmain.cpp)
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
    gvl
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
    gvl
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
