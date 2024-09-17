# SPDX-License-Identifier: BSL-1.0
# https://gist.github.com/Osyotr/e2d415312a10183bc7c614013494bf21

cmake_minimum_required(VERSION 3.25)

include_guard(GLOBAL)

# Include this file before first project() call.
function(x_vcpkg_bootstrap)
  find_package(Git REQUIRED)

  set(X_VCPKG_GIT_REPOSITORY_URL "https://github.com/Microsoft/vcpkg.git" CACHE STRING "Vcpkg git repository")
  set(X_VCPKG_CLONE_DIR "${CMAKE_SOURCE_DIR}/vcpkg" CACHE PATH "Vcpkg clone directory")

  if(NOT EXISTS "${X_VCPKG_CLONE_DIR}/.git")
    message(STATUS "Cloning vcpkg into ${X_VCPKG_CLONE_DIR}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" clone --quiet "${X_VCPKG_GIT_REPOSITORY_URL}" "${X_VCPKG_CLONE_DIR}"
      ERROR_VARIABLE _vcpkg_git_clone_error
    )
    if(_vcpkg_git_clone_error)
      message(FATAL_ERROR "Could not clone vcpkg repository from ${X_VCPKG_GIT_REPOSITORY_URL}\nMake sure you have access rights and the URL is valid.")
    endif()
    message(STATUS "Cloning vcpkg into ${X_VCPKG_CLONE_DIR} - done")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${X_VCPKG_CLONE_DIR}"
    OUTPUT_VARIABLE _vcpkg_current_head_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  set(_baseline_sha "${_vcpkg_current_head_sha}")

  set(_vcpkg_configuration_json_path "${CMAKE_SOURCE_DIR}/vcpkg-configuration.json")
  if(EXISTS "${_vcpkg_configuration_json_path}")
    file(READ "${_vcpkg_configuration_json_path}" _vcpkg_configuration_json_contents)
    string(JSON _default_registry_kind ERROR_VARIABLE _get_baseline_err GET "${_vcpkg_configuration_json_contents}" "default-registry" "kind")
    if(_default_registry_kind STREQUAL "builtin")
      string(JSON _maybe_baseline_sha ERROR_VARIABLE _get_baseline_err GET "${_vcpkg_configuration_json_contents}" "default-registry" "baseline")
    elseif(_default_registry_kind STREQUAL "git")
      string(JSON _git_registry_repo_url ERROR_VARIABLE _get_baseline_err GET "${_vcpkg_configuration_json_contents}" "default-registry" "repository")
      if(_git_registry_repo_url STREQUAL "${X_VCPKG_GIT_REPOSITORY_URL}")
        string(JSON _maybe_baseline_sha ERROR_VARIABLE _get_baseline_err GET "${_vcpkg_configuration_json_contents}" "default-registry" "baseline")
      endif()
    endif()
  endif()

  if(NOT _maybe_baseline_sha)
    set(_vcpkg_json_path "${CMAKE_SOURCE_DIR}/vcpkg.json")
    if(EXISTS "${_vcpkg_json_path}")
      file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" _vcpkg_json_contents)
      string(JSON _maybe_baseline_sha ERROR_VARIABLE _get_baseline_err GET "${_vcpkg_json_contents}" "builtin-baseline")
    endif()
  endif()

  if(_maybe_baseline_sha)
    set(_baseline_sha "${_maybe_baseline_sha}")
  endif()

  set(_should_update_vcpkg FALSE)
  if(NOT "${_vcpkg_current_head_sha}" STREQUAL "${_baseline_sha}")
    set(_should_update_vcpkg TRUE)
  endif()
  foreach(_patch IN LISTS VCPKG_PATCHES)
    get_filename_component(_patch_name "${_patch}" NAME)
    if(NOT EXISTS "${X_VCPKG_CLONE_DIR}/${_patch_name}.applied")
    set(_should_update_vcpkg TRUE)
    break()
    endif()
  endforeach()

  if(_should_update_vcpkg)
    message(STATUS "Fetching changes from vcpkg upstream")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" fetch --quiet --prune
      WORKING_DIRECTORY "${X_VCPKG_CLONE_DIR}"
      # No error checking here to allow offline usage
    )
    message(STATUS "Fetching changes from vcpkg upstream - done")

    message(STATUS "Switching vcpkg HEAD to ${_baseline_sha}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" reset --quiet --hard "${_baseline_sha}"
      WORKING_DIRECTORY "${X_VCPKG_CLONE_DIR}"
      COMMAND_ERROR_IS_FATAL ANY
    )
    message(STATUS "Switching vcpkg HEAD to ${_baseline_sha} - done")

    # Remove vcpkg executable to trigger bootstrap
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
      file(REMOVE "${X_VCPKG_CLONE_DIR}/vcpkg.exe")
    else()
      file(REMOVE "${X_VCPKG_CLONE_DIR}/vcpkg")
    endif()
  endif()
endfunction()
x_vcpkg_bootstrap()
