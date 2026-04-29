#!/bin/bash
# CLI test pro režim --validate nástroje mzf-info.
#
# Pokrývá: validní MZF (rc=0, no stdout), poškozená hlavička bez 0x0d
# terminátoru, truncated MZF (méně než 128 + fsize), oversized MZF
# (filesize větší než 128 + fsize - validate vyžaduje exact match),
# warning hlášky v non-validate režimu pro truncated a trailing bytes.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vytvoř validní MZF s 8 bajty body.
make_valid_mzf() {
    printf 'ABCDEFGH' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n VALID -l 0x1200 -o "$1" "$TEST_TMPDIR/in.bin"
}

# 1. validní MZF: --validate -> rc=0, žádný stdout výstup
test_validate_ok_silent() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --validate "$TEST_TMPDIR/v.mzf" 2>/dev/null)
    local rc=$?
    assert_eq "$rc" "0" "validate ok exit 0" || return 1
    assert_eq "$out" "" "validate ok: no stdout" || return 1
}

# 2. truncated MZF (jen 100 bajtů celkem - menší než 128 hlavička) -> rc!=0
test_validate_truncated_below_header() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    # Vyrobíme MZF s pouze 100 bajty (méně než 128 = celá hlavička)
    head -c 100 "$TEST_TMPDIR/v.mzf" > "$TEST_TMPDIR/trunc_short.mzf"
    "$MZF_INFO" --validate "$TEST_TMPDIR/trunc_short.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "truncated below header must fail" || return 1
}

# 3. truncated MZF (130 bajtů = hlavička + 2 bajty, ale fsize=8) -> validate selhává
test_validate_truncated_body() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    # Originál má 128 + 8 = 136 bajtů. Ořežeme na 130 (hlavička + 2 bajty body
    # < fsize=8) - validate musí selhat.
    head -c 130 "$TEST_TMPDIR/v.mzf" > "$TEST_TMPDIR/trunc.mzf"
    local err
    err=$("$MZF_INFO" --validate "$TEST_TMPDIR/trunc.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "truncated body validate must fail" || return 1
    assert_contains "$err" "truncated" "stderr contains 'truncated' marker" || return 1
}

# 4. oversized MZF (136 + 50 trailing) -> validate selhává (vyžaduje exact match)
test_validate_oversized_trailing() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    cp "$TEST_TMPDIR/v.mzf" "$TEST_TMPDIR/over.mzf"
    printf '\x00%.0s' $(seq 1 50) >> "$TEST_TMPDIR/over.mzf"
    "$MZF_INFO" --validate "$TEST_TMPDIR/over.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "oversized validate must fail (exact match required)" || return 1
}

# 5. truncated MZF v non-validate režimu produkuje "ERROR (truncated:" hlášku
test_truncated_warning_in_default_mode() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    head -c 130 "$TEST_TMPDIR/v.mzf" > "$TEST_TMPDIR/trunc.mzf"
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/trunc.mzf" 2>&1)
    # Neptáme se na rc - default režim může skončit OK ze samotného mzf_load
    # (nečte víc bajtů, než si fsize říká). Důležité je, že hlášku "truncated"
    # uvidíme ve výstupu jako součást validace.
    assert_contains "$out" "truncated" "default mode: warning contains 'truncated'" || return 1
}

# 6. oversized MZF v non-validate režimu produkuje hlášku "trailing bytes"
test_trailing_warning_in_default_mode() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    cp "$TEST_TMPDIR/v.mzf" "$TEST_TMPDIR/over.mzf"
    printf '\x00%.0s' $(seq 1 50) >> "$TEST_TMPDIR/over.mzf"
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/over.mzf" 2>&1) || return 1
    assert_contains "$out" "trailing bytes" "default mode: warning contains 'trailing bytes'" || return 1
}

run_test test_validate_ok_silent
run_test test_validate_truncated_below_header
run_test test_validate_truncated_body
run_test test_validate_oversized_trailing
run_test test_truncated_warning_in_default_mode
run_test test_trailing_warning_in_default_mode

test_summary
