#!/usr/bin/env bash
set -eu
# Usage: GCC=/path/to/gcc TEST_OUTPUT_DIR=/scratch bash code/tests/run_tests.sh
# Keep native executables outside the repository. Output directory is explicit.
: "${TEST_OUTPUT_DIR:?Set TEST_OUTPUT_DIR to a writable scratch directory}"
GCC="${GCC:-gcc}"
cd "$(dirname "$0")/../.."
mkdir -p "$TEST_OUTPUT_DIR"
for suite in buttons ui presenter; do
    sources="code/Main/buttons_logic.c"
    if [ "$suite" = ui ]; then
        sources="$sources code/Main/ui.c"
    elif [ "$suite" = presenter ]; then
        sources="$sources code/Main/ui.c code/Main/ui_presenter.c code/Main/ui_font.c"
    fi
    "$GCC" -std=c90 -pedantic-errors -Wall -Wextra -Werror -Icode/Main \
        "code/tests/test_$suite.c" $sources \
        -o "$TEST_OUTPUT_DIR/test_$suite.exe"
    "$TEST_OUTPUT_DIR/test_$suite.exe"
done
