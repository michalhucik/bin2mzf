#!/bin/bash
# CLI test pro mzf-info v0.1.0+ - dekódování CP/M konvence (SOKODI CMT.COM).
#
# Pokrývá: round-trip přes bin2mzf --cpm, paralelní CP/M sekci pod raw
# bytes fname (CP/M name / ext / attrs), symbolický ftype "CPM" pro 0x22,
# atributové bity (R/O, SYS, ARC, kombinace, none), default přípona COM,
# rtrim trailing mezer u jména a přípony, toggle --no-cpm-decode, silent
# fallback pro non-CP/M MZF, ortogonálnost vůči --validate.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vyrobí binární soubor zadané velikosti naplněný 0xAA.
make_input() {
    local path="$1"
    local size="$2"
    head -c "$size" /dev/zero | tr '\0' '\252' > "$path"
}

# T1. základní CP/M dekódování: --cpm-name HELLO -> CP/M name + ext + COM
test_cpm_basic_decode() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/t1.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/t1.mzf" 2>&1) || return 1
    assert_contains "$out" "CP/M name:" "section header CP/M name present" || return 1
    assert_contains "$out" "HELLO" "CP/M name HELLO present" || return 1
    assert_contains "$out" "CP/M ext:" "section header CP/M ext present" || return 1
    assert_contains "$out" "COM" "CP/M ext COM present" || return 1
}

# T2. ftype symbol: 0x22 -> "CPM"
test_cpm_ftype_symbol() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/t2.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/t2.mzf" 2>&1) || return 1
    assert_contains "$out" "0x22 (CPM)" "ftype 0x22 maps to CPM symbol" || return 1
}

# T3. R/O atribut: --cpm-attr ro -> "R/O" v attrs, ne SYS ani ARC
test_cpm_attr_ro() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr ro \
               -o "$TEST_TMPDIR/t3.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out attrs
    out=$("$MZF_INFO" "$TEST_TMPDIR/t3.mzf" 2>&1) || return 1
    attrs=$(echo "$out" | grep "^CP/M attrs:")
    assert_contains "$attrs" "R/O" "attrs contains R/O" || return 1
    assert_not_contains "$attrs" "SYS" "attrs does not contain SYS" || return 1
    assert_not_contains "$attrs" "ARC" "attrs does not contain ARC" || return 1
}

# T4. SYS atribut: --cpm-attr sys -> "SYS"
test_cpm_attr_sys() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr sys \
               -o "$TEST_TMPDIR/t4.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out attrs
    out=$("$MZF_INFO" "$TEST_TMPDIR/t4.mzf" 2>&1) || return 1
    attrs=$(echo "$out" | grep "^CP/M attrs:")
    assert_contains "$attrs" "SYS" "attrs contains SYS" || return 1
    assert_not_contains "$attrs" "R/O" "attrs does not contain R/O" || return 1
    assert_not_contains "$attrs" "ARC" "attrs does not contain ARC" || return 1
}

# T5. ARC atribut: --cpm-attr arc -> "ARC"
test_cpm_attr_arc() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr arc \
               -o "$TEST_TMPDIR/t5.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out attrs
    out=$("$MZF_INFO" "$TEST_TMPDIR/t5.mzf" 2>&1) || return 1
    attrs=$(echo "$out" | grep "^CP/M attrs:")
    assert_contains "$attrs" "ARC" "attrs contains ARC" || return 1
    assert_not_contains "$attrs" "R/O" "attrs does not contain R/O" || return 1
    assert_not_contains "$attrs" "SYS" "attrs does not contain SYS" || return 1
}

# T6. kombinace atributů: --cpm-attr ro,sys,arc -> "R/O,SYS,ARC"
test_cpm_attr_combined() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr ro,sys,arc \
               -o "$TEST_TMPDIR/t6.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out attrs
    out=$("$MZF_INFO" "$TEST_TMPDIR/t6.mzf" 2>&1) || return 1
    attrs=$(echo "$out" | grep "^CP/M attrs:")
    assert_contains "$attrs" "R/O,SYS,ARC" "attrs combined R/O,SYS,ARC" || return 1
}

# T7. žádné atributy: --cpm bez --cpm-attr -> "(none)"
test_cpm_attr_none() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/t7.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out attrs
    out=$("$MZF_INFO" "$TEST_TMPDIR/t7.mzf" 2>&1) || return 1
    attrs=$(echo "$out" | grep "^CP/M attrs:")
    assert_contains "$attrs" "(none)" "attrs (none) when no attribute set" || return 1
}

# T8. default přípona: bez --cpm-ext -> ext "COM"
test_cpm_default_ext_com() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/t8.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out ext
    out=$("$MZF_INFO" "$TEST_TMPDIR/t8.mzf" 2>&1) || return 1
    ext=$(echo "$out" | grep "^CP/M ext:")
    assert_contains "$ext" "\"COM\"" "default ext is COM in quotes" || return 1
}

# T9. krátké jméno: --cpm-name AB -> "AB" (rtrim, ne "AB      ")
test_cpm_short_name() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name AB \
               -o "$TEST_TMPDIR/t9.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out name
    out=$("$MZF_INFO" "$TEST_TMPDIR/t9.mzf" 2>&1) || return 1
    name=$(echo "$out" | grep "^CP/M name:")
    assert_contains "$name" "\"AB\"" "short name AB rtrimmed (no padding)" || return 1
}

# T10. krátká přípona: --cpm-ext C -> "C" (rtrim po strip bit 7)
test_cpm_short_ext() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-ext C \
               -o "$TEST_TMPDIR/t10.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out ext
    out=$("$MZF_INFO" "$TEST_TMPDIR/t10.mzf" 2>&1) || return 1
    ext=$(echo "$out" | grep "^CP/M ext:")
    assert_contains "$ext" "\"C\"" "short ext C rtrimmed (no padding)" || return 1
}

# T11. --no-cpm-decode skryje CP/M sekci, Name: zůstane
test_cpm_no_cpm_decode_flag() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/t11.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" --no-cpm-decode "$TEST_TMPDIR/t11.mzf" 2>&1) || return 1
    assert_not_contains "$out" "CP/M name:" "--no-cpm-decode hides CP/M section" || return 1
    assert_contains "$out" "Name:" "Name: line still present" || return 1
}

# T12. non-CP/M MZF (ftype 0x01) -> žádná CP/M sekce, exit 0
test_cpm_non_sokodi_silent() {
    make_input "$TEST_TMPDIR/in.bin" 16
    "$BIN2MZF" -n FOO -l 0x1200 \
               -o "$TEST_TMPDIR/t12.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" "$TEST_TMPDIR/t12.mzf" 2>&1) || return 1
    assert_not_contains "$out" "CP/M name:" "non-CP/M MZF has no CP/M section" || return 1
}

# T13. --validate u CP/M MZF -> rc=0, prázdný stdout
test_cpm_validate_unaffected() {
    make_input "$TEST_TMPDIR/in.bin" 64
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr ro,sys \
               -o "$TEST_TMPDIR/t13.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local out
    out=$("$MZF_INFO" --validate "$TEST_TMPDIR/t13.mzf" 2>/dev/null)
    local rc=$?
    assert_eq "$rc" "0" "--validate returns 0 for valid CP/M MZF" || return 1
    assert_eq "$out" "" "--validate produces empty stdout" || return 1
}

run_test test_cpm_basic_decode
run_test test_cpm_ftype_symbol
run_test test_cpm_attr_ro
run_test test_cpm_attr_sys
run_test test_cpm_attr_arc
run_test test_cpm_attr_combined
run_test test_cpm_attr_none
run_test test_cpm_default_ext_com
run_test test_cpm_short_name
run_test test_cpm_short_ext
run_test test_cpm_no_cpm_decode_flag
run_test test_cpm_non_sokodi_silent
run_test test_cpm_validate_unaffected

test_summary
