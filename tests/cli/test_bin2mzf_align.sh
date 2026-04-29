#!/bin/bash
# CLI test pro bin2mzf v0.2.0 - align/size/filler.
#
# Pokrývá: --align-block (padding na násobek), --align-to (truncate i pad),
# --filler (custom byte hex i decimal), -s/--size (alias k --align-to),
# --no-auto-size + --size (fixní fsize v hlavičce), mutual exclusion errory,
# limity hodnot (0, > 65535).

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vyčte N hex bajtů od offsetu OFF z BIN ve formátu
# odděleném mezerami.
read_hex_bytes() {
    local file="$1"
    local off="$2"
    local n="$3"
    od -An -v -tx1 -j"$off" -N"$n" "$file" | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'
}

# Pomocná funkce: vyrobí binární soubor zadané velikosti naplněný 0xAA.
# Důvod 0xAA: snadno odlišitelné od defaultního filleru 0x00 i od 0xFF.
make_input() {
    local path="$1"
    local size="$2"
    head -c "$size" /dev/zero | tr '\0' '\252' > "$path"
}

# 1. --align-block 256 na 100B vstup -> 256B body, last 156B = 0x00
test_align_block_256_padding() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" -n A -l 0x1000 --align-block 256 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    # výsledný MZF = 128B header + 256B body = 384B
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "384" "MZF file size = 128 + 256" || return 1
    # body offset 128, prvních 100 bajtů musí být 0xAA, posledních 156 = 0x00
    local first
    first=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 128 1)
    assert_eq "$first" "aa" "first body byte = 0xAA (input)" || return 1
    local last
    last=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 383 1)
    assert_eq "$last" "00" "last body byte = 0x00 (default filler)" || return 1
    # verifikace, že 100. byte = 0xAA, 101. = 0x00
    local boundary
    boundary=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 227 2)
    assert_eq "$boundary" "aa 00" "boundary 100/101 = aa 00" || return 1
}

# 2. --align-block 256 na 256B vstup -> 256B (no change)
test_align_block_already_aligned() {
    make_input "$TEST_TMPDIR/in.bin" 256
    "$BIN2MZF" -n A -l 0x1000 --align-block 256 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "384" "256B already aligned, no padding" || return 1
}

# 3. --align-to 1024 na 100B vstup -> 1024B body, padding 0x00
test_align_to_pad() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" -n A -l 0x1000 --align-to 1024 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "1152" "MZF size = 128 + 1024" || return 1
    local last
    last=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1151 1)
    assert_eq "$last" "00" "last byte = 0x00 padding" || return 1
}

# 4. --align-to 100 na 1000B vstup -> 100B, warning obsahuje "truncated"
test_align_to_truncate() {
    make_input "$TEST_TMPDIR/in.bin" 1000
    local err
    err=$("$BIN2MZF" -n A -l 0x1000 --align-to 100 \
                     -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1) || return 1
    assert_contains "$err" "truncated" "warning contains 'truncated'" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "228" "MZF size = 128 + 100" || return 1
}

# 5. --align-to 100 na 100B vstup -> 100B (no change)
test_align_to_equal() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" -n A -l 0x1000 --align-to 100 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "228" "100 == 100, no change" || return 1
}

# 6. --filler 0xFF s --align-block 256 -> padding 0xFF
test_filler_0xff() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" -n A -l 0x1000 --align-block 256 --filler 0xFF \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local last
    last=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 383 1)
    assert_eq "$last" "ff" "filler 0xFF used for padding" || return 1
    # boundary check: 100. (offset 227) = 0xAA, 101. (228) = 0xFF
    local boundary
    boundary=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 227 2)
    assert_eq "$boundary" "aa ff" "boundary 100/101 = aa ff" || return 1
}

# 7. --filler 255 (decimal) ekvivalentní 0xFF
test_filler_decimal() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" -n A -l 0x1000 --align-block 256 --filler 255 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local last
    last=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 383 1)
    assert_eq "$last" "ff" "decimal 255 = 0xFF filler" || return 1
}

# 8. -s 256 (alias pro --align-to 256) na 100B vstup -> 256B body
test_size_alias_align_to() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" -n A -l 0x1000 -s 256 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "384" "-s 256 acts as --align-to 256" || return 1
}

# 9. --no-auto-size --size 100 na 200B vstup -> body 100B (truncate),
# fsize bytes = "64 00" (100 LE)
test_no_auto_size_with_size() {
    make_input "$TEST_TMPDIR/in.bin" 200
    "$BIN2MZF" -n A -l 0x1000 --size 100 --no-auto-size \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "228" "MZF size = 128 + 100 (truncated)" || return 1
    # fsize na offsetu 0x12 = 18, 2 bajty LE
    local fs
    fs=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 18 2)
    assert_eq "$fs" "64 00" "fsize = 100 (0x64) LE" || return 1
}

# 10. --align-block + --align-to současně -> error
test_align_block_and_to_conflict() {
    make_input "$TEST_TMPDIR/in.bin" 100
    local err
    err=$("$BIN2MZF" -n A -l 0x1000 --align-block 256 --align-to 256 \
                     -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "mutual exclusion: align-block + align-to" || return 1
    assert_contains "$err" "mutually exclusive" "stderr mentions mutual exclusion" || return 1
}

# 11. --size + --align-to současně -> error
test_size_and_align_to_conflict() {
    make_input "$TEST_TMPDIR/in.bin" 100
    local err
    err=$("$BIN2MZF" -n A -l 0x1000 --size 100 --align-to 200 \
                     -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "mutual exclusion: size + align-to" || return 1
}

# 12. --align-block 0 -> error (validace v parse_align_value)
test_align_block_invalid_zero() {
    make_input "$TEST_TMPDIR/in.bin" 100
    local err
    err=$("$BIN2MZF" -n A -l 0x1000 --align-block 0 \
                     -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "align-block 0 must fail" || return 1
}

# 13. --align-to 70000 -> error (overflow nad MZF_MAX_BODY_SIZE)
test_align_overflow_above_64k() {
    make_input "$TEST_TMPDIR/in.bin" 100
    local err
    err=$("$BIN2MZF" -n A -l 0x1000 --align-to 70000 \
                     -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "align-to 70000 must fail" || return 1
}

run_test test_align_block_256_padding
run_test test_align_block_already_aligned
run_test test_align_to_pad
run_test test_align_to_truncate
run_test test_align_to_equal
run_test test_filler_0xff
run_test test_filler_decimal
run_test test_size_alias_align_to
run_test test_no_auto_size_with_size
run_test test_align_block_and_to_conflict
run_test test_size_and_align_to_conflict
run_test test_align_block_invalid_zero
run_test test_align_overflow_above_64k

test_summary
