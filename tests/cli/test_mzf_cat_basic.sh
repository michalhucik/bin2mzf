#!/bin/bash
# Základní CLI test pro mzf-cat MVP (v0.1.0).
#
# Pokrývá: existence binárky, --help/--version/--lib-versions, round-trip
# 1/2/3 vstupů, --pad-between (mezera + výpočet + filler), --align-block,
# --align-to, mutex voleb, overflow > 65535, hard error pro -i, povinný
# positional, prázdný vstup, charset round-trip.

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

# 1. existence binárky
test_binary_exists() {
    if [[ -x "$MZF_CAT" || -x "$MZF_CAT.exe" ]]; then
        return 0
    fi
    echo "    FAIL: binary not found at $MZF_CAT"
    return 1
}

# 2. --help vypisuje Usage a vrací 0
test_help() {
    local out
    out=$("$MZF_CAT" --help 2>&1) || return 1
    assert_contains "$out" "Usage" "help contains Usage" || return 1
}

# 3. --version vrací 0 a obsahuje "mzf-cat 0.1.0"
test_version() {
    local out
    out=$("$MZF_CAT" --version 2>&1) || return 1
    assert_contains "$out" "mzf-cat 0.1.0" "version contains mzf-cat 0.1.0" || return 1
    assert_contains "$out" "bin2mzf-cli" "version contains release name" || return 1
}

# 4. --lib-versions obsahuje názvy všech knihoven
test_lib_versions() {
    local out
    out=$("$MZF_CAT" --lib-versions 2>&1) || return 1
    assert_contains "$out" "mzf" "lib-versions contains mzf" || return 1
    assert_contains "$out" "endianity" "lib-versions contains endianity" || return 1
    assert_contains "$out" "sharpmz_ascii" "lib-versions contains sharpmz_ascii" || return 1
}

# 5. základní 1 input round-trip přes mzf-strip
test_one_input_roundtrip() {
    printf 'HELLO' > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n SOLO -l 0x1200 -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    assert_file_exists "$TEST_TMPDIR/out.mzf" "output created" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    cmp "$TEST_TMPDIR/body.bin" "$TEST_TMPDIR/in.bin" || {
        echo "    FAIL: body bytes differ from single input"
        return 1
    }
}

# 6. 2 inputs - body je cat(in1, in2)
test_two_inputs_concat() {
    printf 'AAAA' > "$TEST_TMPDIR/a.bin"
    printf 'BBBB' > "$TEST_TMPDIR/b.bin"
    "$MZF_CAT" -n DUO -l 0x1200 -o "$TEST_TMPDIR/out.mzf" \
               "$TEST_TMPDIR/a.bin" "$TEST_TMPDIR/b.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    cat "$TEST_TMPDIR/a.bin" "$TEST_TMPDIR/b.bin" > "$TEST_TMPDIR/expected.bin"
    cmp "$TEST_TMPDIR/body.bin" "$TEST_TMPDIR/expected.bin" || {
        echo "    FAIL: body bytes differ from cat(a,b)"
        return 1
    }
}

# 7. 3 inputs - body je cat(in1, in2, in3)
test_three_inputs_concat() {
    printf 'XX' > "$TEST_TMPDIR/x.bin"
    printf 'YY' > "$TEST_TMPDIR/y.bin"
    printf 'ZZ' > "$TEST_TMPDIR/z.bin"
    "$MZF_CAT" -n TRIO -l 0x1200 -o "$TEST_TMPDIR/out.mzf" \
               "$TEST_TMPDIR/x.bin" "$TEST_TMPDIR/y.bin" "$TEST_TMPDIR/z.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    cat "$TEST_TMPDIR/x.bin" "$TEST_TMPDIR/y.bin" "$TEST_TMPDIR/z.bin" \
        > "$TEST_TMPDIR/expected.bin"
    cmp "$TEST_TMPDIR/body.bin" "$TEST_TMPDIR/expected.bin" || {
        echo "    FAIL: body bytes differ from cat(x,y,z)"
        return 1
    }
}

# 8. --pad-between 256 mezi 3 binárkami: mezi non-last je padding
test_pad_between_three_inputs() {
    # 3 binárky velikosti 100 bajtů => offsety: 0, 100+pad(156)=256, 256+100+pad(156)=512, 512+100=612
    dd if=/dev/zero bs=1 count=100 2>/dev/null | tr '\0' 'A' > "$TEST_TMPDIR/p1.bin"
    dd if=/dev/zero bs=1 count=100 2>/dev/null | tr '\0' 'B' > "$TEST_TMPDIR/p2.bin"
    dd if=/dev/zero bs=1 count=100 2>/dev/null | tr '\0' 'C' > "$TEST_TMPDIR/p3.bin"
    "$MZF_CAT" -n PAD -l 0x1200 --pad-between 256 \
               -o "$TEST_TMPDIR/out.mzf" \
               "$TEST_TMPDIR/p1.bin" "$TEST_TMPDIR/p2.bin" "$TEST_TMPDIR/p3.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    # 100 + 156 + 100 + 156 + 100 = 612
    assert_eq "$sz" "612" "pad-between 256 with 3x100 -> 612 bytes" || return 1
    # offset 100 by měl být 0x00 (filler default)
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 100 1)
    assert_eq "$b" "00" "filler default 0x00 at offset 100" || return 1
    # offset 256 by měl být 'B' (0x42)
    b=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 256 1)
    assert_eq "$b" "42" "second input starts at offset 256" || return 1
}

# 9. --pad-between výpočet: vstup 100 -> padding 156 -> druhý začíná na 256
test_pad_between_calculation() {
    dd if=/dev/zero bs=1 count=100 2>/dev/null | tr '\0' 'A' > "$TEST_TMPDIR/p1.bin"
    printf 'B' > "$TEST_TMPDIR/p2.bin"
    "$MZF_CAT" -n CALC -l 0x1200 --pad-between 256 \
               -o "$TEST_TMPDIR/out.mzf" \
               "$TEST_TMPDIR/p1.bin" "$TEST_TMPDIR/p2.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    # 100 + 156 (padding) + 1 = 257
    assert_eq "$sz" "257" "100 + pad + 1 = 257" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 256 1)
    assert_eq "$b" "42" "second input ('B' = 0x42) at offset 256" || return 1
}

# 10. --pad-between 256 --filler 0xFF: padding obsahuje 0xFF
test_pad_between_filler_ff() {
    dd if=/dev/zero bs=1 count=100 2>/dev/null | tr '\0' 'A' > "$TEST_TMPDIR/p1.bin"
    printf 'B' > "$TEST_TMPDIR/p2.bin"
    "$MZF_CAT" -n FILL -l 0x1200 --pad-between 256 --filler 0xFF \
               -o "$TEST_TMPDIR/out.mzf" \
               "$TEST_TMPDIR/p1.bin" "$TEST_TMPDIR/p2.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 100 1)
    assert_eq "$b" "ff" "filler 0xFF at offset 100" || return 1
    b=$(read_hex_bytes "$TEST_TMPDIR/body.bin" 255 1)
    assert_eq "$b" "ff" "filler 0xFF at offset 255 (last padding byte)" || return 1
}

# 11. --align-block 512: tělo zarovnáno na násobek 512
test_align_block_512() {
    printf 'X%.0s' $(seq 1 100) > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n ALB -l 0x1200 --align-block 512 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    assert_eq "$sz" "512" "align-block 512 with 100B input -> 512" || return 1
}

# 12. --align-to 1024: tělo přesně 1024 bajtů
test_align_to_1024() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n ALT -l 0x1200 --align-to 1024 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/body.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/body.bin")
    assert_eq "$sz" "1024" "align-to 1024 -> 1024" || return 1
}

# 13. mutex --align-block × --align-to
test_mutex_align_block_align_to() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n M1 -l 0x1200 --align-block 256 --align-to 512 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "--align-block + --align-to must fail" || return 1
}

# 14. mutex --size × --align-block
test_mutex_size_align_block() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n M2 -l 0x1200 -s 256 --align-block 128 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "--size + --align-block must fail" || return 1
}

# 15. overflow > 65535: 2 vstupy po 40000 bajtech -> 80000
test_overflow_exceeds_max() {
    dd if=/dev/zero bs=1 count=40000 2>/dev/null > "$TEST_TMPDIR/big1.bin"
    dd if=/dev/zero bs=1 count=40000 2>/dev/null > "$TEST_TMPDIR/big2.bin"
    local err_out
    err_out=$("$MZF_CAT" -n OVR -l 0x1200 \
                         -o "$TEST_TMPDIR/out.mzf" \
                         "$TEST_TMPDIR/big1.bin" "$TEST_TMPDIR/big2.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "two 40k inputs (80k) must fail" || return 1
    assert_contains "$err_out" "exceed" "error mentions 'exceed'" || return 1
}

# 16. -i je hard error (variadic positional je standard, getopt unknown option)
test_dash_i_unknown() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n DI -l 0x1200 -i "$TEST_TMPDIR/in.bin" \
               -o "$TEST_TMPDIR/out.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "-i must fail (not in long_options)" || return 1
}

# 17. žádný positional -> error "at least one"
test_no_positional_fails() {
    local err_out
    err_out=$("$MZF_CAT" -n NP -l 0x1200 -o "$TEST_TMPDIR/out.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "no positional must fail" || return 1
    assert_contains "$err_out" "at least one" "error mentions 'at least one'" || return 1
}

# 18. empty input file je validní (0 bajtů body)
test_empty_input_valid() {
    : > "$TEST_TMPDIR/empty.bin"
    "$MZF_CAT" -n EMP -l 0x1200 -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/empty.bin" || return 1
    local mzf_size
    mzf_size=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$mzf_size" "128" "empty input -> mzf is just header (128B)" || return 1
}

# 19. charset eu round-trip: name "TEST" se uloží jako EU bajty a mzf-info je přečte zpět
test_charset_eu_roundtrip() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$MZF_CAT" -n TEST -l 0x1200 -C eu \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local info_out
    info_out=$("$MZF_INFO" --charset eu "$TEST_TMPDIR/out.mzf" 2>&1) || return 1
    assert_contains "$info_out" "TEST" "mzf-info shows 'TEST' name" || return 1
}

run_test test_binary_exists
run_test test_help
run_test test_version
run_test test_lib_versions
run_test test_one_input_roundtrip
run_test test_two_inputs_concat
run_test test_three_inputs_concat
run_test test_pad_between_three_inputs
run_test test_pad_between_calculation
run_test test_pad_between_filler_ff
run_test test_align_block_512
run_test test_align_to_1024
run_test test_mutex_align_block_align_to
run_test test_mutex_size_align_block
run_test test_overflow_exceeds_max
run_test test_dash_i_unknown
run_test test_no_positional_fails
run_test test_empty_input_valid
run_test test_charset_eu_roundtrip

test_summary
