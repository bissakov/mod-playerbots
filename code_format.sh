#!/usr/bin/env bash

set -euo pipefail

clang_format="${CLANG_FORMAT_BIN:-clang-format-18}"
if ! command -v "$clang_format" >/dev/null; then
    echo "$clang_format not found; set CLANG_FORMAT_BIN to use another binary." >&2
    exit 1
fi

root="$(cd "$(dirname "$0")" && pwd)"
find "$root/src" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
    -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) -print0 |
    xargs -0 -r "$clang_format" -i
