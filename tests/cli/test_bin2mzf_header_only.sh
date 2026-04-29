#!/bin/bash
# CLI test pro bin2mzf v0.2.0 - --header-only.
#
# Pokrývá: výstup přesně 128B, ignorování vstupu (positional / -i),
# default fsize=0 (auto-size na prázdné tělo), správnost pole fname,
# kombinace s --no-auto-size + --size (fixní fsize bez fyzických dat),
# warning při --header-only s positional argumentem.

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

# 1. Základní --header-only -> výstup přesně 128B
test_header_only_size_128() {
    "$BIN2MZF" --header-only -n FOO -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" </dev/null || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "128" "header-only output exactly 128 bytes" || return 1
}

# 2. --header-only s reálným vstupem - vstup ignorován, stále 128B
test_header_only_ignores_input() {
    head -c 1024 /dev/zero | tr '\0' '\xAA' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" --header-only -n FOO -l 0x1200 \
               -i "$TEST_TMPDIR/in.bin" \
               -o "$TEST_TMPDIR/out.mzf" 2>/dev/null || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "128" "1KB input ignored, output still 128B" || return 1
}

# 3. Default --header-only -> fsize = 0 (auto-size aplikuje 0 na prázdné tělo)
test_header_only_fsize_zero() {
    "$BIN2MZF" --header-only -n FOO -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" </dev/null || return 1
    # fsize na offsetu 0x12 = 18, 2 bajty LE
    local fs
    fs=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 18 2)
    assert_eq "$fs" "00 00" "default header-only fsize = 0" || return 1
}

# 4. fname obsahuje "FOO" + 0x0D terminátor
test_header_only_fname_correct() {
    "$BIN2MZF" --header-only -n FOO -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" </dev/null || return 1
    # fname offset = 0x01, prvních 3 bajty = "FOO" = 46 4F 4F
    local fn
    fn=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$fn" "46 4f 4f" "fname starts with 'FOO'" || return 1
    # terminátor 0x0D je na offsetu 0x11 = 17 (po 16 bajtech jména)
    local term
    term=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 17 1)
    assert_eq "$term" "0d" "terminator at offset 0x11 = 0x0D" || return 1
}

# 5. --header-only --no-auto-size --size 100 -> fsize=100, file size stále 128B
test_header_only_with_size_no_auto() {
    "$BIN2MZF" --header-only --no-auto-size --size 100 \
               -n FOO -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" </dev/null 2>/dev/null || return 1
    local sz
    sz=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$sz" "128" "still 128B even with --size 100" || return 1
    local fs
    fs=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 18 2)
    assert_eq "$fs" "64 00" "fsize=100 (0x64) LE" || return 1
}

# 6. --header-only s positional vstupem -> warning obsahuje "ignored"
test_header_only_warning_on_input() {
    head -c 100 /dev/zero > "$TEST_TMPDIR/in.bin"
    local err
    err=$("$BIN2MZF" --header-only -n FOO -l 0x1200 \
                     -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1) || return 1
    assert_contains "$err" "ignored" "stderr mentions 'ignored' for input" || return 1
}

run_test test_header_only_size_128
run_test test_header_only_ignores_input
run_test test_header_only_fsize_zero
run_test test_header_only_fname_correct
run_test test_header_only_with_size_no_auto
run_test test_header_only_warning_on_input

test_summary
