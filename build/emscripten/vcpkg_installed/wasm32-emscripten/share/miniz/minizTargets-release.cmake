#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "miniz::miniz" for configuration "Release"
set_property(TARGET miniz::miniz APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(miniz::miniz PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libminiz.a"
  )

list(APPEND _cmake_import_check_targets miniz::miniz )
list(APPEND _cmake_import_check_files_for_miniz::miniz "${_IMPORT_PREFIX}/lib/libminiz.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
