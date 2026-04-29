#!/bin/bash
# Sdílené pomocné funkce pro CLI testy rodiny bin2mzf.
# Source: source tests/cli/helpers.sh

# Odpojit stdin od případného TTY - CLI nástroje (bin2mzf rodina) by mohly
# v destruktivních režimech interaktivně potvrzovat operaci, pokud je
# stdin TTY. V testech to způsobí zatuhnutí čekáním na uživatelský vstup.
# Non-TTY stdin => případné confirm_destructive_op() automaticky pokračuje
# bez dotazu.
#
# POZOR: Na MSYS2/MinGW je exec </dev/null nedostatečné - MinGW isatty()
# vidí stále konzolový Windows handle. Proto destruktivní operace v testech
# budou navíc používat flag -y/--yes.
exec </dev/null

# Cesty k binárkám rodiny bin2mzf
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CLI_DIR="$PROJECT_DIR/build-cli"

BIN2MZF="$CLI_DIR/bin2mzf"
MZF_INFO="$CLI_DIR/mzf-info"
MZF_HDR="$CLI_DIR/mzf-hdr"
MZF_STRIP="$CLI_DIR/mzf-strip"
MZF_CAT="$CLI_DIR/mzf-cat"
MZF_PASTE="$CLI_DIR/mzf-paste"

# Temp adresář pro testy
TEST_TMPDIR=$(mktemp -d)
trap "rm -rf $TEST_TMPDIR" EXIT

# Počítadla
_PASSED=0
_FAILED=0
_TOTAL=0

# Barvy (pokud terminál podporuje)
if [ -t 1 ]; then
    _GREEN='\033[0;32m'
    _RED='\033[0;31m'
    _NC='\033[0m'
else
    _GREEN=''
    _RED=''
    _NC=''
fi

## assert_eq "actual" "expected" "message"
assert_eq() {
    if [ "$1" = "$2" ]; then
        return 0
    else
        echo -e "    ${_RED}FAIL${_NC}: $3"
        echo "      expected: '$2'"
        echo "      actual:   '$1'"
        return 1
    fi
}

## assert_neq "actual" "unexpected" "message"
assert_neq() {
    if [ "$1" != "$2" ]; then
        return 0
    else
        echo -e "    ${_RED}FAIL${_NC}: $3"
        echo "      expected NOT: '$2'"
        echo "      actual:       '$1'"
        return 1
    fi
}

## assert_contains "string" "substring" "message"
assert_contains() {
    if echo "$1" | grep -q "$2"; then
        return 0
    else
        echo -e "    ${_RED}FAIL${_NC}: $3"
        echo "      string: '$1'"
        echo "      missing: '$2'"
        return 1
    fi
}

## assert_not_contains "string" "substring" "message"
assert_not_contains() {
    if echo "$1" | grep -q "$2"; then
        echo -e "    ${_RED}FAIL${_NC}: $3"
        echo "      string: '$1'"
        echo "      unexpected: '$2'"
        return 1
    else
        return 0
    fi
}

## assert_file_exists "path" "message"
assert_file_exists() {
    if [ -f "$1" ]; then
        return 0
    else
        echo -e "    ${_RED}FAIL${_NC}: $2 - file not found: $1"
        return 1
    fi
}

## json_get "json_string" "key" - jednoduché grep parsování JSON
## Podporuje: "key": value, "key": "value"
json_get() {
    local val
    # Zkusit string hodnotu
    val=$(echo "$1" | grep -o "\"$2\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" | head -1 | sed "s/\"$2\"[[:space:]]*:[[:space:]]*\"//" | sed 's/"$//')
    if [ -n "$val" ]; then
        echo "$val"
        return
    fi
    # Zkusit numerickou hodnotu
    val=$(echo "$1" | grep -o "\"$2\"[[:space:]]*:[[:space:]]*[0-9]*" | head -1 | sed "s/\"$2\"[[:space:]]*:[[:space:]]*//" )
    echo "$val"
}

## assert_json_eq "json_string" "key" "expected_value" "message"
assert_json_eq() {
    local actual
    actual=$(json_get "$1" "$2")
    assert_eq "$actual" "$3" "${4:-JSON $2 == $3}"
}

## run_test "test_function_name"
run_test() {
    _TOTAL=$((_TOTAL + 1))
    printf "  %-55s" "$1"
    local _output
    _output=$($1 2>&1)
    if [ $? -eq 0 ]; then
        echo -e "${_GREEN}OK${_NC}"
        _PASSED=$((_PASSED + 1))
    else
        echo -e "${_RED}FAIL${_NC}"
        echo "$_output" | grep -E "(FAIL|Error)" | head -5
        _FAILED=$((_FAILED + 1))
    fi
}

## test_summary - výpis souhrnu a návratový kód
test_summary() {
    echo ""
    echo "--- $_PASSED passed, $_FAILED failed, $_TOTAL total ---"
    if [ $_FAILED -ne 0 ]; then
        return 1
    fi
    return 0
}
