cmake_minimum_required(VERSION 3.25..3.30)

# options at top so they're easy to find
option(OPENLIERO_BUILD_TCTOOL "Build tctool" OFF)
option(OPENLIERO_BUILD_VIDEOTOOL "Build videotool" OFF)
option(OPENLIERO_USE_VCPKG "Use vcpkg to manage dependencies" ON)

# tell cmake we have goodies in the tools/cmake directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake")

include(GenerateVersion) # generate version metadata from git

# configure vcpkg
if(OPENLIERO_USE_VCPKG)
  set(VCPKG_TOOLCHAIN_PATH
    "scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "General path to vcpkg toolchain file"
  )

  # use local vcpkg
  if(EXISTS "$ENV{VCPKG_ROOT}/${VCPKG_TOOLCHAIN_PATH}")
    message(STATUS "Using system-provided vcpkg to manage dependencies")
    set(CMAKE_TOOLCHAIN_FILE
      "$ENV{VCPKG_ROOT}/${VCPKG_TOOLCHAIN_PATH}"
      CACHE STRING "vcpkg toolchain file"
    )
  else()
    message(STATUS "Using bootstrapped vcpkg to manage dependencies")
    set(X_VCPKG_CLONE_DIR "${CMAKE_SOURCE_DIR}/tools/vcpkg/vcpkg"
      CACHE PATH "vcpkg clone directory"
    )
    set(CMAKE_TOOLCHAIN_FILE
      "${X_VCPKG_CLONE_DIR}/${VCPKG_TOOLCHAIN_PATH}"
      CACHE STRING "vcpkg toolchain file"
    )
    include(BootstrapVcpkg)
  endif()

  set(VCPKG_BOOTSTRAP_OPTIONS "-disableMetrics")
  set(VCPKG_INSTALL_OPTIONS "--no-print-usage")
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
else()
  message(STATUS "Using system-provided dependencies")
endif()

project(openliero
  DESCRIPTION "A brutal subterranean shooter game"
  HOMEPAGE_URL https://github.com/openliero/openliero
  LANGUAGES C CXX
)

# find dependencies
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

# local dependencies
add_subdirectory(src/gvl)
add_subdirectory(src/game)

if(APPLE)
  add_executable(openliero
    src/game/gameEntry.cpp
    src/game/main.cpp
  )
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
#  resource(PROFILES "${CMAKE_CURRENT_SOURCE_DIR}/data/Profiles" Resources/Profiles *.lpf)
#  resource(RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/data/Resources" Resources/Resources *.png)
#  resource(SETUPS "${CMAKE_CURRENT_SOURCE_DIR}/data/Setups" Resources/Setups *.cfg)
#  resource(TC "${CMAKE_CURRENT_SOURCE_DIR}/data/TC" Resources/TC *)
#  resource(ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons" Resources openliero.icns)
#
#  set(RESOURCE_FILES ${PROFILES} ${RESOURCES} ${SETUPS} ${TC} ${ICON})
#
#  add_executable(openliero MACOSX_BUNDLE src/game/main.cpp "${RESOURCE_FILES}")
elseif(WIN32)
  add_executable(openliero
    WIN32
    src/game/gameEntry.cpp
    src/game/main.cpp
    packaging/icons/openliero.rc
  )
else()
  add_executable(openliero
    src/game/gameEntry.cpp
    src/game/main.cpp
  )
endif()

target_link_libraries(openliero PRIVATE game)

if(OPENLIERO_BUILD_TCTOOL)
  add_subdirectory(src/tc_tool)
endif()

if(OPENLIERO_BUILD_VIDEOTOOL)
  add_subdirectory(src/video_tool)
endif()

# binaries
install(TARGETS openliero CONFIGURATIONS Release DESTINATION .)
if(OPENLIERO_BUILD_TCTOOL)
  install(TARGETS tctool CONFIGURATIONS Release DESTINATION .)
endif()
if(OPENLIERO_BUILD_VIDEOTOOL)
  install(TARGETS videotool CONFIGURATIONS Release DESTINATION .)
endif()

# assets
install(DIRECTORY data/Profiles DESTINATION .)
install(DIRECTORY data/Resources DESTINATION .)
install(DIRECTORY data/Setups DESTINATION .)
install(DIRECTORY data/TC DESTINATION .)

# documentation
install(FILES README.md DESTINATION .)

# licenses
install(FILES LICENSE DESTINATION LICENSES)
install(FILES data/TC/openliero/sounds/LICENSE RENAME LICENSE.SOUNDS DESTINATION LICENSES)
