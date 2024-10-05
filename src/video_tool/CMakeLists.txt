add_library(vt)

target_sources(vt
  PRIVATE
  replay_to_video.cpp
  video_recorder.c
)

target_link_libraries(vt
  PUBLIC
  game
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

add_executable(videotool tools_main.cpp)
target_link_libraries(videotool vt)
