#!/bin/bash
# Základní CLI test pro mzf-paste MVP (v0.1.0).
#
# Pokrývá: existence binárky, --help/--version/--lib-versions, insert
# round-trip (offset 0/mid/end/numeric/keyword), overwrite (mid, extend),
# gap (insert/overwrite + custom filler), --from-mzf, mutex --insert
# × --overwrite, in-place modifikace, -o output, overflow > 65535,
# nevalidní target, chybějící positional argumenty, chybějící --at.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vyčte N hex bajtů od offsetu OFF z BIN ve formátu
# odděleném mezerami (bez čísel řádků).
read_hex_bytes() {
    local file="$1"
    local off="$2"
    local n="$3"
    od -An -v -tx1 -j"$off" -N"$n" "$file" | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'
}

# Pomocná funkce: vytvoří MZF base soubor s daným obsahem těla.
make_base_mzf() {
    local body="$1"
    local out="$2"
    local name="${3:-BASE}"
    printf '%s' "$body" > "$TEST_TMPDIR/_body.bin"
    "$BIN2MZF" -n "$name" -l 0x1200 -i "$TEST_TMPDIR/_body.bin" -o "$out"
}

# T1. existence binárky
test_binary_exists() {
    if [[ -x "$MZF_PASTE" || -x "$MZF_PASTE.exe" ]]; then
        return 0
    fi
    echo "    FAIL: binary not found at $MZF_PASTE"
    return 1
}

# T2. --help vypisuje Usage a vrací 0
test_help() {
    local out
    out=$("$MZF_PASTE" --help 2>&1) || return 1
    assert_contains "$out" "Usage" "help contains Usage" || return 1
}

# T3. --version vrací 0 a obsahuje "mzf-paste 0.1.0"
test_version() {
    local out
    out=$("$MZF_PASTE" --version 2>&1) || return 1
    assert_contains "$out" "mzf-paste 0.1.0" "version contains mzf-paste 0.1.0" || return 1
    assert_contains "$out" "bin2mzf-cli" "version contains release name" || return 1
}

# T4. --lib-versions obsahuje názvy všech knihoven
test_lib_versions() {
    local out
    out=$("$MZF_PASTE" --lib-versions 2>&1) || return 1
    assert_contains "$out" "mzf" "lib-versions contains mzf" || return 1
    assert_contains "$out" "endianity" "lib-versions contains endianity" || return 1
}

# T5. insert na offset 0: výsledek = input + target
test_insert_at_zero() {
    make_base_mzf 'AAAABBBB' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'XXXX' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --insert --at 0 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 12)
    assert_eq "$actual" "58 58 58 58 41 41 41 41 42 42 42 42" \
              "insert at 0: XXXX + AAAABBBB" || return 1
}

# T6. insert na mid offset (4): target[0..4] + input + target[4..]
test_insert_mid() {
    make_base_mzf 'AAAABBBBCCCC' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'YYYY' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --insert --at 0x04 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 16)
    assert_eq "$actual" "41 41 41 41 59 59 59 59 42 42 42 42 43 43 43 43" \
              "insert mid: AAAA + YYYY + BBBBCCCC" || return 1
}

# T7. insert na konec (numeric)
test_insert_at_end_numeric() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'ZZ' > "$TEST_TMPDIR/p.bin"
    # current body size = 4
    "$MZF_PASTE" --insert --at 4 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 6)
    assert_eq "$actual" "41 41 41 41 5a 5a" "insert at fsize numeric: AAAA + ZZ" || return 1
}

# T8. insert na konec (keyword 'end')
test_insert_at_end_keyword() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'ZZ' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --insert --at end "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 6)
    assert_eq "$actual" "41 41 41 41 5a 5a" "insert at 'end': AAAA + ZZ" || return 1
}

# T9. overwrite mid: body size beze změny
test_overwrite_mid() {
    make_base_mzf 'AAAABBBBCCCC' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'YYYY' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --overwrite --at 4 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    assert_eq "$sz" "12" "overwrite preserves body size" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 12)
    assert_eq "$actual" "41 41 41 41 59 59 59 59 43 43 43 43" \
              "overwrite mid: AAAA + YYYY + CCCC" || return 1
}

# T10. overwrite + extend: body size roste
test_overwrite_extend() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'YYYYZZZZ' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --overwrite --extend --at 2 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    assert_eq "$sz" "10" "overwrite+extend grows body to offset+input" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 10)
    assert_eq "$actual" "41 41 59 59 59 59 5a 5a 5a 5a" \
              "overwrite extend: AA + YYYYZZZZ" || return 1
}

# T11. overwrite past end bez --extend -> error
test_overwrite_past_end_no_extend() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'YYYYYY' > "$TEST_TMPDIR/p.bin"
    local err_out
    err_out=$("$MZF_PASTE" --overwrite --at 2 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
              -o "$TEST_TMPDIR/out.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "overwrite past end without --extend must fail" || return 1
    assert_contains "$err_out" "extend" "error mentions --extend" || return 1
}

# T12. gap insert (offset > fsize): filler default 0x00
test_gap_insert_default_filler() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'ZZ' > "$TEST_TMPDIR/p.bin"
    # current size = 4, offset = 8 -> gap [4..8) = 0x00, then ZZ at 8..10
    "$MZF_PASTE" --insert --at 8 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    assert_eq "$sz" "10" "gap insert: target_size + gap + input" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 10)
    assert_eq "$actual" "41 41 41 41 00 00 00 00 5a 5a" \
              "gap default 0x00: AAAA + 0000 + ZZ" || return 1
}

# T13. gap insert custom filler 0xFF
test_gap_insert_custom_filler() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'ZZ' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --insert --at 8 --filler 0xFF "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 10)
    assert_eq "$actual" "41 41 41 41 ff ff ff ff 5a 5a" \
              "gap filler 0xFF: AAAA + FFFF + ZZ" || return 1
}

# T14. --from-mzf: vstup je MZF, použije se jen tělo
test_from_mzf() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    make_base_mzf 'YYYY' "$TEST_TMPDIR/input.mzf" INP || return 1
    "$MZF_PASTE" --from-mzf --insert --at 4 "$TEST_TMPDIR/input.mzf" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 8)
    assert_eq "$actual" "41 41 41 41 59 59 59 59" \
              "from-mzf: append YYYY (extracted from MZF body)" || return 1
}

# T15. mutex --insert × --overwrite: hard error
test_mutex_insert_overwrite() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'X' > "$TEST_TMPDIR/p.bin"
    local err_out
    err_out=$("$MZF_PASTE" --insert --overwrite --at 0 "$TEST_TMPDIR/p.bin" \
              "$TEST_TMPDIR/base.mzf" -o "$TEST_TMPDIR/out.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "--insert + --overwrite must fail" || return 1
    assert_contains "$err_out" "mutually exclusive" "error mentions 'mutually exclusive'" || return 1
}

# T16. in-place modifikace (bez -o): target je modifikován
test_in_place_modify() {
    make_base_mzf 'AAAABBBB' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'XXXX' > "$TEST_TMPDIR/p.bin"
    "$MZF_PASTE" --insert --at 4 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" || return 1
    assert_file_exists "$TEST_TMPDIR/base.mzf" "target file still exists" || return 1
    # tmp file should not exist
    if [ -f "$TEST_TMPDIR/base.mzf.tmp" ]; then
        echo "    FAIL: tmp file leaked"
        return 1
    fi
    "$MZF_STRIP" "$TEST_TMPDIR/base.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local actual
    actual=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 0 12)
    assert_eq "$actual" "41 41 41 41 58 58 58 58 42 42 42 42" \
              "in-place modify: target updated" || return 1
}

# T17. -o output: original target zachován
test_output_preserves_target() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'XX' > "$TEST_TMPDIR/p.bin"
    local orig_size
    orig_size=$(stat -c %s "$TEST_TMPDIR/base.mzf")
    "$MZF_PASTE" --insert --at 0 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" \
        -o "$TEST_TMPDIR/out.mzf" || return 1
    assert_file_exists "$TEST_TMPDIR/out.mzf" "output file created" || return 1
    local target_size
    target_size=$(stat -c %s "$TEST_TMPDIR/base.mzf")
    assert_eq "$target_size" "$orig_size" "target preserved with -o" || return 1
}

# T18. overflow > 65535: target ~64KB + input ~64KB > 65535 -> error
test_overflow_exceeds_max() {
    # Vytvoř target s body 60000 bajtů
    dd if=/dev/zero bs=1 count=60000 2>/dev/null | tr '\0' 'A' > "$TEST_TMPDIR/big_body.bin"
    "$BIN2MZF" -n BIG -l 0x1200 -i "$TEST_TMPDIR/big_body.bin" -o "$TEST_TMPDIR/big.mzf"
    # Patch o 10000 bajtů - výsledek 70000 > 65535
    dd if=/dev/zero bs=1 count=10000 2>/dev/null | tr '\0' 'B' > "$TEST_TMPDIR/p.bin"
    local err_out
    err_out=$("$MZF_PASTE" --insert --at 0 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/big.mzf" \
              -o "$TEST_TMPDIR/out.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "result body > 65535 must fail" || return 1
    assert_contains "$err_out" "exceed" "error mentions 'exceed'" || return 1
}

# T19. invalid target (truncated): rc != 0
test_invalid_target_truncated() {
    # Soubor menší než MZF hlavička (128B)
    head -c 50 /dev/zero > "$TEST_TMPDIR/bad.mzf"
    printf 'X' > "$TEST_TMPDIR/p.bin"
    local err_out
    err_out=$("$MZF_PASTE" --insert --at 0 "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/bad.mzf" \
              -o "$TEST_TMPDIR/out.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "truncated target must fail" || return 1
}

# T20. missing positional arguments: rc != 0
test_missing_positional() {
    "$MZF_PASTE" --insert --at 0 >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "no positional args must fail" || return 1
}

# T21. missing --at: rc != 0, error mentions --at
test_missing_at() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/base.mzf" || return 1
    printf 'X' > "$TEST_TMPDIR/p.bin"
    local err_out
    err_out=$("$MZF_PASTE" --insert "$TEST_TMPDIR/p.bin" "$TEST_TMPDIR/base.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "missing --at must fail" || return 1
    assert_contains "$err_out" "at" "error mentions --at" || return 1
}

run_test test_binary_exists
run_test test_help
run_test test_version
run_test test_lib_versions
run_test test_insert_at_zero
run_test test_insert_mid
run_test test_insert_at_end_numeric
run_test test_insert_at_end_keyword
run_test test_overwrite_mid
run_test test_overwrite_extend
run_test test_overwrite_past_end_no_extend
run_test test_gap_insert_default_filler
run_test test_gap_insert_custom_filler
run_test test_from_mzf
run_test test_mutex_insert_overwrite
run_test test_in_place_modify
run_test test_output_preserves_target
run_test test_overflow_exceeds_max
run_test test_invalid_target_truncated
run_test test_missing_positional
run_test test_missing_at

test_summary
