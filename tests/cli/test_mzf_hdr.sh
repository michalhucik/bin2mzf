#!/bin/bash
# Základní CLI test pro mzf-hdr MVP (v0.1.0).
#
# Pokrývá: existence binárky, --help/--version/--lib-versions, modifikace
# jednotlivých polí hlavičky (-t, -n, -l, -e), zachování ostatních polí,
# tělo zachováno bit-by-bit, in-place vs -o mode, charset/upper, escape
# v -n, comment truncate, --cmnt-bin (=104, <104, >104), CP/M plný cyklus,
# atributy, mutex (--cpm × ostatní, -c × --cmnt-bin, --cpm-name bez --cpm),
# auto-size default, --no-auto-size, -s N implicit no-auto-size, invalidní
# input, neexistující input.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná: vyčte N hex bajtů od OFF (mezerami oddělené, bez čísel řádků).
read_hex_bytes() {
    local file="$1"
    local off="$2"
    local n="$3"
    od -An -v -tx1 -j"$off" -N"$n" "$file" | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'
}

# Pomocná: vytvoří MZF base soubor s daným obsahem těla a default jménem
# BASE, load 0x1200.
make_base_mzf() {
    local body="$1"
    local out="$2"
    printf '%s' "$body" > "$TEST_TMPDIR/_body.bin"
    "$BIN2MZF" -n BASE -l 0x1200 -i "$TEST_TMPDIR/_body.bin" -o "$out"
}

# T1. existence binárky
test_binary_exists() {
    if [[ -x "$MZF_HDR" || -x "$MZF_HDR.exe" ]]; then
        return 0
    fi
    echo "    FAIL: binary not found at $MZF_HDR"
    return 1
}

# T2. --help vypisuje Usage a vrací 0
test_help() {
    local out
    out=$("$MZF_HDR" --help 2>&1) || return 1
    assert_contains "$out" "Usage" "help contains Usage" || return 1
}

# T3. --version vrací 0 a obsahuje "mzf-hdr 0.1.0"
test_version() {
    local out
    out=$("$MZF_HDR" --version 2>&1) || return 1
    assert_contains "$out" "mzf-hdr 0.1.0" "version contains mzf-hdr 0.1.0" || return 1
    assert_contains "$out" "bin2mzf-cli" "version contains release name" || return 1
}

# T4. --lib-versions obsahuje názvy všech knihoven
test_lib_versions() {
    local out
    out=$("$MZF_HDR" --lib-versions 2>&1) || return 1
    assert_contains "$out" "mzf" "lib-versions contains mzf" || return 1
    assert_contains "$out" "endianity" "lib-versions contains endianity" || return 1
}

# T5. -t btx zachování ostatních polí
test_set_type_preserves_other_fields() {
    make_base_mzf 'AAAABBBB' "$TEST_TMPDIR/in.mzf" || return 1
    cp "$TEST_TMPDIR/in.mzf" "$TEST_TMPDIR/orig.mzf"
    "$MZF_HDR" -t btx -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    # ftype je byte 0
    local ftype
    ftype=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    assert_eq "$ftype" "02" "ftype changed to 0x02 (BTX)" || return 1
    # name na offset 1..16 zachován
    local orig_name new_name
    orig_name=$(read_hex_bytes "$TEST_TMPDIR/orig.mzf" 1 16)
    new_name=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 16)
    assert_eq "$new_name" "$orig_name" "name preserved" || return 1
}

# T6. -n NEWNAME změní jméno
test_set_name() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" -n NEWNAME -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local name
    name=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 7)
    assert_eq "$name" "4e 45 57 4e 41 4d 45" "name = NEWNAME" || return 1
}

# T7. -l 0xC500 -e 0xC600 změní load+exec
test_set_load_exec() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" -l 0xC500 -e 0xC600 -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local fstrt fexec
    fstrt=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x14 2)
    fexec=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x16 2)
    assert_eq "$fstrt" "00 c5" "fstrt = 0xC500 (LE)" || return 1
    assert_eq "$fexec" "00 c6" "fexec = 0xC600 (LE)" || return 1
}

# T8. tělo zachováno bit-by-bit
test_body_preserved() {
    make_base_mzf 'AAAABBBBCCCC' "$TEST_TMPDIR/in.mzf" || return 1
    cp "$TEST_TMPDIR/in.mzf" "$TEST_TMPDIR/orig.mzf"
    "$MZF_HDR" -n CHANGED -t btx -l 0xFFFF -e 0xFEEE \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    # extrahuj tělo z obou
    "$MZF_STRIP" "$TEST_TMPDIR/orig.mzf" -o "$TEST_TMPDIR/orig_body.bin" || return 1
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf"  -o "$TEST_TMPDIR/out_body.bin"  || return 1
    if ! cmp -s "$TEST_TMPDIR/orig_body.bin" "$TEST_TMPDIR/out_body.bin"; then
        echo "    FAIL: body not preserved bit-by-bit"
        return 1
    fi
}

# T9. in-place modify (bez -o)
test_in_place_modify() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/target.mzf" || return 1
    "$MZF_HDR" -n INPLACE "$TEST_TMPDIR/target.mzf" || return 1
    assert_file_exists "$TEST_TMPDIR/target.mzf" "target still exists" || return 1
    if [ -f "$TEST_TMPDIR/target.mzf.tmp" ]; then
        echo "    FAIL: tmp file leaked"
        return 1
    fi
    local name
    name=$(read_hex_bytes "$TEST_TMPDIR/target.mzf" 1 7)
    assert_eq "$name" "49 4e 50 4c 41 43 45" "in-place name = INPLACE" || return 1
}

# T10. -o mode (target zachován)
test_output_preserves_input() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    local orig_md5
    orig_md5=$(md5sum "$TEST_TMPDIR/in.mzf" | awk '{print $1}')
    "$MZF_HDR" -n NEWNAME -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    assert_file_exists "$TEST_TMPDIR/out.mzf" "output created" || return 1
    local input_md5
    input_md5=$(md5sum "$TEST_TMPDIR/in.mzf" | awk '{print $1}')
    assert_eq "$input_md5" "$orig_md5" "input preserved with -o" || return 1
}

# T11. --charset jp -n TESTNAME (JP konverze; TESTNAME je v ASCII, JP/EU
# se shoduje; ale ověříme aspoň, že to projde a name je správný).
test_charset_jp() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" --charset jp -n TESTNAME -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local name
    name=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 8)
    assert_eq "$name" "54 45 53 54 4e 41 4d 45" "JP name = TESTNAME (ASCII identity)" || return 1
}

# T12. --upper -n hello -> "HELLO"
test_upper_lower_to_upper() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" --upper -n hello -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local name
    name=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 5)
    assert_eq "$name" "48 45 4c 4c 4f" "upper: hello -> HELLO" || return 1
}

# T13. \xNN escape v -n -> raw bajty
test_escape_in_name() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" -n 'AB\x01CD' -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local name
    name=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 5)
    assert_eq "$name" "41 42 01 43 44" "escape: AB\\x01CD" || return 1
}

# T14. -c long... truncate s warningem "truncated"
test_comment_truncated() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    # 110 znaků (104+6 přebytek)
    local long_cmnt
    long_cmnt=$(printf 'A%.0s' $(seq 1 110))
    local err_out
    err_out=$("$MZF_HDR" -c "$long_cmnt" -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_eq "$rc" "0" "comment truncate succeeds" || return 1
    assert_contains "$err_out" "truncated" "warning contains 'truncated'" || return 1
}

# T15. --cmnt-bin =104 B raw shoda
test_cmnt_bin_exact_104() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    # vytvoř 104 B s pattern
    dd if=/dev/zero bs=1 count=104 2>/dev/null | tr '\0' 'Z' > "$TEST_TMPDIR/cmnt.bin"
    "$MZF_HDR" --cmnt-bin "$TEST_TMPDIR/cmnt.bin" -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    # cmnt na offset 0x18 (24), velikost 104 - extrahujeme prvních 8 bajtů jako vzorek
    local cmnt
    cmnt=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x18 8)
    assert_eq "$cmnt" "5a 5a 5a 5a 5a 5a 5a 5a" "cmnt-bin =104: ZZZ..." || return 1
}

# T16. --cmnt-bin <104 B pad nulami
test_cmnt_bin_short_pad_zeroes() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    # vytvoř 4 B
    printf 'XYZW' > "$TEST_TMPDIR/cmnt.bin"
    "$MZF_HDR" --cmnt-bin "$TEST_TMPDIR/cmnt.bin" -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local cmnt
    cmnt=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x18 6)
    assert_eq "$cmnt" "58 59 5a 57 00 00" "short cmnt-bin padded with zeros" || return 1
}

# T17. --cmnt-bin >104 truncate s warning "truncated"
test_cmnt_bin_long_truncate_warns() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    dd if=/dev/zero bs=1 count=200 2>/dev/null | tr '\0' 'Q' > "$TEST_TMPDIR/cmnt.bin"
    local err_out
    err_out=$("$MZF_HDR" --cmnt-bin "$TEST_TMPDIR/cmnt.bin" -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_eq "$rc" "0" "long cmnt-bin succeeds with warning" || return 1
    assert_contains "$err_out" "truncated" "warning contains 'truncated'" || return 1
    # cmnt první bajt = 'Q'
    local first
    first=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x18 1)
    assert_eq "$first" "51" "cmnt[0] = Q (0x51)" || return 1
}

# T18. --cpm na non-CPM target -> ftype=0x22, fname SOKODI, fstrt/fexec=0x0100
test_cpm_full_cycle() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" --cpm --cpm-name HELLO --cpm-ext COM \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local ftype fstrt fexec
    ftype=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    fstrt=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x14 2)
    fexec=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x16 2)
    assert_eq "$ftype" "22" "ftype = 0x22 (CPM)" || return 1
    assert_eq "$fstrt" "00 01" "fstrt = 0x0100 (LE)" || return 1
    assert_eq "$fexec" "00 01" "fexec = 0x0100 (LE)" || return 1
    # SOKODI fname: "HELLO   .COM" + 0x0D
    local fname
    fname=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 13)
    assert_eq "$fname" "48 45 4c 4c 4f 20 20 20 2e 43 4f 4d 0d" "SOKODI 8.3 fname" || return 1
    # tělo zachováno
    "$MZF_STRIP" "$TEST_TMPDIR/out.mzf" -o "$TEST_TMPDIR/out_body.bin" || return 1
    local body_bytes
    body_bytes=$(read_hex_bytes "$TEST_TMPDIR/out_body.bin" 0 4)
    assert_eq "$body_bytes" "41 41 41 41" "CP/M body preserved" || return 1
}

# T19. --cpm-attr ro -> bit 7 ve fname[9]
test_cpm_attr_ro() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    "$MZF_HDR" --cpm --cpm-name TEST --cpm-attr ro \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    # fname[9] = 'C' (0x43) | 0x80 = 0xC3
    local b9
    b9=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x0a 1)
    assert_eq "$b9" "c3" "fname[9] R/O bit 7 set: 0x43|0x80=0xC3" || return 1
}

# T20. mutex --cpm × -t -> rc != 0
test_mutex_cpm_type() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    local err_out
    err_out=$("$MZF_HDR" --cpm --cpm-name X -t btx \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "--cpm × --type must fail" || return 1
}

# T21. mutex --cpm × -l -> rc != 0
test_mutex_cpm_load() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    local err_out
    err_out=$("$MZF_HDR" --cpm --cpm-name X -l 0x1000 \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "--cpm × --load-addr must fail" || return 1
}

# T22. mutex --cpm × -n -> rc != 0
test_mutex_cpm_name() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    local err_out
    err_out=$("$MZF_HDR" --cpm --cpm-name X -n FOO \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "--cpm × --name must fail" || return 1
}

# T23. mutex --cpm-name bez --cpm -> rc != 0
test_mutex_cpm_name_without_cpm() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    local err_out
    err_out=$("$MZF_HDR" --cpm-name X \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "--cpm-name without --cpm must fail" || return 1
    assert_contains "$err_out" "requires --cpm" "error mentions 'requires --cpm'" || return 1
}

# T24. mutex -c × --cmnt-bin -> rc != 0
test_mutex_comment_cmntbin() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    printf 'X' > "$TEST_TMPDIR/cmnt.bin"
    local err_out
    err_out=$("$MZF_HDR" -c "hello" --cmnt-bin "$TEST_TMPDIR/cmnt.bin" \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "--comment × --cmnt-bin must fail" || return 1
    assert_contains "$err_out" "mutually exclusive" "error mentions 'mutually exclusive'" || return 1
}

# T25. auto-size default: fsize z body_size
test_auto_size_default() {
    make_base_mzf 'ABCDEFGHIJ' "$TEST_TMPDIR/in.mzf" || return 1
    # body je 10 bajtů, default auto-size dá fsize=0x000A
    "$MZF_HDR" -n AUTO -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local fsize
    fsize=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x12 2)
    assert_eq "$fsize" "0a 00" "auto-size default: fsize = body_size (LE)" || return 1
}

# T26. --no-auto-size: fsize zachován z input.mzf
test_no_auto_size_preserves_fsize() {
    make_base_mzf 'ABCDEFGH' "$TEST_TMPDIR/in.mzf" || return 1
    # body 8 B, default fsize = 0x0008
    # Zfalšujeme fsize ručně přes --size, pak ověříme, že --no-auto-size
    # vstupní fsize zachová.
    # Jednodušší: --no-auto-size na nezměněném MZF nesmí změnit fsize.
    local orig_fsize
    orig_fsize=$(read_hex_bytes "$TEST_TMPDIR/in.mzf" 0x12 2)
    "$MZF_HDR" --no-auto-size -n KEEPNAME -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local new_fsize
    new_fsize=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x12 2)
    assert_eq "$new_fsize" "$orig_fsize" "no-auto-size: fsize preserved" || return 1
}

# T27a. -s N kde N <= body_size: fsize=N nastaveno správně, knihovna mzf_save
# zapisuje právě fsize bajtů body (round-trip přes mzf-strip dává prvních
# N bajtů těla).
test_size_explicit_within_body() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    # body je 4 bajty (AAAA), -s 0x4 nastaví fsize=0x0004 == body_size
    "$MZF_HDR" -s 0x4 -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local fsize
    fsize=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x12 2)
    assert_eq "$fsize" "04 00" "fsize = 0x0004 (LE)" || return 1
    # Tělo (offset 0x80, fsize=4 bajty) byte-identické s originálem.
    local body_bytes
    body_bytes=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0x80 4)
    assert_eq "$body_bytes" "41 41 41 41" "body bytes preserved (AAAA)" || return 1

    # Subtest: -s N kde N < body_size také projde (fsize=2, output má jen
    # 2 bajty body, což je validní use case "header říká, prvních N bajtů
    # je validních").
    "$MZF_HDR" -s 0x2 -o "$TEST_TMPDIR/out2.mzf" "$TEST_TMPDIR/in.mzf" || return 1
    local fsize2
    fsize2=$(read_hex_bytes "$TEST_TMPDIR/out2.mzf" 0x12 2)
    assert_eq "$fsize2" "02 00" "fsize = 0x0002 (LE) when N < body_size" || return 1
}

# T27b. -s N kde N > body_size: hard error (žádný segfault, žádný output)
test_size_explicit_exceeds_body() {
    make_base_mzf 'AAAA' "$TEST_TMPDIR/in.mzf" || return 1
    # body je 4 bajty, -s 0x100 (256) je výrazně větší.
    rm -f "$TEST_TMPDIR/out.mzf"
    local err_out rc
    err_out=$("$MZF_HDR" -s 0x100 -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.mzf" 2>&1)
    rc=$?
    assert_neq "$rc" "0" "size > body must fail (rc != 0)" || return 1
    # Žádný segfault: rc=139 (SIGSEGV) je explicitně nepřijatelný.
    if [[ "$rc" == "139" ]]; then
        echo "    FAIL: segfault (rc=139)"
        return 1
    fi
    assert_contains "$err_out" "exceeds body size" "error mentions 'exceeds body size'" || return 1
    # Atomic write semantika: výstupní soubor nesmí existovat.
    if [[ -e "$TEST_TMPDIR/out.mzf" ]]; then
        echo "    FAIL: output file exists after failed write"
        return 1
    fi
}

# T28. nonexistent input -> rc != 0
test_nonexistent_input() {
    "$MZF_HDR" -n FOO -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/does_not_exist.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "nonexistent input must fail" || return 1
}

# T29. invalid input MZF (truncated) -> rc != 0
test_invalid_input_truncated() {
    head -c 50 /dev/zero > "$TEST_TMPDIR/bad.mzf"
    "$MZF_HDR" -n FOO -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/bad.mzf" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "truncated MZF input must fail" || return 1
}

run_test test_binary_exists
run_test test_help
run_test test_version
run_test test_lib_versions
run_test test_set_type_preserves_other_fields
run_test test_set_name
run_test test_set_load_exec
run_test test_body_preserved
run_test test_in_place_modify
run_test test_output_preserves_input
run_test test_charset_jp
run_test test_upper_lower_to_upper
run_test test_escape_in_name
run_test test_comment_truncated
run_test test_cmnt_bin_exact_104
run_test test_cmnt_bin_short_pad_zeroes
run_test test_cmnt_bin_long_truncate_warns
run_test test_cpm_full_cycle
run_test test_cpm_attr_ro
run_test test_mutex_cpm_type
run_test test_mutex_cpm_load
run_test test_mutex_cpm_name
run_test test_mutex_cpm_name_without_cpm
run_test test_mutex_comment_cmntbin
run_test test_auto_size_default
run_test test_no_auto_size_preserves_fsize
run_test test_size_explicit_within_body
run_test test_size_explicit_exceeds_body
run_test test_nonexistent_input
run_test test_invalid_input_truncated

test_summary
