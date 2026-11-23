#!/usr/bin/env bash
# Checks the C++: its formatting (.clang-format) and its lint (.clang-tidy),
# with clang-format and clang-tidy 20, as CI does. clang-tidy reads the native
# build's compile_commands.json, so build the native preset first.
# Fails on any difference from the format or any warning.
#
# Usage: tools/lint-cpp.sh [--fix]
#   --fix  reformats the files instead of only checking them
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

# Tracked and new files, but not deleted ones.
files=()
while IFS= read -r file; do
    if [[ -f $file ]]; then
        files+=("$file")
    fi
done < <(git ls-files --cached --others --exclude-standard 'cpp/*.cpp' 'cpp/*.hpp')

if [[ ${1:-} == --fix ]]; then
    clang-format-20 -i "${files[@]}"
else
    clang-format-20 --dry-run --Werror "${files[@]}"
fi

# The headers of the compiler the build uses, not clang's guess, which can be
# a newer GCC installed without its C++ headers.
gcc_dir=$(dirname "$(c++ -print-libgcc-file-name)")
run-clang-tidy-20 -p cpp/build/native -quiet -warnings-as-errors='*' -extra-arg=--gcc-install-dir="$gcc_dir" 'cpp/(apps|modules)/'
