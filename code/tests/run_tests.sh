#!/usr/bin/env bash
set -eu
# Usage: GCC=/path/to/gcc TEST_OUTPUT_DIR=/scratch bash code/tests/run_tests.sh
# Keep native executables outside the repository. Output directory is explicit.
: "${TEST_OUTPUT_DIR:?Set TEST_OUTPUT_DIR to a writable scratch directory}"
GCC="${GCC:-gcc}"
cd "$(dirname "$0")/../.."
mkdir -p "$TEST_OUTPUT_DIR"
for suite in buttons ui presenter ac_capacity signal_formats config_store ir_dma_probe bosch144 ir_capture app; do
    sources="code/Main/buttons_logic.c"
    extra_flags=""
    if [ "$suite" = ui ] || [ "$suite" = ac_capacity ] || [ "$suite" = signal_formats ]; then
        sources="$sources code/Main/ui.c"
    elif [ "$suite" = presenter ]; then
        sources="$sources code/Main/ui.c code/Main/ui_presenter.c code/Main/ui_font.c"
    elif [ "$suite" = config_store ]; then
        sources="code/Main/config_store.c"
    elif [ "$suite" = bosch144 ]; then
        sources="code/Main/bosch144.c"
    elif [ "$suite" = ir_dma_probe ]; then
        sources="code/Main/ir_dma_probe.c"
        extra_flags="-DIR_DMA_PROBE_PORTABLE"
    elif [ "$suite" = ir_capture ]; then
        sources="code/Main/ir_capture.c code/Main/ir_hardware.c code/Main/bosch144.c"
        extra_flags="-DIR_HARDWARE_TEST"
    elif [ "$suite" = app ]; then
        sources="$sources code/Main/ui.c code/Main/config_store.c code/Main/ui_presenter.c code/Main/bosch144.c"
        extra_flags="-Icode/tests/integration_mocks -Icode/Library"
    fi
    "$GCC" -std=c90 -pedantic-errors -Wall -Wextra -Werror $extra_flags -Icode/Main \
        "code/tests/test_$suite.c" $sources \
        -o "$TEST_OUTPUT_DIR/test_$suite.exe"
    if [ "$suite" = bosch144 ]; then
        "$TEST_OUTPUT_DIR/test_$suite.exe" tests/fixtures/upstream_bosch144_cool16.json
    else
        "$TEST_OUTPUT_DIR/test_$suite.exe"
    fi
done
