/**
 * @file    text_field.c
 * @brief   Implementace sdílených helperů pro parsování argumentů
 *          velikosti/zarovnání a transformaci textových polí MZF hlavičky.
 *
 * Funkce sem byly přesunuty z `bin2mzf.c` (refactor sémanticky 1:1) -
 * první konzument je `bin2mzf`, druhý `mzf-cat`, další přibudou
 * (`mzf-paste`, `mzf-hdr`).
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

#include "libs/mzf/mzf.h"
#include "libs/sharpmz_ascii/sharpmz_ascii.h"
#include "tools/common/argparse_helpers.h"
#include "text_field.h"

/* -------------------------------------------------------------------------
 *  parse_size_arg
 * -----------------------------------------------------------------------*/

int parse_size_arg ( const char *s, uint16_t *out, bool *has_size ) {
    if ( !s || !out || !has_size ) return -1;
    if ( strcmp ( s, "auto" ) == 0 ) {
        *out = 0;
        *has_size = false;
        return 0;
    }
    uint16_t v = 0;
    if ( argparse_uint16 ( s, &v ) != 0 ) return -1;
    if ( v == 0 ) return -1;
    /* horní mez 0xFFFF už je vyčerpaná typem uint16_t a navíc odpovídá
       MZF_MAX_BODY_SIZE - tj. žádný extra check není potřeba */
    *out = v;
    *has_size = true;
    return 0;
}

/* -------------------------------------------------------------------------
 *  parse_align_value
 * -----------------------------------------------------------------------*/

int parse_align_value ( const char *s, uint32_t *out ) {
    if ( !s || !*s || !out ) return -1;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul ( s, &end, 0 );
    if ( errno != 0 ) return -1;
    if ( !end || *end != '\0' ) return -1;
    if ( v == 0 ) return -1;
    if ( v > (unsigned long) MZF_MAX_BODY_SIZE ) return -1;
    *out = (uint32_t) v;
    return 0;
}

/* -------------------------------------------------------------------------
 *  parse_c_escapes
 * -----------------------------------------------------------------------*/

int parse_c_escapes ( const char *src, uint8_t *dst, size_t dst_max,
                      size_t *out_len ) {
    if ( !src || !dst || !out_len ) return -1;
    size_t w = 0;
    size_t i = 0;
    while ( src[i] != '\0' ) {
        unsigned char ch = (unsigned char) src[i];
        if ( ch != '\\' ) {
            if ( w >= dst_max ) return -1;
            dst[w++] = ch;
            i++;
            continue;
        }
        /* escape mód: musí následovat alespoň jeden znak */
        unsigned char esc = (unsigned char) src[i + 1];
        if ( esc == '\0' ) return -1;
        switch ( esc ) {
            case '\\':
                if ( w >= dst_max ) return -1;
                dst[w++] = 0x5C;
                i += 2;
                break;
            case 'n':
                if ( w >= dst_max ) return -1;
                dst[w++] = 0x0A;
                i += 2;
                break;
            case 'r':
                if ( w >= dst_max ) return -1;
                dst[w++] = 0x0D;
                i += 2;
                break;
            case 't':
                if ( w >= dst_max ) return -1;
                dst[w++] = 0x09;
                i += 2;
                break;
            case 'x': {
                /* musí následovat právě 2 hex číslice */
                unsigned char h1 = (unsigned char) src[i + 2];
                unsigned char h2 = ( h1 == '\0' ) ? 0 : (unsigned char) src[i + 3];
                if ( h1 == '\0' || h2 == '\0' ) return -1;
                if ( !isxdigit ( h1 ) || !isxdigit ( h2 ) ) return -1;
                int v1 = ( h1 >= '0' && h1 <= '9' ) ? ( h1 - '0' )
                       : ( h1 >= 'a' && h1 <= 'f' ) ? ( h1 - 'a' + 10 )
                       : ( h1 - 'A' + 10 );
                int v2 = ( h2 >= '0' && h2 <= '9' ) ? ( h2 - '0' )
                       : ( h2 >= 'a' && h2 <= 'f' ) ? ( h2 - 'a' + 10 )
                       : ( h2 - 'A' + 10 );
                if ( w >= dst_max ) return -1;
                dst[w++] = (uint8_t) ( ( v1 << 4 ) | v2 );
                i += 4;
                break;
            }
            default:
                /* neznámá escape sekvence */
                return -1;
        }
    }
    *out_len = w;
    return 0;
}

/* -------------------------------------------------------------------------
 *  transform_text_field
 * -----------------------------------------------------------------------*/

int transform_text_field ( const char *in,
                           int charset,
                           bool upper,
                           en_TEXT_FIELD_KIND field_kind,
                           size_t max_out,
                           uint8_t *out,
                           size_t *out_len ) {
    if ( !out || !out_len ) return -1;

    if ( in == NULL ) {
        *out_len = 0;
        return 0;
    }

    /* per-pole detekce escape - escape vypíná charset i upper pro toto pole */
    bool has_escape = ( strchr ( in, '\\' ) != NULL );
    int  effective_charset = has_escape ? TOOL_CHARSET_NONE : charset;
    bool effective_upper   = has_escape ? false : upper;

    const char *field_name = ( field_kind == TEXT_FIELD_NAME ) ? "name" : "comment";

    /* tmp buffer s nadměrnou kapacitou - pojme i nezkonvertovaný UTF-8 */
    const size_t tmp_cap = max_out * 4 + 1;
    uint8_t tmp[tmp_cap];
    size_t  tmp_len = 0;

    if ( has_escape ) {
        if ( parse_c_escapes ( in, tmp, tmp_cap, &tmp_len ) != 0 ) {
            fprintf ( stderr, "Error: malformed escape sequence in %s\n", field_name );
            return -1;
        }
    } else {
        size_t in_len = strlen ( in );
        /* defenzivní ořez na sizeof(tmp)-1 (pro null terminátor pro sharpmz) */
        if ( in_len > tmp_cap - 1 ) {
            in_len = tmp_cap - 1;
        }
        memcpy ( tmp, in, in_len );
        tmp[in_len] = '\0';
        tmp_len = in_len;
    }

    /* uppercase a-z -> A-Z (jen ASCII rozsah) */
    if ( effective_upper ) {
        for ( size_t i = 0; i < tmp_len; i++ ) {
            if ( tmp[i] >= 0x61 && tmp[i] <= 0x7A ) {
                tmp[i] = (uint8_t) ( tmp[i] - 0x20 );
            }
        }
    }

    /* charset konverze do out_tmp s nadměrnou kapacitou */
    uint8_t out_tmp[tmp_cap];
    size_t  out_tmp_len = 0;

    switch ( effective_charset ) {
        case TOOL_CHARSET_EU:
        case TOOL_CHARSET_UTF8_EU: {
            int n = sharpmz_str_from_utf8 ( (const char *) tmp, out_tmp, tmp_cap,
                                            SHARPMZ_CHARSET_EU );
            if ( n < 0 ) return -1;
            out_tmp_len = (size_t) n;
            break;
        }
        case TOOL_CHARSET_JP:
        case TOOL_CHARSET_UTF8_JP: {
            int n = sharpmz_str_from_utf8 ( (const char *) tmp, out_tmp, tmp_cap,
                                            SHARPMZ_CHARSET_JP );
            if ( n < 0 ) return -1;
            out_tmp_len = (size_t) n;
            break;
        }
        case TOOL_CHARSET_NONE:
        default:
            memcpy ( out_tmp, tmp, tmp_len );
            out_tmp_len = tmp_len;
            break;
    }

    /* validace délky a kopírování do out */
    if ( out_tmp_len <= max_out ) {
        memcpy ( out, out_tmp, out_tmp_len );
        *out_len = out_tmp_len;
        return 0;
    }

    /* overflow */
    if ( field_kind == TEXT_FIELD_NAME ) {
        fprintf ( stderr,
                  "Error: name exceeds %zu bytes after charset conversion (got %zu bytes)\n",
                  max_out, out_tmp_len );
        return -1;
    }

    /* COMMENT: warning + truncate. Slovo "truncated" povinné! */
    fprintf ( stderr,
              "Warning: comment truncated to %zu bytes (got %zu bytes after charset conversion)\n",
              max_out, out_tmp_len );
    memcpy ( out, out_tmp, max_out );
    *out_len = max_out;
    return 0;
}

/* -------------------------------------------------------------------------
 *  rtrim_ascii
 * -----------------------------------------------------------------------*/

size_t rtrim_ascii ( const uint8_t *buf, size_t len ) {
    while ( len > 0 && buf[len - 1] == 0x20u ) {
        len--;
    }
    return len;
}
