cmake_minimum_required(VERSION 3.21)

project(gvl
  DESCRIPTION "A collection of versatile support libraries for C++"
  LANGUAGES C CXX
)

set(GVL_SOURCES
  src/containers/list.cpp
  src/io2/convert.cpp
  src/io2/stream.cpp
  src/support/bits.c
  src/support/debug.cpp
  src/support/platform.cpp
  src/system/system.c
)

add_library(${PROJECT_NAME} ${GVL_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include)
