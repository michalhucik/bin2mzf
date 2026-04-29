#!/bin/bash
# Základní CLI test pro bin2mzf MVP.
#
# Pokrývá: existence binárky, --help/--version/--lib-versions, povinné
# argumenty, round-trip bin -> MZF (file i stdin/stdout), kontrolu
# bajtů hlavičky, default exec=load, default ftype=OBJ, symbolické a
# číselné -t, validaci délky jména a velikosti těla, truncate komentáře,
# odmítnutí neplatné load adresy.

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vyčte N hex bajtů od offsetu OFF z BIN ve formátu
# odděleném mezerami (bez čísel řádků). Používá `od` pro maximální
# přenositelnost na MSYS2.
read_hex_bytes() {
    local file="$1"
    local off="$2"
    local n="$3"
    od -An -v -tx1 -j"$off" -N"$n" "$file" | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'
}

# 1. existence binárky
test_binary_exists() {
    if [[ -x "$BIN2MZF" || -x "$BIN2MZF.exe" ]]; then
        return 0
    fi
    echo "    FAIL: binary not found at $BIN2MZF"
    return 1
}

# 2. --help vypisuje Usage a vrací 0
test_help() {
    local out
    out=$("$BIN2MZF" --help 2>&1) || return 1
    assert_contains "$out" "Usage" "help contains Usage" || return 1
}

# 3. --version vrací 0 a obsahuje verzi a release
test_version() {
    local out
    out=$("$BIN2MZF" --version 2>&1) || return 1
    assert_contains "$out" "bin2mzf 0.2.0" "version contains bin2mzf 0.2.0" || return 1
    assert_contains "$out" "bin2mzf-cli 1.0.0" "version contains bin2mzf-cli 1.0.0" || return 1
}

# 4. --lib-versions obsahuje názvy všech knihoven
test_lib_versions() {
    local out
    out=$("$BIN2MZF" --lib-versions 2>&1) || return 1
    assert_contains "$out" "mzf" "lib-versions contains mzf" || return 1
    assert_contains "$out" "endianity" "lib-versions contains endianity" || return 1
    assert_contains "$out" "sharpmz_ascii" "lib-versions contains sharpmz_ascii" || return 1
    assert_contains "$out" "generic_driver" "lib-versions contains generic_driver" || return 1
}

# 5. spuštění bez argumentů selže (chybí povinné -n a -l)
test_no_args_fails() {
    "$BIN2MZF" </dev/null >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "no args must fail" || return 1
}

# 6. chybějící --name selže
test_missing_name_fails() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -l 0x1200 -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "missing --name must fail" || return 1
}

# 7. chybějící --load-addr selže
test_missing_load_fails() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n FOO -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "missing --load-addr must fail" || return 1
}

# 8. základní round-trip: file -> bin2mzf -> file, body bajty se shodují
test_basic_file_to_file_roundtrip() {
    printf '\x12\x34\x56\x78ABCD\xff\x00' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -t 0x01 -n TEST -l 0x1200 -e 0x1208 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    assert_file_exists "$TEST_TMPDIR/out.mzf" "output created" || return 1
    local in_size mzf_size
    in_size=$(stat -c %s "$TEST_TMPDIR/in.bin")
    mzf_size=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$mzf_size" "$((128 + in_size))" "mzf size = 128 + body" || return 1
    dd if="$TEST_TMPDIR/out.mzf" bs=1 skip=128 of="$TEST_TMPDIR/body.bin" 2>/dev/null
    cmp "$TEST_TMPDIR/body.bin" "$TEST_TMPDIR/in.bin" || {
        echo "    FAIL: body bytes differ from input"
        return 1
    }
}

# 9. kontrola jednotlivých bajtů hlavičky (ftype, name, terminator, addrs)
test_header_fields_correct() {
    printf '\xaa\xbb\xcc' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -t 0x01 -n TEST -l 0x1200 -e 0x1208 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    # byte 0 = ftype 0x01
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    assert_eq "$b" "01" "ftype byte == 01" || return 1
    # bytes 1..4 = "TEST" = 54 45 53 54
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 4)
    assert_eq "$b" "54 45 53 54" "name bytes == 'TEST'" || return 1
    # byte 0x11 = terminator 0x0d
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 17 1)
    assert_eq "$b" "0d" "name terminator == 0x0d" || return 1
    # bytes 0x12..0x13 = fsize LE = 03 00
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 18 2)
    assert_eq "$b" "03 00" "fsize LE == 03 00" || return 1
    # bytes 0x14..0x15 = fstrt = 0x1200 LE = 00 12
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 20 2)
    assert_eq "$b" "00 12" "fstrt LE == 00 12" || return 1
    # bytes 0x16..0x17 = fexec = 0x1208 LE = 08 12
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 22 2)
    assert_eq "$b" "08 12" "fexec LE == 08 12" || return 1
}

# 10. default exec-addr == load-addr (bez -e)
test_default_exec_equals_load() {
    printf 'A' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n DEF -l 0x3456 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local fstrt fexec
    fstrt=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 20 2)
    fexec=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 22 2)
    assert_eq "$fexec" "$fstrt" "default exec-addr equals load-addr" || return 1
    assert_eq "$fstrt" "56 34" "fstrt LE == 56 34" || return 1
}

# 11. default ftype = OBJ (0x01) bez -t
test_default_ftype_obj() {
    printf 'A' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n D -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    assert_eq "$b" "01" "default ftype == 0x01 (OBJ)" || return 1
}

# 12. symbolický --type btx -> 0x02
test_ftype_symbolic_btx() {
    printf 'A' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -t btx -n S -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    assert_eq "$b" "02" "ftype btx == 0x02" || return 1
}

# 13. číselné --type 0x05 -> 0x05 (RB)
test_ftype_numeric_hex() {
    printf 'A' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -t 0x05 -n N -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 0 1)
    assert_eq "$b" "05" "ftype 0x05 == 0x05" || return 1
}

# 14. stdin -> stdout pipeline
test_stdin_to_stdout() {
    printf 'HELLO\n' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n PIPE -l 0x1200 \
               < "$TEST_TMPDIR/in.bin" > "$TEST_TMPDIR/out.mzf" || return 1
    local in_size mzf_size
    in_size=$(stat -c %s "$TEST_TMPDIR/in.bin")
    mzf_size=$(stat -c %s "$TEST_TMPDIR/out.mzf")
    assert_eq "$mzf_size" "$((128 + in_size))" "stdin/stdout size correct" || return 1
}

# 15. jméno delší než 16 znaků selže
test_name_too_long_fails() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n THIS_NAME_IS_WAY_TOO_LONG -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "name > 16 chars must fail" || return 1
}

# 16. tělo > 65535 bajtů selže
test_body_too_large_fails() {
    # 65536 bajtů z /dev/zero
    dd if=/dev/zero of="$TEST_TMPDIR/big.bin" bs=1024 count=64 2>/dev/null
    # přidáme jeden bajt navíc, ať máme 65537
    printf '\x00' >> "$TEST_TMPDIR/big.bin"
    "$BIN2MZF" -n BIG -l 0x1200 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/big.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "body > 65535 bytes must fail" || return 1
}

# 17. dlouhý komentář -> warning, ale exit 0 a soubor je vytvořený
test_comment_truncation_warning() {
    printf 'A' > "$TEST_TMPDIR/in.bin"
    # 200znakový komentář (víc než 104)
    local long_comment
    long_comment=$(printf 'X%.0s' $(seq 1 200))
    local err_out
    err_out=$("$BIN2MZF" -n CMT -l 0x1200 -c "$long_comment" \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1) || return 1
    assert_contains "$err_out" "truncated" "long comment produces truncate warning" || return 1
    assert_file_exists "$TEST_TMPDIR/out.mzf" "output still created" || return 1
}

# 18. neplatná load adresa selže
test_invalid_load_addr_fails() {
    printf 'A' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n N -l notnumber \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" >/dev/null 2>&1
    local rc=$?
    assert_neq "$rc" "0" "invalid load-addr must fail" || return 1
}

run_test test_binary_exists
run_test test_help
run_test test_version
run_test test_lib_versions
run_test test_no_args_fails
run_test test_missing_name_fails
run_test test_missing_load_fails
run_test test_basic_file_to_file_roundtrip
run_test test_header_fields_correct
run_test test_default_exec_equals_load
run_test test_default_ftype_obj
run_test test_ftype_symbolic_btx
run_test test_ftype_numeric_hex
run_test test_stdin_to_stdout
run_test test_name_too_long_fails
run_test test_body_too_large_fails
run_test test_comment_truncation_warning
run_test test_invalid_load_addr_fails

test_summary
