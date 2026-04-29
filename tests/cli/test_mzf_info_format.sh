#!/bin/bash
# CLI test pro mzf-info v0.2.0+ - --format text|json|csv.
#
# Pokrývá: default `text` (zpětná kompatibilita), explicit `--format text`
# (byte-identical s default), JSON validní výstup a klíče (ftype, name,
# fstrt, valid, ftype_symbol, ...), CSV řádky `key,value`, neznámý formát
# selže, hard error pro `--hexdump` a `--body-only` v JSON/CSV módu,
# silent ignore `--format` při `--validate`, CP/M klíče v JSON, validation
# klíče u truncated MZF, lib-versions obsahuje output_format.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vytvoří validní OBJ MZF s 8 bajty body (FOO @ 0x1200).
make_valid_mzf() {
    printf 'ABCDEFGH' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n FOO -l 0x1200 -e 0x1200 -c "Hello" \
               -o "$1" "$TEST_TMPDIR/in.bin"
}

# 1. Default je text (žádný --format) - obsahuje řádek "File type:"
test_format_default_is_text() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    assert_contains "$out" "File type:" "default output contains 'File type:'" || return 1
}

# 2. Explicit `--format text` musí být byte-identical s default (žádný flag)
test_format_text_explicit() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    "$MZF_INFO" "$TEST_TMPDIR/v.mzf" > "$TEST_TMPDIR/a.txt" 2>&1 || return 1
    "$MZF_INFO" --format text "$TEST_TMPDIR/v.mzf" > "$TEST_TMPDIR/b.txt" 2>&1 || return 1
    if ! diff -q "$TEST_TMPDIR/a.txt" "$TEST_TMPDIR/b.txt" >/dev/null; then
        echo "    FAIL: --format text differs from default text output"
        diff "$TEST_TMPDIR/a.txt" "$TEST_TMPDIR/b.txt" | head -20
        return 1
    fi
    return 0
}

# 3. `--format json` produkuje validní JSON objekt {...}
test_format_json_valid() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    assert_contains "$out" "{" "JSON output starts with {" || return 1
    assert_contains "$out" "}" "JSON output ends with }" || return 1
}

# 4. JSON má klíče ftype, ftype_symbol, name, fsize, fstrt, fexec, comment, valid
test_format_json_keys() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    assert_contains "$out" '"ftype"'        "JSON has ftype key"        || return 1
    assert_contains "$out" '"ftype_symbol"' "JSON has ftype_symbol key" || return 1
    assert_contains "$out" '"name"'         "JSON has name key"         || return 1
    assert_contains "$out" '"fsize"'        "JSON has fsize key"        || return 1
    assert_contains "$out" '"fstrt"'        "JSON has fstrt key"        || return 1
    assert_contains "$out" '"fexec"'        "JSON has fexec key"        || return 1
    assert_contains "$out" '"comment"'      "JSON has comment key"      || return 1
    assert_contains "$out" '"valid"'        "JSON has valid key"        || return 1
}

# 5. JSON ftype hodnota = 1 pro OBJ MZF
test_format_json_ftype_value() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    assert_json_eq "$out" "ftype" "1" "JSON ftype == 1" || return 1
}

# 6. JSON ftype_symbol == "OBJ" pro default MZF
test_format_json_ftype_symbol() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    assert_json_eq "$out" "ftype_symbol" "OBJ" "JSON ftype_symbol == OBJ" || return 1
}

# 7. JSON fstrt jako string "0x1200"
test_format_json_fstrt_hex() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    assert_json_eq "$out" "fstrt" "0x1200" "JSON fstrt == 0x1200" || return 1
}

# 8. CSV výstup: první řádek = "key,value", exit 0
test_format_csv_valid() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out first
    out=$("$MZF_INFO" --format csv "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    first=$(echo "$out" | head -1)
    assert_eq "$first" "key,value" "CSV first line == 'key,value'" || return 1
}

# 9. CSV obsahuje klíčové páry: ftype, fstrt, valid
test_format_csv_kv_pairs() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --format csv "$TEST_TMPDIR/v.mzf" 2>&1) || return 1
    # Pomocí grep '^prefix,' aby řádek opravdu byl ve formátu key,value
    if ! echo "$out" | grep -q '^ftype,1$';     then echo "    FAIL: missing 'ftype,1'";       return 1; fi
    if ! echo "$out" | grep -q '^fstrt,0x1200$'; then echo "    FAIL: missing 'fstrt,0x1200'"; return 1; fi
    if ! echo "$out" | grep -q '^valid,true$';   then echo "    FAIL: missing 'valid,true'";    return 1; fi
    return 0
}

# 10. Neznámý formát: --format xml -> exit 1, stderr "invalid --format value"
test_format_unknown_error() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local err
    err=$("$MZF_INFO" --format xml "$TEST_TMPDIR/v.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "unknown format must fail" || return 1
    assert_contains "$err" "invalid --format value" "stderr contains 'invalid --format value'" || return 1
}

# 11. --format json + --hexdump -> exit 1, stderr "is incompatible with --format"
test_format_json_with_hexdump_error() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local err
    err=$("$MZF_INFO" --format json --hexdump "$TEST_TMPDIR/v.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "json+hexdump must fail" || return 1
    assert_contains "$err" "is incompatible with --format" "stderr contains 'is incompatible with --format'" || return 1
}

# 12. --format csv + --body-only -> exit 1, stderr "is incompatible with --format"
test_format_csv_with_body_only_error() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local err
    err=$("$MZF_INFO" --format csv --body-only "$TEST_TMPDIR/v.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "csv+body-only must fail" || return 1
    assert_contains "$err" "is incompatible with --format" "stderr contains 'is incompatible with --format'" || return 1
}

# 13. --validate + --format json: silent, exit 0 pro validní MZF (validate má precedenci)
test_format_json_with_validate_silent() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    local out
    out=$("$MZF_INFO" --validate --format json "$TEST_TMPDIR/v.mzf" 2>/dev/null)
    local rc=$?
    assert_eq "$rc" "0" "validate+format json: exit 0 for valid MZF" || return 1
    assert_eq "$out" "" "validate+format json: stdout empty (validate has precedence)" || return 1
}

# 14. JSON pro CP/M MZF (ftype=0x22, SOKODI layout) má cpm_* klíče
test_format_json_cpm_keys() {
    printf 'ABCDEFGH' > "$TEST_TMPDIR/cpm.bin"
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr ro \
               -o "$TEST_TMPDIR/cpm.mzf" "$TEST_TMPDIR/cpm.bin" || return 1
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/cpm.mzf" 2>&1) || return 1
    assert_contains "$out" '"cpm_name"'     "JSON has cpm_name key"     || return 1
    assert_contains "$out" '"cpm_ext"'      "JSON has cpm_ext key"      || return 1
    assert_contains "$out" '"cpm_attr_ro"'  "JSON has cpm_attr_ro key"  || return 1
    assert_contains "$out" '"cpm_attr_sys"' "JSON has cpm_attr_sys key" || return 1
    assert_contains "$out" '"cpm_attr_arc"' "JSON has cpm_attr_arc key" || return 1
    assert_contains "$out" '"cpm_attrs"'    "JSON has cpm_attrs key"    || return 1
    assert_json_eq  "$out" "cpm_name" "HELLO" "JSON cpm_name == HELLO"   || return 1
    assert_json_eq  "$out" "cpm_ext"  "COM"   "JSON cpm_ext == COM"      || return 1
    assert_json_eq  "$out" "cpm_attr_ro"  "true"  "JSON cpm_attr_ro == true"  || return 1
    assert_json_eq  "$out" "cpm_attr_sys" "false" "JSON cpm_attr_sys == false" || return 1
}

# 15. Truncated body MZF -> JSON má size_match=truncated, valid=false
test_format_json_truncated_validation() {
    make_valid_mzf "$TEST_TMPDIR/v.mzf" || return 1
    # Originál má 128 + 8 = 136 bajtů. Ořežeme na 130 (hlavička + 2 bajty body
    # < fsize=8) - validation musí říct "truncated".
    head -c 130 "$TEST_TMPDIR/v.mzf" > "$TEST_TMPDIR/trunc.mzf"
    local out
    out=$("$MZF_INFO" --format json "$TEST_TMPDIR/trunc.mzf" 2>&1) || return 1
    assert_json_eq "$out" "size_match" "truncated" "JSON size_match == truncated" || return 1
    assert_json_eq "$out" "valid"      "false"     "JSON valid == false"          || return 1
}

# 16. --lib-versions obsahuje output_format
test_format_lib_versions_includes_outfmt() {
    local out
    out=$("$MZF_INFO" --lib-versions 2>&1) || return 1
    assert_contains "$out" "output_format" "lib-versions contains output_format" || return 1
}

run_test test_format_default_is_text
run_test test_format_text_explicit
run_test test_format_json_valid
run_test test_format_json_keys
run_test test_format_json_ftype_value
run_test test_format_json_ftype_symbol
run_test test_format_json_fstrt_hex
run_test test_format_csv_valid
run_test test_format_csv_kv_pairs
run_test test_format_unknown_error
run_test test_format_json_with_hexdump_error
run_test test_format_csv_with_body_only_error
run_test test_format_json_with_validate_silent
run_test test_format_json_cpm_keys
run_test test_format_json_truncated_validation
run_test test_format_lib_versions_includes_outfmt

test_summary
