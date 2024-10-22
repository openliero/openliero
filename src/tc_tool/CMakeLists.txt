add_library(tc)

target_sources(tc
  PRIVATE
  common_exereader.cpp
  common_writer.cpp
)

target_link_libraries(tc
  PUBLIC
  game
)

add_executable(tctool main.cpp)
target_link_libraries(tctool tc)
