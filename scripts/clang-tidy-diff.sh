#!/usr/bin/env bash
# Run clang-tidy on lines changed in HEAD relative to a base git ref.
# Mirrors the .github/workflows/clang-tidy.yml job, so a clean run here
# means a clean run in CI.
#
# Usage:
#   scripts/clang-tidy-diff.sh <build-dir> [base-ref]
#
#   build-dir  Directory containing compile_commands.json
#              (e.g. build/linux-x64).
#   base-ref   Git ref to diff against (default: origin/master).
#
# Environment:
#   CLANG_TIDY_DIFF  Override the path to clang-tidy-diff.py.

set -euo pipefail

if [ "$#" -lt 1 ]; then
	cat <<'EOF' >&2
usage: clang-tidy-diff.sh <build-dir> [base-ref]
EOF
	exit 2
fi

build_dir="$1"
base_ref="${2:-origin/master}"

if [ ! -f "${build_dir}/compile_commands.json" ]; then
	echo "error: ${build_dir}/compile_commands.json not found" >&2
	echo "       configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
	exit 1
fi

script="${CLANG_TIDY_DIFF:-}"
if [ -z "$script" ]; then
	# Ubuntu 24.04+ ships the script unsuffixed; older releases use a
	# version suffix. Homebrew installs it under llvm's share directory.
	for candidate in \
		/usr/share/clang/clang-tidy-diff.py \
		/usr/share/clang/clang-tidy-diff-*.py \
		/opt/homebrew/opt/llvm/share/clang/clang-tidy-diff.py \
		/usr/local/opt/llvm/share/clang/clang-tidy-diff.py
	do
		if [ -f "$candidate" ]; then
			script="$candidate"
			break
		fi
	done
fi

if [ -z "$script" ] || [ ! -f "$script" ]; then
	echo "error: clang-tidy-diff.py not found" >&2
	echo "       install clang-tidy (apt/brew) or set CLANG_TIDY_DIFF" >&2
	exit 1
fi

if ! jobs=$(nproc 2>/dev/null); then
	jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

output=$(git diff -U0 "${base_ref}"...HEAD -- 'src/*' \
	| python3 "$script" \
		-p1 \
		-path "$build_dir" \
		-j"$jobs" \
		-regex '.*\.(cpp|hpp|h|cc|cxx)$' \
		-quiet)

echo "$output"

if echo "$output" | grep -E "^[^[:space:]]+:[0-9]+:[0-9]+: (warning|error):" >/dev/null; then
	echo "clang-tidy reported issues on changed lines" >&2
	exit 1
fi
