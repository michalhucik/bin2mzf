/**
 * @file    argparse_helpers.c
 * @brief   Implementace sdílených parserů argumentů pro CLI rodinu bin2mzf.
 *
 * Funkce v tomto souboru byly přesunuty 1:1 z `bin2mzf.c` (původní verze
 * 0.2.0) do `src/tools/common/`, aby je mohla sdílet i druhá binárka
 * rodiny `mzf-info`. Sémantika všech parserů (vstupní formáty, errno
 * pattern, hraniční hodnoty) je zachována identická.
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "libs/mzf/mzf.h"
#include "argparse_helpers.h"

/* Implementace `argparse_uint16` - viz Doxygen v hlavičkovém souboru. */
int argparse_uint16 ( const char *s, uint16_t *out ) {
    if ( !s || !*s || !out ) return -1;
    char *end = NULL;
    errno = 0;
    /* base 0 = autodetekce 0x.. / 0.. / decimal */
    unsigned long v = strtoul ( s, &end, 0 );
    if ( errno != 0 ) return -1;
    if ( !end || *end != '\0' ) return -1;
    if ( v > 0xFFFFu ) return -1;
    *out = (uint16_t) v;
    return 0;
}

/* Implementace `argparse_byte` - viz Doxygen v hlavičkovém souboru. */
int argparse_byte ( const char *s, uint8_t *out ) {
    if ( !out ) return -1;
    uint16_t v = 0;
    if ( argparse_uint16 ( s, &v ) != 0 ) return -1;
    if ( v > 0xFFu ) return -1;
    *out = (uint8_t) v;
    return 0;
}

/* Implementace `argparse_charset` - viz Doxygen v hlavičkovém souboru. */
int argparse_charset ( const char *s, en_TOOL_CHARSET *out ) {
    if ( !s || !out ) return -1;
    if ( strcmp ( s, "eu" ) == 0 )      { *out = TOOL_CHARSET_EU;      return 0; }
    if ( strcmp ( s, "jp" ) == 0 )      { *out = TOOL_CHARSET_JP;      return 0; }
    if ( strcmp ( s, "utf8-eu" ) == 0 ) { *out = TOOL_CHARSET_UTF8_EU; return 0; }
    if ( strcmp ( s, "utf8-jp" ) == 0 ) { *out = TOOL_CHARSET_UTF8_JP; return 0; }
    if ( strcmp ( s, "none" ) == 0 )    { *out = TOOL_CHARSET_NONE;    return 0; }
    return -1;
}

/* Implementace `argparse_ftype` - viz Doxygen v hlavičkovém souboru.
 *
 * Edge case z původního bin2mzf.c (řádek 353-355): pokud je vstup příliš
 * dlouhý pro symbolické jméno (přesahuje sizeof(buf)-1), zkopírovaná
 * lowercase varianta není kompletní. V takovém případě se nepoužije
 * žádné srovnání symbolického jména a parser propadne rovnou na číselný
 * fallback. Tento invariant je explicitně testovaný (regression). */
int argparse_ftype ( const char *s, uint8_t *out ) {
    if ( !s || !*s || !out ) return -1;

    /* lowercase kopie do bufferu pro case-insensitive porovnání */
    char buf[8];
    size_t i = 0;
    while ( s[i] && i < sizeof ( buf ) - 1 ) {
        buf[i] = (char) tolower ( (unsigned char) s[i] );
        i++;
    }
    buf[i] = '\0';
    if ( s[i] != '\0' ) {
        /* příliš dlouhé - není symbolické, zkusit jako číslo dále */
    } else {
        if ( strcmp ( buf, "obj" ) == 0 ) { *out = MZF_FTYPE_OBJ; return 0; }
        if ( strcmp ( buf, "btx" ) == 0 ) { *out = MZF_FTYPE_BTX; return 0; }
        if ( strcmp ( buf, "bsd" ) == 0 ) { *out = MZF_FTYPE_BSD; return 0; }
        if ( strcmp ( buf, "brd" ) == 0 ) { *out = MZF_FTYPE_BRD; return 0; }
        if ( strcmp ( buf, "rb" ) == 0 )  { *out = MZF_FTYPE_RB;  return 0; }
    }

    /* fallback: číselný parser */
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul ( s, &end, 0 );
    if ( errno != 0 ) return -1;
    if ( !end || *end != '\0' ) return -1;
    if ( v > 0xFFu ) return -1;
    *out = (uint8_t) v;
    return 0;
}
