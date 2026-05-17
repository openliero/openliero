#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SDL3_image::SDL3_image-static" for configuration "Debug"
set_property(TARGET SDL3_image::SDL3_image-static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SDL3_image::SDL3_image-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/libSDL3_image.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::SDL3_image-static )
list(APPEND _cmake_import_check_files_for_SDL3_image::SDL3_image-static "${_IMPORT_PREFIX}/debug/lib/libSDL3_image.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
