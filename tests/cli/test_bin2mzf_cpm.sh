#!/bin/bash
# CLI test pro bin2mzf v0.2.0+ - CP/M konvence (SOKODI CMT.COM).
#
# Pokrývá: ftype/fstrt/fexec preset (0x22/0x0100/0x0100), 8.3+CR layout
# pole fname.name, atribut bity (R/O, SYS, ARC, kombinace), case-insensitive
# tokenizer s longest-match (r/o vs ro), separátorové varianty (čárka,
# mezera, concat), default přípona "COM", silent uppercase + space-pad,
# overflow / non-ASCII validace, mutual exclusion s --type/--load-addr/
# --exec-addr/--charset/--name, požadavek --cpm pro sub-options, povolené
# kombinace s --comment.

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

# Pomocná funkce: vyrobí binární soubor zadané velikosti naplněný 0xAA.
make_input() {
    local path="$1"
    local size="$2"
    head -c "$size" /dev/zero | tr '\0' '\252' > "$path"
}

# 1. základní CP/M preset: ftype=0x22, fstrt=fexec=0x0100
test_cpm_basic() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local ftype fstrt fexec
    ftype=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    assert_eq "$ftype" "22" "ftype = 0x22 (CP/M marker)" || return 1
    fstrt=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 20 2)
    assert_eq "$fstrt" "00 01" "fstrt = 0x0100 (LE)" || return 1
    fexec=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 22 2)
    assert_eq "$fexec" "00 01" "fexec = 0x0100 (LE)" || return 1
}

# 2. fname layout: HELLO + 3 mezery + . + COM + 0x0D + 0x0D 0x0D 0x0D + term
test_cpm_fname_layout() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-ext COM \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    # name[0..12] = "HELLO   .COM\r" = 48 45 4c 4c 4f 20 20 20 2e 43 4f 4d 0d
    local fname13
    fname13=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 13)
    assert_eq "$fname13" "48 45 4c 4c 4f 20 20 20 2e 43 4f 4d 0d" \
              "fname[0..12] = HELLO   .COM CR" || return 1
    # name[13..15] = 0x0D 0x0D 0x0D (z memset v mzf_tools_create_mzfhdr)
    local pad3
    pad3=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 14 3)
    assert_eq "$pad3" "0d 0d 0d" "fname[13..15] = 0x0D padding" || return 1
    # terminator (offset 0x11 = 17) = 0x0D
    local term
    term=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 17 1)
    assert_eq "$term" "0d" "fname.terminator = 0x0D" || return 1
}

# 3. R/O atribut: bit 7 fname[9] (offset 10) nastaven ('C' 0x43 -> 0xC3)
test_cpm_attr_ro() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr ro \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 10 1)
    assert_eq "$b" "c3" "fname[9] = 'C' | 0x80 = 0xC3 (R/O)" || return 1
}

# 4. SYS atribut: bit 7 fname[10] (offset 11) nastaven ('O' 0x4F -> 0xCF)
test_cpm_attr_sys() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr sys \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 11 1)
    assert_eq "$b" "cf" "fname[10] = 'O' | 0x80 = 0xCF (SYS)" || return 1
}

# 5. ARC atribut: bit 7 fname[11] (offset 12) nastaven ('M' 0x4D -> 0xCD)
test_cpm_attr_arc() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr arc \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 12 1)
    assert_eq "$b" "cd" "fname[11] = 'M' | 0x80 = 0xCD (ARC)" || return 1
}

# 6. všechny tři atributy: ro,sys,arc
test_cpm_attr_all() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr ro,sys,arc \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local ext3
    ext3=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 10 3)
    assert_eq "$ext3" "c3 cf cd" "all attrs set on .COM" || return 1
}

# 7. concatenated tokeny (roSys = ro,sys)
test_cpm_attr_concat() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr roSys \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local ext3
    ext3=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 10 3)
    assert_eq "$ext3" "c3 cf 4d" "roSys = R/O + SYS, no ARC" || return 1
}

# 8. mezerou oddělené tokeny case-insensitive ("RO ARC")
test_cpm_attr_space() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr "RO ARC" \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local ext3
    ext3=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 10 3)
    assert_eq "$ext3" "c3 4f cd" "RO ARC = R/O + ARC, no SYS" || return 1
}

# 9. neznámý token v --cpm-attr -> error
test_cpm_attr_unknown() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO --cpm-attr foo \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: unknown attr token should error"
        return 1
    fi
    return 0
}

# 10. lowercase silent uppercase: hello / com -> HELLO / COM
test_cpm_lowercase_uppercased() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name hello --cpm-ext com \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local fname13
    fname13=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 13)
    assert_eq "$fname13" "48 45 4c 4c 4f 20 20 20 2e 43 4f 4d 0d" \
              "lowercase silently uppercased" || return 1
}

# 11. krátké jméno: AB -> AB + 6 mezer pad
test_cpm_short_name() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name AB \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local name8
    name8=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 8)
    assert_eq "$name8" "41 42 20 20 20 20 20 20" "AB + 6 spaces pad" || return 1
}

# 12. default --cpm-ext = COM, pokud uživatel neuvede
test_cpm_default_ext() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local ext3
    ext3=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 10 3)
    assert_eq "$ext3" "43 4f 4d" "default ext = COM" || return 1
}

# 13. overflow --cpm-name (>8 znaků) -> error
test_cpm_name_overflow() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name TOOLONGNAME \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: long name should error"
        return 1
    fi
    return 0
}

# 14. overflow --cpm-ext (>3 znaky) -> error
test_cpm_ext_overflow() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO --cpm-ext TOOL \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: long ext should error"
        return 1
    fi
    return 0
}

# 15. non-printable v --cpm-name -> error.
# POZN.: Bajty > 0x7E (např. 0xFF) jsou na MSYS2 vrstvě převáděny mezi
# UTF-8 / Windows ANSI při exec do .exe, takže nejsou přenosný způsob,
# jak otestovat ASCII validaci. Control bajt 0x01 přes hranici projde
# čistě a triggeruje stejnou validační větev (`< 0x20`).
test_cpm_name_nonascii() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name $'FOO\x01' \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: control byte name should error"
        return 1
    fi
    return 0
}

# 16. --cpm + --type = exit error
test_cpm_excl_type() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO -t 0x01 \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm + --type should error"
        return 1
    fi
    return 0
}

# 17. --cpm + --load-addr = exit error
test_cpm_excl_load() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO -l 0x1200 \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm + --load-addr should error"
        return 1
    fi
    return 0
}

# 18. --cpm + --exec-addr = exit error
test_cpm_excl_exec() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO -e 0x0200 \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm + --exec-addr should error"
        return 1
    fi
    return 0
}

# 19. --cpm + --charset = exit error
test_cpm_excl_charset() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO --charset eu \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm + --charset should error"
        return 1
    fi
    return 0
}

# 20. --cpm + --name = exit error
test_cpm_excl_name() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm --cpm-name HELLO -n FOO \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm + --name should error"
        return 1
    fi
    return 0
}

# 21. --cpm-name bez --cpm = exit error
test_cpm_subopt_without_cpm() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm-name FOO -l 0x1000 \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm-name without --cpm should error"
        return 1
    fi
    return 0
}

# 22. --cpm + --comment OK (komentář raw v poli cmnt)
test_cpm_with_comment() {
    make_input "$TEST_TMPDIR/in.bin" 100
    "$BIN2MZF" --cpm --cpm-name HELLO -c "Note" \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    # cmnt začíná na offsetu 0x18 = 24
    local cmnt4
    cmnt4=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 24 4)
    assert_eq "$cmnt4" "4e 6f 74 65" "comment 'Note' raw bytes" || return 1
}

# 23. --cpm bez --cpm-name = exit error
test_cpm_missing_name() {
    make_input "$TEST_TMPDIR/in.bin" 100
    if "$BIN2MZF" --cpm \
                  -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>/dev/null; then
        echo "    FAIL: --cpm without --cpm-name should error"
        return 1
    fi
    return 0
}

run_test test_cpm_basic
run_test test_cpm_fname_layout
run_test test_cpm_attr_ro
run_test test_cpm_attr_sys
run_test test_cpm_attr_arc
run_test test_cpm_attr_all
run_test test_cpm_attr_concat
run_test test_cpm_attr_space
run_test test_cpm_attr_unknown
run_test test_cpm_lowercase_uppercased
run_test test_cpm_short_name
run_test test_cpm_default_ext
run_test test_cpm_name_overflow
run_test test_cpm_ext_overflow
run_test test_cpm_name_nonascii
run_test test_cpm_excl_type
run_test test_cpm_excl_load
run_test test_cpm_excl_exec
run_test test_cpm_excl_charset
run_test test_cpm_excl_name
run_test test_cpm_subopt_without_cpm
run_test test_cpm_with_comment
run_test test_cpm_missing_name

test_summary
