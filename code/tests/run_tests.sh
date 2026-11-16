#!/usr/bin/env bash
set -eu
# Run from any directory; binaries remain outside the source tree.
: "${TEST_OUTPUT_DIR:?Set TEST_OUTPUT_DIR to a writable scratch directory}"
GCC="${GCC:-gcc}"
cd "$(dirname "$0")/../.."
mkdir -p "$TEST_OUTPUT_DIR"
ac_sources="code/Main/bosch144.c code/Main/ac_codec.c code/Main/ac_gree.c code/Main/ac_midea.c code/Main/ac_coolix.c code/Main/ac_haier.c"
for suite in buttons ui presenter ac_capacity signal_formats config_store ir_dma_probe bosch144 ir_capture app ac_gree ac_midea ac_coolix ac_haier ac_codec ac_learning ac_profiles; do
    extra_flags=""
    case "$suite" in
        buttons) sources="code/Main/buttons_logic.c" ;;
        ui|ac_capacity|signal_formats|ac_learning) sources="code/Main/buttons_logic.c code/Main/ui.c $ac_sources" ;;
        presenter) sources="code/Main/ui.c code/Main/ui_presenter.c code/Main/ui_font.c $ac_sources" ;;
        config_store|ac_profiles) sources="code/Main/ui.c code/Main/config_store.c $ac_sources" ;;
        bosch144) sources="code/Main/bosch144.c" ;;
        ir_dma_probe) sources="code/Main/ir_dma_probe.c"; extra_flags="-DIR_DMA_PROBE_PORTABLE" ;;
        ir_capture) sources="code/Main/ir_capture.c code/Main/ir_hardware.c $ac_sources"; extra_flags="-DIR_HARDWARE_TEST" ;;
        app) sources="code/Main/buttons_logic.c code/Main/ui.c code/Main/config_store.c code/Main/ui_presenter.c $ac_sources"; extra_flags="-Icode/tests/integration_mocks -Icode/Library" ;;
        ac_gree|ac_midea|ac_coolix|ac_haier) sources="code/Main/$suite.c" ;;
        ac_codec) sources="$ac_sources" ;;
    esac
    "$GCC" -std=c90 -pedantic-errors -Wall -Wextra -Werror $extra_flags -Icode/Main \
        "code/tests/test_$suite.c" $sources -o "$TEST_OUTPUT_DIR/test_$suite.exe"
    if [ "$suite" = bosch144 ]; then
        "$TEST_OUTPUT_DIR/test_$suite.exe" tests/fixtures/upstream_bosch144_cool16.json
    else
        "$TEST_OUTPUT_DIR/test_$suite.exe"
    fi
done
