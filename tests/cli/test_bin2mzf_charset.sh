#!/bin/bash
# CLI test pro bin2mzf v0.2.0 - charset, upper, escape sekvence.
#
# Pokrývá: default --charset eu, explicitní eu/jp/none/utf8-eu/utf8-jp,
# --upper, escape \xNN, escape disables charset, escape disables upper,
# invalid charset, malformed escape, ASCII boundary jména.
#
# Pozor-bod MSYS2: bash v Windows konvertuje UTF-8 znaky v argv na CP1252,
# takže testy s reálnými UTF-8 vícebajtovými znaky v argv nelze provést.
# Pokrytí UTF-8 zajišťují unit testy knihovny sharpmz_ascii (test_eu_utf8_*,
# test_jp_utf8_*, test_str_from_utf8_eu).

source "$(dirname "$0")/helpers.sh"
echo "=== $0 ==="

# Pomocná funkce: vyčte N hex bajtů od offsetu OFF z BIN ve formátu
# odděleném mezerami (kopie z test_bin2mzf_basic.sh).
read_hex_bytes() {
    local file="$1"
    local off="$2"
    local n="$3"
    od -An -v -tx1 -j"$off" -N"$n" "$file" | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'
}

# Sharp MZ EU lowercase mapování (ověřeno na běhu binárky):
#   a (0x61) -> 0xa1
#   b (0x62) -> 0x9a
#   c (0x63) -> 0x9f

# 1. Default charset = eu - lowercase abc se mapuje na MZ EU kódy
test_default_charset_eu() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -n abc -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "a1 9a 9f" "default charset eu maps abc -> Sharp MZ" || return 1
}

# 2. Explicitní --charset eu s velkými písmeny - identita (0x20-0x5D)
test_explicit_charset_eu_uppercase() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C eu -n ABC -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "41 42 43" "charset eu identity for ABC" || return 1
}

# 3. --charset none - syrové bajty bez konverze (lowercase abc zůstává 0x61-0x63)
test_charset_none_raw_bytes() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C none -n abc -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "61 62 63" "charset none preserves raw lowercase" || return 1
}

# 4. --charset jp s velkými písmeny - identita 0x41-0x43
test_charset_jp_uppercase() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C jp -n ABC -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "41 42 43" "charset jp identity for ABC" || return 1
}

# 5. --charset jp s lowercase - JP nemapuje a-z, vrací mezeru 0x20
test_charset_jp_lowercase_to_space() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C jp -n abc -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "20 20 20" "charset jp lowercase -> spaces" || return 1
}

# 6. --charset utf8-eu pro ASCII vstup - synonymum k eu (ASCII identita 0x20-0x5D)
# Pozn.: skutečná UTF-8 multibyte verifikace je v unit testech knihovny
# (MSYS2 argv ANSI konverze brání předání UTF-8 do bin2mzf z bash testu).
test_charset_utf8_eu_ascii_synonym() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C utf8-eu -n ABC -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "41 42 43" "utf8-eu identity for ABC" || return 1
    # navíc: lowercase ASCII chování stejné jako eu (a -> 0xa1)
    "$BIN2MZF" -C utf8-eu -n a -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 1)
    assert_eq "$b" "a1" "utf8-eu lowercase a == eu lowercase a" || return 1
}

# 7. --charset utf8-jp pro single-byte ASCII - identita 0x20-0x5D
test_charset_utf8_jp_ascii_identity() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C utf8-jp -n ABC -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "41 42 43" "utf8-jp ASCII identity for ABC" || return 1
}

# 8. --upper s eu - lowercase abc se nejprve uppercasne na ABC (0x41-0x43)
test_upper_with_eu() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C eu --upper -n abc -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "41 42 43" "--upper converts abc to ABC before charset" || return 1
}

# 9. Escape sekvence \xNN vyrobí přesný bajt
# Pozn.: 'A\x42C' v bash single-quote dorazí do argv jako 6 znaků: A \ x 4 2 C
# parser uvidí escape \x42 -> 0x42, takže výstup je A B C = 41 42 43.
test_escape_xNN_literal_byte() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C eu -n 'A\x42C' -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "41 42 43" "escape \\x42 produces literal 0x42" || return 1
}

# 10. Escape vypíná --charset pro dané pole
# Vstup 'a\xFFb': escape přítomen -> charset NONE, výsledek raw 0x61 0xFF 0x62
# I když je --charset jp (které by jinak vyrobilo 0x20 0x20 0x20).
test_escape_disables_charset() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C jp -n 'a\xFFb' -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "61 ff 62" "escape disables charset (jp -> none for this field)" || return 1
}

# 11. Escape vypíná --upper pro dané pole
# Vstup 'a\x5Cb' (escape \x5C = '\') - escape přítomen -> upper se neaplikuje
# Výsledek: 0x61 0x5C 0x62 (lowercase 'a' zůstává).
test_escape_disables_upper() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" --upper -n 'a\x5Cb' -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    local b
    b=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$b" "61 5c 62" "escape disables --upper for this field" || return 1
}

# 12. Escape je per-pole - escape v --name nezapne NONE pro --comment
# --name má escape (none), --comment nemá -> stále eu konverze.
test_escape_per_field_granularity() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    "$BIN2MZF" -C eu -n 'a\x42c' -c abc -l 0x1000 \
               -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" || return 1
    # name: a\x42c = a B c (raw, neeu) -> 0x61 0x42 0x63
    local nb
    nb=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 1 3)
    assert_eq "$nb" "61 42 63" "name uses NONE due to escape" || return 1
    # comment je na offsetu 0x18 (24), 3 bajty - eu konverze 'abc' -> a1 9a 9f
    local cb
    cb=$(read_hex_bytes "$TEST_TMPDIR/out.mzf" 24 3)
    assert_eq "$cb" "a1 9a 9f" "comment uses EU (no escape in comment)" || return 1
}

# 13. Invalid --charset hodnota selže s rozpoznatelnou hláškou
test_invalid_charset_fails() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    local err_out
    err_out=$("$BIN2MZF" -C xxx -n A -l 0x1000 \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "invalid charset must fail" || return 1
    assert_contains "$err_out" "invalid --charset" "stderr mentions invalid charset" || return 1
}

# 14. Malformed escape selže s "malformed escape sequence" hláškou
# 'a\xZZ' - \xZZ není validní hex (ZZ nejsou hex číslice).
test_malformed_escape_fails() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    local err_out
    err_out=$("$BIN2MZF" -n 'a\xZZ' -l 0x1000 \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "malformed escape must fail" || return 1
    assert_contains "$err_out" "malformed escape" "stderr mentions malformed escape" || return 1
}

# 15. Jméno 17 ASCII znaků přeteče po EU konverzi (1:1 ASCII identity)
# - hard error (NAME field).
test_name_ascii_overflow_after_charset() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    local err_out
    err_out=$("$BIN2MZF" -C eu -n ABCDEFGHIJKLMNOPQ -l 0x1000 \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1)
    local rc=$?
    assert_neq "$rc" "0" "17 ASCII chars must fail after EU conversion" || return 1
    assert_contains "$err_out" "exceeds" "stderr mentions overflow" || return 1
}

# 16. Komentář delší než 104 B po EU konverzi -> warning "truncated", soubor vznikne
# (kritický invariant - regresní test #17 v test_bin2mzf_basic.sh také hledá
# slovo "truncated", tady to ověřujeme i s explicitním --charset eu).
test_comment_overflow_truncated_eu() {
    printf 'X' > "$TEST_TMPDIR/in.bin"
    local long_comment
    long_comment=$(printf 'A%.0s' $(seq 1 200))
    local err_out
    err_out=$("$BIN2MZF" -C eu -n CMT -l 0x1000 -c "$long_comment" \
                          -o "$TEST_TMPDIR/out.mzf" "$TEST_TMPDIR/in.bin" 2>&1) || return 1
    assert_contains "$err_out" "truncated" "long comment produces truncate warning" || return 1
    assert_file_exists "$TEST_TMPDIR/out.mzf" "output still created" || return 1
}

run_test test_default_charset_eu
run_test test_explicit_charset_eu_uppercase
run_test test_charset_none_raw_bytes
run_test test_charset_jp_uppercase
run_test test_charset_jp_lowercase_to_space
run_test test_charset_utf8_eu_ascii_synonym
run_test test_charset_utf8_jp_ascii_identity
run_test test_upper_with_eu
run_test test_escape_xNN_literal_byte
run_test test_escape_disables_charset
run_test test_escape_disables_upper
run_test test_escape_per_field_granularity
run_test test_invalid_charset_fails
run_test test_malformed_escape_fails
run_test test_name_ascii_overflow_after_charset
run_test test_comment_overflow_truncated_eu

test_summary
