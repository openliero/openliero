#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "miniz::miniz" for configuration "Debug"
set_property(TARGET miniz::miniz APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(miniz::miniz PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/libminiz.a"
  )

list(APPEND _cmake_import_check_targets miniz::miniz )
list(APPEND _cmake_import_check_files_for_miniz::miniz "${_IMPORT_PREFIX}/debug/lib/libminiz.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
