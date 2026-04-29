/**
 * @file    cpm_helpers.c
 * @author  Michal Hucik <hucik@ordoz.com>
 * @brief   Implementace sdílených parserů a transformací CP/M konvence MZF.
 *
 * Tělo modulu `cpm_helpers` - viz hlavičku cpm_helpers.h pro popis API.
 * Funkce byly vyextrahovány z původně lokálních duplikátů v `bin2mzf.c`
 * a `mzf_info.c`. Refactor sémanticky 1:1 (signatury, návratové hodnoty,
 * error hlášky beze změny - kromě prefixu hlášek `Error:` ponechán
 * podle původní implementace).
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "tools/common/cpm_helpers.h"
#include "tools/common/cpm_constants.h"
#include "tools/common/text_field.h"

/* -------------------------------------------------------------------------
 *  mzf_cpm_parse_attr
 * -----------------------------------------------------------------------*/

int mzf_cpm_parse_attr ( const char *s, uint8_t *out_mask ) {
    if ( !out_mask ) return -1;
    *out_mask = 0;
    if ( s == NULL || *s == '\0' ) return 0;

    uint8_t acc = 0;
    const char *p = s;
    while ( *p ) {
        /* skip oddělovače: čárka, mezera, tab */
        while ( *p == ',' || *p == ' ' || *p == '\t' ) p++;
        if ( !*p ) break;

        /* longest-match: `r/o` (3) > `ro` (2) > `sys` (3) > `arc` (3) */
        if ( strncasecmp ( p, "r/o", 3 ) == 0 ) {
            acc |= MZF_CPM_ATTR_RO;
            p += 3;
        } else if ( strncasecmp ( p, "ro", 2 ) == 0 ) {
            acc |= MZF_CPM_ATTR_RO;
            p += 2;
        } else if ( strncasecmp ( p, "sys", 3 ) == 0 ) {
            acc |= MZF_CPM_ATTR_SYS;
            p += 3;
        } else if ( strncasecmp ( p, "arc", 3 ) == 0 ) {
            acc |= MZF_CPM_ATTR_ARC;
            p += 3;
        } else {
            fprintf ( stderr, "Error: invalid --cpm-attr token at '%s'\n", p );
            return -1;
        }
    }

    *out_mask = acc;
    return 0;
}

/* -------------------------------------------------------------------------
 *  mzf_cpm_normalize_field
 * -----------------------------------------------------------------------*/

int mzf_cpm_normalize_field ( const char *in, unsigned max_len,
                              uint8_t *out_buf, const char *field_label ) {
    if ( !out_buf || !field_label ) return -1;
    if ( in == NULL ) {
        fprintf ( stderr, "Error: --cpm-%s is required\n", field_label );
        return -1;
    }
    size_t len = strlen ( in );
    if ( len == 0 ) {
        fprintf ( stderr, "Error: --cpm-%s must not be empty\n", field_label );
        return -1;
    }
    if ( len > (size_t) max_len ) {
        fprintf ( stderr, "Error: --cpm-%s too long (max %u, got %zu)\n",
                  field_label, max_len, len );
        return -1;
    }

    /* pre-fill mezerami */
    memset ( out_buf, 0x20, max_len );

    for ( size_t i = 0; i < len; i++ ) {
        unsigned char c = (unsigned char) in[i];
        if ( c < 0x20 || c > 0x7E ) {
            fprintf ( stderr,
                      "Error: --cpm-%s contains non-ASCII byte 0x%02X at position %zu\n",
                      field_label, c, i );
            return -1;
        }
        out_buf[i] = (uint8_t) toupper ( c );
    }
    return 0;
}

/* -------------------------------------------------------------------------
 *  mzf_cpm_build_fname
 * -----------------------------------------------------------------------*/

int mzf_cpm_build_fname ( const char *cpm_name, const char *cpm_ext,
                          uint8_t attr_mask,
                          uint8_t out_buf[MZF_CPM_FNAME_LEN] ) {
    if ( !out_buf ) return -1;

    /* jméno -> out_buf[0..7] */
    if ( mzf_cpm_normalize_field ( cpm_name, MZF_CPM_NAME_LEN,
                                    &out_buf[0], "name" ) != 0 ) {
        return -1;
    }

    /* tečka */
    out_buf[MZF_CPM_DOT_POS] = '.';

    /* přípona -> out_buf[9..11] */
    if ( mzf_cpm_normalize_field ( cpm_ext, MZF_CPM_EXT_LEN,
                                    &out_buf[MZF_CPM_EXT_POS], "ext" ) != 0 ) {
        return -1;
    }

    /* atributy v bitu 7 přípony */
    if ( attr_mask & MZF_CPM_ATTR_RO )  out_buf[MZF_CPM_EXT_POS]     |= MZF_CPM_ATTR_BIT;
    if ( attr_mask & MZF_CPM_ATTR_SYS ) out_buf[MZF_CPM_EXT_POS + 1] |= MZF_CPM_ATTR_BIT;
    if ( attr_mask & MZF_CPM_ATTR_ARC ) out_buf[MZF_CPM_EXT_POS + 2] |= MZF_CPM_ATTR_BIT;

    /* CR na pozici 12 - vnitřní layout 8.3+CR */
    out_buf[MZF_CPM_TERM_POS] = 0x0D;

    return 0;
}

/* -------------------------------------------------------------------------
 *  mzf_cpm_decode_header
 * -----------------------------------------------------------------------*/

bool mzf_cpm_decode_header ( const st_MZF_HEADER *hdr,
                             uint8_t *out_name, size_t *out_name_len,
                             uint8_t *out_ext,  size_t *out_ext_len,
                             uint8_t *out_attr_mask ) {
    if ( hdr->ftype != MZF_CPM_FTYPE ) {
        return false;
    }
    if ( hdr->fname.name[MZF_CPM_DOT_POS] != (uint8_t) '.' ) {
        return false;
    }
    uint8_t term = hdr->fname.name[MZF_CPM_TERM_POS];
    if ( term != 0x0Du && term != 0x00u ) {
        return false;
    }

    /* Kopie a rtrim jména. */
    for ( size_t i = 0; i < MZF_CPM_NAME_LEN; i++ ) {
        out_name[i] = hdr->fname.name[i];
    }
    *out_name_len = rtrim_ascii ( out_name, MZF_CPM_NAME_LEN );

    /* Strip bitu 7 v každém bajtu přípony - PŘED rtrim, jinak by 0xC3/0xCF/0xCD
       nebyly mezery a rtrim by nic neudělal. Bit 7 -> atribut. */
    uint8_t attr_mask = 0;
    for ( size_t i = 0; i < MZF_CPM_EXT_LEN; i++ ) {
        uint8_t raw = hdr->fname.name[MZF_CPM_EXT_POS + i];
        if ( raw & MZF_CPM_ATTR_BIT ) {
            switch ( i ) {
                case 0: attr_mask |= MZF_CPM_ATTR_RO;  break;
                case 1: attr_mask |= MZF_CPM_ATTR_SYS; break;
                case 2: attr_mask |= MZF_CPM_ATTR_ARC; break;
                default: break;
            }
        }
        out_ext[i] = (uint8_t) ( raw & 0x7Fu );
    }
    *out_ext_len = rtrim_ascii ( out_ext, MZF_CPM_EXT_LEN );
    *out_attr_mask = attr_mask;
    return true;
}
