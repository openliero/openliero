#!/usr/bin/env bash

cppcheck_opts=(
	--check-level=exhaustive
	--library=posix
	--enable=all
	--suppress=missingIncludeSystem
	--std=c++11
	--std=c99
)

cppcheck_include_folders=(
	-I src/gvl/include
	-I src
	-I src/game
)

cppcheck "${cppcheck_opts[@]}" "${cppcheck_include_folders[@]}" src/game 2>errors.log
