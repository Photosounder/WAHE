#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_dir=$(mktemp -d)
test_exe="$test_dir/message_ownership"
libs=(-lm -ldl -pthread)
flags=(-std=gnu11 -O2 -Wno-incompatible-pointer-types -Wno-int-conversion)
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        test_exe+=".exe"
        flags+=(-static)
        libs=(-lm -lshell32 -lole32 -lcomdlg32 -luser32 -ladvapi32)
        ;;
esac
trap 'rm -f -- "$test_exe"; rmdir -- "$test_dir"' EXIT
"${CC:-cc}" "${flags[@]}" "$root/tests/message_ownership.c" -o "$test_exe" "${libs[@]}"
"$test_exe" "$@"
