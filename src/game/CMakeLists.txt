add_library(game)

# embed version metadata
configure_file(
  "${PROJECT_SOURCE_DIR}/tools/cmake/metadata.cpp.in"
  "${PROJECT_SOURCE_DIR}/src/game/metadata.cpp"
)

target_sources(game PUBLIC
  ai/dijkstra.cpp
  ai/predictive_ai.cpp
  bobject.cpp
  bonus.cpp
  common.cpp
  console.cpp
  constants.cpp
  controller/commonController.cpp
  controller/localController.cpp
  controller/replayController.cpp
  controller/stats_presenter.cpp
  filesystem.cpp
  game.cpp
  gfx/blit.cpp
  gfx/font.cpp
  gfx/palette.cpp
  gfx/renderer.cpp
  gfx/sprite.cpp
  gfx.cpp
  keys.cpp
  level.cpp
  math.cpp
  menu/booleanSwitchBehavior.cpp
  menu/enumBehavior.cpp
  menu/hiddenMenu.cpp
  menu/integerBehavior.cpp
  menu/mainMenu.cpp
  menu/menu.cpp
  menu/menuItem.cpp
  menu/timeBehavior.cpp
  metadata.cpp
  mixer/mixer.cpp
  mixer/player.cpp
  ninjarope.cpp
  nobject.cpp
  replay.cpp
  settings.cpp
  sfx.cpp
  sobject.cpp
  spectatorviewport.cpp
  stats_recorder.cpp
  text.cpp
  viewport.cpp
  weapon.cpp
  weapsel.cpp
  worm.cpp
)

target_include_directories(game PUBLIC .)

target_link_libraries(game
  PUBLIC
  miniz::miniz
  gvl
  $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
  $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
)
