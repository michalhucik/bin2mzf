#!/bin/bash
# Základní CLI test pro mzf-strip MVP.
#
# Pokrývá: existence binárky, --help/--version/--lib-versions,
# round-trip přes bin2mzf -> mzf-strip (file i stdin, file i stdout),
# header-only výstup (fsize=0), truncated body, oversized vstup,
# header too short, neexistující soubor, prázdný vstup, binární
# round-trip s celým rozsahem 0x00..0xFF.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# T1. existence binárky a --help
test_help() {
    if ! [[ -x "$MZF_STRIP" || -x "$MZF_STRIP.exe" ]]; then
        echo "    FAIL: binary not found at $MZF_STRIP"
        return 1
    fi
    local out
    out=$("$MZF_STRIP" --help 2>&1) || return 1
    assert_contains "$out" "Usage" "help contains Usage" || return 1
}

# T2. --version
test_version() {
    local out
    out=$("$MZF_STRIP" --version 2>&1) || return 1
    assert_contains "$out" "mzf-strip 0.1.0" "version contains 'mzf-strip 0.1.0'" || return 1
}

# T3. --lib-versions
test_lib_versions() {
    local out
    out=$("$MZF_STRIP" --lib-versions 2>&1) || return 1
    assert_contains "$out" "mzf" "lib-versions contains mzf" || return 1
}

# T4. round-trip file -> file
test_roundtrip_file_to_file() {
    printf 'HELLO_WORLD_1234' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n RT1 -l 0x1200 -o "$TEST_TMPDIR/rt1.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/rt1.mzf" -o "$TEST_TMPDIR/out.bin" || return 1
    cmp "$TEST_TMPDIR/in.bin" "$TEST_TMPDIR/out.bin" || {
        echo "    FAIL: round-trip file->file mismatch"
        return 1
    }
}

# T5. round-trip stdin -> file
test_roundtrip_stdin_to_file() {
    printf 'STDIN_INPUT_DATA' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n RT2 -l 0x1200 -o "$TEST_TMPDIR/rt2.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_STRIP" -o "$TEST_TMPDIR/out.bin" < "$TEST_TMPDIR/rt2.mzf" || return 1
    cmp "$TEST_TMPDIR/in.bin" "$TEST_TMPDIR/out.bin" || {
        echo "    FAIL: round-trip stdin->file mismatch"
        return 1
    }
}

# T6. round-trip stdin -> stdout (pipe)
test_roundtrip_pipe() {
    printf 'PIPE_DATA_BYTES' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n RT3 -l 0x1200 -o "$TEST_TMPDIR/rt3.mzf" "$TEST_TMPDIR/in.bin" || return 1
    cat "$TEST_TMPDIR/rt3.mzf" | "$MZF_STRIP" > "$TEST_TMPDIR/out.bin" || return 1
    cmp "$TEST_TMPDIR/in.bin" "$TEST_TMPDIR/out.bin" || {
        echo "    FAIL: round-trip pipe mismatch"
        return 1
    }
}

# T7. header-only -> empty output (fsize == 0, rc == 0)
test_header_only_empty_output() {
    "$BIN2MZF" --header-only -n HO -l 0x1200 -o "$TEST_TMPDIR/ho.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/ho.mzf" -o "$TEST_TMPDIR/out.bin" || return 1
    if [ -s "$TEST_TMPDIR/out.bin" ]; then
        echo "    FAIL: header-only output is not empty"
        return 1
    fi
}

# T8. header too short -> error
test_header_too_short() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n SHORT -l 0x1200 -o "$TEST_TMPDIR/short.mzf" "$TEST_TMPDIR/in.bin" || return 1
    head -c 100 "$TEST_TMPDIR/short.mzf" > "$TEST_TMPDIR/clipped.mzf"
    local err
    err=$("$MZF_STRIP" "$TEST_TMPDIR/clipped.mzf" -o "$TEST_TMPDIR/out.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "header-too-short must fail" || return 1
    if echo "$err" | grep -qE "shorter|header"; then
        return 0
    fi
    echo "    FAIL: stderr does not contain 'shorter' or 'header': $err"
    return 1
}

# T9. truncated body -> error (size mismatch)
test_truncated_body() {
    # Vytvoř MZF s 128B tělem a uřízni na 200B (60B chybí v těle).
    printf 'A%.0s' $(seq 1 128) > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n TRUNC -l 0x1200 -o "$TEST_TMPDIR/trunc.mzf" "$TEST_TMPDIR/in.bin" || return 1
    head -c 200 "$TEST_TMPDIR/trunc.mzf" > "$TEST_TMPDIR/truncated.mzf"
    local err
    err=$("$MZF_STRIP" "$TEST_TMPDIR/truncated.mzf" -o "$TEST_TMPDIR/out.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "truncated body must fail" || return 1
    assert_contains "$err" "size mismatch" "stderr contains 'size mismatch'" || return 1
}

# T10. empty input -> error
test_empty_input() {
    : > "$TEST_TMPDIR/empty.mzf"
    "$MZF_STRIP" "$TEST_TMPDIR/empty.mzf" -o "$TEST_TMPDIR/out.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "empty input must fail" || return 1
}

# T11. nonexistent file -> error
test_nonexistent_file() {
    local err
    err=$("$MZF_STRIP" "$TEST_TMPDIR/does_not_exist.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "nonexistent file must fail" || return 1
    assert_contains "$err" "Error" "stderr contains 'Error'" || return 1
}

# T12. oversized input -> error (size mismatch)
test_oversized_input() {
    printf 'B%.0s' $(seq 1 32) > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n OVR -l 0x1200 -o "$TEST_TMPDIR/ovr.mzf" "$TEST_TMPDIR/in.bin" || return 1
    # Připoj extra bajty, aby vstup byl > 128 + fsize (32).
    printf 'EXTRA_BYTES' > "$TEST_TMPDIR/extra.bin"
    cat "$TEST_TMPDIR/ovr.mzf" "$TEST_TMPDIR/extra.bin" > "$TEST_TMPDIR/oversized.mzf"
    local err
    err=$("$MZF_STRIP" "$TEST_TMPDIR/oversized.mzf" -o "$TEST_TMPDIR/out.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "oversized input must fail" || return 1
    assert_contains "$err" "size mismatch" "stderr contains 'size mismatch'" || return 1
}

# T13. binary data round-trip - všech 256 hodnot 0x00..0xFF
test_binary_roundtrip() {
    # Vytvoř soubor s posloupností 0x00..0xFF (256 bajtů).
    python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)))" > "$TEST_TMPDIR/in.bin" 2>/dev/null \
        || perl -e 'binmode STDOUT; print pack("C*", 0..255)' > "$TEST_TMPDIR/in.bin" \
        || {
            # Fallback: printf v shellu
            for i in $(seq 0 255); do
                printf "\\x$(printf '%02x' "$i")"
            done > "$TEST_TMPDIR/in.bin"
        }
    "$BIN2MZF" -n BIN -l 0x1000 -o "$TEST_TMPDIR/bin.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/bin.mzf" -o "$TEST_TMPDIR/out.bin" || return 1
    cmp "$TEST_TMPDIR/in.bin" "$TEST_TMPDIR/out.bin" || {
        echo "    FAIL: binary round-trip mismatch"
        return 1
    }
}

run_test test_help
run_test test_version
run_test test_lib_versions
run_test test_roundtrip_file_to_file
run_test test_roundtrip_stdin_to_file
run_test test_roundtrip_pipe
run_test test_header_only_empty_output
run_test test_header_too_short
run_test test_truncated_body
run_test test_empty_input
run_test test_nonexistent_file
run_test test_oversized_input
run_test test_binary_roundtrip

test_summary
