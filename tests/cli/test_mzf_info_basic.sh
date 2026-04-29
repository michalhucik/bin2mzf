#!/bin/bash
# Základní CLI test pro mzf-info MVP.
#
# Pokrývá: existence binárky, --help/--version/--lib-versions,
# round-trip přes bin2mzf -> mzf-info (file i stdin), výpis hlavičky,
# --header-only, --body-only, --hexdump s --offset/--length, charset
# variance (jp, none), mutual exclusion a out-of-range chyby.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# 1. existence binárky
test_binary_exists() {
    if [[ -x "$MZF_INFO" || -x "$MZF_INFO.exe" ]]; then
        return 0
    fi
    echo "    FAIL: binary not found at $MZF_INFO"
    return 1
}

# 2. --help vypisuje Usage a vrací 0
test_help() {
    local out
    out=$("$MZF_INFO" --help 2>&1) || return 1
    assert_contains "$out" "Usage" "help contains Usage" || return 1
}

# 3. --version vrací 0 a obsahuje verzi
test_version() {
    local out
    out=$("$MZF_INFO" --version 2>&1) || return 1
    assert_contains "$out" "0.2.0" "version contains 0.2.0" || return 1
    assert_contains "$out" "bin2mzf-cli" "version contains release name" || return 1
}

# 4. --lib-versions obsahuje názvy všech knihoven
test_lib_versions() {
    local out
    out=$("$MZF_INFO" --lib-versions 2>&1) || return 1
    assert_contains "$out" "mzf" "lib-versions contains mzf" || return 1
    assert_contains "$out" "endianity" "lib-versions contains endianity" || return 1
    assert_contains "$out" "sharpmz_ascii" "lib-versions contains sharpmz_ascii" || return 1
}

# 5. neexistující soubor selže
test_nonexistent_file_fails() {
    local err
    err=$("$MZF_INFO" "$TEST_TMPDIR/does_not_exist.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "nonexistent file must fail" || return 1
    assert_contains "$err" "Error" "stderr contains Error" || return 1
}

# 6. round-trip file path: bin2mzf -> mzf-info, výstup obsahuje jméno a load addr
test_roundtrip_file() {
    printf 'HELLO!!!' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n FOO -l 0x1200 -o "$TEST_TMPDIR/foo.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/foo.mzf" 2>&1) || return 1
    assert_contains "$out" "FOO" "output contains name FOO" || return 1
    assert_contains "$out" "0x1200" "output contains load addr 0x1200" || return 1
    assert_contains "$out" "OBJ" "output contains symbolic ftype OBJ" || return 1
}

# 7. round-trip přes stdin pipe
test_roundtrip_stdin() {
    printf 'WORLD' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n BAR -l 0x100 -o "$TEST_TMPDIR/bar.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" < "$TEST_TMPDIR/bar.mzf" 2>&1) || return 1
    assert_contains "$out" "BAR" "stdin: output contains name BAR" || return 1
}

# 8. --header-only neobsahuje hexdump řádky
test_header_only_excludes_hexdump() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n HO -l 0x1200 -o "$TEST_TMPDIR/ho.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" --header-only "$TEST_TMPDIR/ho.mzf" 2>&1) || return 1
    assert_contains "$out" "File type:" "header-only contains File type" || return 1
    # hexdump řádek začíná 8 hex znaků a mezerou - hledáme typický pattern "00000000 "
    assert_not_contains "$out" "^00000000  " "header-only must not contain hexdump line" || return 1
}

# 9. --body-only neobsahuje header text
test_body_only_excludes_header() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n BO -l 0x1200 -o "$TEST_TMPDIR/bo.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" --body-only "$TEST_TMPDIR/bo.mzf" 2>&1) || return 1
    assert_not_contains "$out" "File type:" "body-only must not contain File type" || return 1
    assert_contains "$out" "00000000" "body-only contains hexdump offset" || return 1
}

# 10. --hexdump kombinace - obsahuje header text I hexdump
test_hexdump_combined() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n HD -l 0x1200 -o "$TEST_TMPDIR/hd.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" --hexdump "$TEST_TMPDIR/hd.mzf" 2>&1) || return 1
    assert_contains "$out" "File type:" "hexdump combined contains header" || return 1
    assert_contains "$out" "00000000" "hexdump combined contains hex offset" || return 1
}

# 11. --hexdump --offset --length: omezený rozsah
test_hexdump_offset_length() {
    # 32 bajtů těla, takže rozsahy 0..31 jsou validní
    printf '\x00%.0s' $(seq 1 32) > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n RNG -l 0x1200 -o "$TEST_TMPDIR/rng.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    # Po --body-only je first hex line "00000000  ..." pokud offset 0
    out=$("$MZF_INFO" --body-only --offset 0x10 --length 0x10 "$TEST_TMPDIR/rng.mzf" 2>&1) || return 1
    assert_contains "$out" "00000010" "hexdump line starts at offset 0x10" || return 1
}

# 12. --charset jp - rc=0 pro ASCII jméno
test_charset_jp() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n JP -l 0x1200 -o "$TEST_TMPDIR/jp.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_INFO" --charset jp "$TEST_TMPDIR/jp.mzf" >/dev/null 2>&1 || return 1
}

# 13. --charset none - výstup neobsahuje "(utf8:" a ani neselže
test_charset_none() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n NN -l 0x1200 -o "$TEST_TMPDIR/nn.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" --charset none "$TEST_TMPDIR/nn.mzf" 2>&1) || return 1
    # V none režimu vypisujeme jen raw bajty, žádné UTF-8
    assert_contains "$out" "raw bytes only" "charset none: marker raw bytes only" || return 1
}

# 14. mutual exclusion --header-only + --body-only -> error
test_mutual_exclusion() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n ME -l 0x1200 -o "$TEST_TMPDIR/me.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_INFO" --header-only --body-only "$TEST_TMPDIR/me.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "header-only + body-only must fail" || return 1
}

# 15. --offset/--length out of body range -> error
test_offset_length_out_of_range() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    # body = 1 bajt, požadujeme offset 1, length 1000 -> off+len = 1001 > 1
    "$BIN2MZF" -n OOR -l 0x1200 -o "$TEST_TMPDIR/oor.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_INFO" --body-only --offset 1 --length 1000 "$TEST_TMPDIR/oor.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "out-of-range offset/length must fail" || return 1
}

run_test test_binary_exists
run_test test_help
run_test test_version
run_test test_lib_versions
run_test test_nonexistent_file_fails
run_test test_roundtrip_file
run_test test_roundtrip_stdin
run_test test_header_only_excludes_hexdump
run_test test_body_only_excludes_header
run_test test_hexdump_combined
run_test test_hexdump_offset_length
run_test test_charset_jp
run_test test_charset_none
run_test test_mutual_exclusion
run_test test_offset_length_out_of_range

test_summary
