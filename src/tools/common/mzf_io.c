/**
 * @file   mzf_io.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  Implementace sdílených I/O helperů rodiny bin2mzf.
 *
 * Tělo modulu `mzf_io` - viz hlavičku mzf_io.h pro popis API.
 * Helpery jsou sdílené mezi `bin2mzf`, `mzf-cat` a `mzf-paste`.
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
#include <errno.h>
#include <unistd.h>

#include "tools/common/mzf_io.h"

#include "libs/mzf/mzf.h"
#include "libs/generic_driver/generic_driver.h"
#include "libs/generic_driver/memory_driver.h"

/**
 * @brief Načte celý obsah souboru do bufferu od offsetu `buf_offset`.
 *
 * Implementace: `fopen("rb")` -> chunked `fread` smyčka -> probe na
 * jeden bajt navíc (detekce přetečení) -> `fclose`. Při I/O chybě
 * nebo přetečení vypíše hlášku na stderr a vrátí -1.
 *
 * Sjednocená error hláška pro overflow obsahuje substring `"exceed"`
 * a `"maximum body size"` (regression invariant - test
 * `test_mzf_cat_basic.sh:201` na to drží grep `"exceed"`).
 *
 * @param path         Cesta k souboru.
 * @param buf          Cílový buffer.
 * @param buf_offset   Offset v `buf`.
 * @param buf_capacity Celková kapacita bufferu.
 * @param bytes_read   Výstup: počet načtených bajtů.
 *
 * @return 0 OK, -1 chyba.
 */
int read_file_to_buffer_at ( const char *path, uint8_t *buf,
                             size_t buf_offset, size_t buf_capacity,
                             size_t *bytes_read ) {
    if ( !path || !buf || !bytes_read ) return -1;
    if ( buf_offset > buf_capacity ) return -1;

    FILE *fp = fopen ( path, "rb" );
    if ( !fp ) {
        fprintf ( stderr, "Error: cannot open input file '%s': %s\n",
                  path, strerror ( errno ) );
        return -1;
    }

    size_t cap_left = buf_capacity - buf_offset;
    size_t total = 0;
    while ( total < cap_left ) {
        size_t n = fread ( buf + buf_offset + total, 1, cap_left - total, fp );
        if ( n == 0 ) break;
        total += n;
    }
    int err = ferror ( fp );
    /* detekce přetečení: pokud jsme spotřebovali celou volnou kapacitu,
       zkusíme přečíst další bajt - pokud uspěje, soubor je delší než
       kapacita */
    int extra = 0;
    if ( total == cap_left && !err ) {
        uint8_t probe;
        if ( fread ( &probe, 1, 1, fp ) == 1 ) extra = 1;
    }
    fclose ( fp );

    if ( err ) {
        fprintf ( stderr, "Error: read error on '%s'\n", path );
        return -1;
    }
    if ( extra ) {
        fprintf ( stderr,
                  "Error: input file '%s' too large (would exceed maximum body size of %u bytes)\n",
                  path, MZF_MAX_BODY_SIZE );
        return -1;
    }

    *bytes_read = total;
    return 0;
}

/**
 * @brief Aplikuje zarovnání nebo vynucenou velikost na tělo souboru.
 *
 * Implementace 1:1 z `bin2mzf.c` před refactorem (sémanticky shodná
 * s lokální kopií v `mzf_cat.c`). Slovo `"truncated"` v warningu je
 * regression invariant pro test `test_bin2mzf_align.sh`.
 *
 * @param body_buf      In/out buffer s tělem.
 * @param buf_capacity  Kapacita bufferu.
 * @param body_size     In/out velikost tělesa.
 * @param align_block   --align-block (0 = nepoužito).
 * @param align_to      --align-to (validní jen při `has_align_to`).
 * @param has_align_to  Rozlišení align_to=0 vs nepoužito.
 * @param filler        Padding bajt.
 *
 * @return 0 OK, -1 chyba (overflow / nedostatek kapacity).
 */
int apply_alignment ( uint8_t *body_buf, size_t buf_capacity,
                      size_t *body_size,
                      uint32_t align_block,
                      uint32_t align_to, bool has_align_to,
                      uint8_t filler ) {
    if ( !body_buf || !body_size ) return -1;

    /* --align-block: pad na násobek N */
    if ( align_block != 0 ) {
        size_t cur = *body_size;
        /* zarovnání nahoru: ((cur + N - 1) / N) * N */
        size_t aligned = ( ( cur + align_block - 1 ) / align_block ) * align_block;
        if ( aligned > MZF_MAX_BODY_SIZE ) {
            fprintf ( stderr,
                      "Error: --align-block %u would produce %zu bytes (max %u)\n",
                      align_block, aligned, MZF_MAX_BODY_SIZE );
            return -1;
        }
        if ( aligned > buf_capacity ) {
            fprintf ( stderr,
                      "Error: align buffer overflow (need %zu, have %zu)\n",
                      aligned, buf_capacity );
            return -1;
        }
        if ( aligned > cur ) {
            memset ( body_buf + cur, filler, aligned - cur );
        }
        *body_size = aligned;
    }

    /* --align-to / --size: vynutit přesnou velikost */
    if ( has_align_to ) {
        size_t cur = *body_size;
        size_t target = (size_t) align_to;
        if ( target > MZF_MAX_BODY_SIZE ) {
            fprintf ( stderr,
                      "Error: target size %zu exceeds MZF body limit %u\n",
                      target, MZF_MAX_BODY_SIZE );
            return -1;
        }
        if ( target > buf_capacity ) {
            fprintf ( stderr,
                      "Error: align buffer overflow (need %zu, have %zu)\n",
                      target, buf_capacity );
            return -1;
        }
        if ( cur > target ) {
            /* slovo "truncated" je povinný invariant pro test */
            fprintf ( stderr,
                      "Warning: body truncated from %zu to %zu bytes\n",
                      cur, target );
            *body_size = target;
        } else if ( cur < target ) {
            memset ( body_buf + cur, filler, target - cur );
            *body_size = target;
        }
        /* cur == target: no-op */
    }

    return 0;
}

/**
 * @brief Zapíše MZF strukturu do souboru přes generic_driver.
 *
 * Implementace 1:1 z `bin2mzf.c` před refactorem.
 *
 * @param path Cílová cesta.
 * @param mzf  MZF data.
 * @return 0 OK, -1 chyba.
 */
int write_mzf_to_file ( const char *path, const st_MZF *mzf ) {
    st_DRIVER drv;
    generic_driver_file_init ( &drv );

    st_HANDLER h;
    /* generic_driver_open_file přijímá `char *` (ne const) */
    if ( !generic_driver_open_file ( &h, &drv, (char *) path, FILE_DRIVER_OPMODE_W ) ) {
        fprintf ( stderr, "Error: cannot create output file '%s': %s\n",
                  path, generic_driver_error_message ( &h, &drv ) );
        return -1;
    }

    en_MZF_ERROR rc = mzf_save ( &h, mzf );
    if ( rc != MZF_OK ) {
        fprintf ( stderr, "Error: failed to write MZF: %s\n", mzf_error_string ( rc ) );
        generic_driver_close ( &h );
        return -1;
    }
    generic_driver_close ( &h );
    return 0;
}

/**
 * @brief Zapíše MZF strukturu na stdout přes paměťový driver.
 *
 * Implementace 1:1 z `bin2mzf.c` před refactorem. Otevře paměťový
 * handler s přesnou velikostí, nechá `mzf_save` zapsat do paměti
 * a pak přesype obsah na stdout přes `fwrite`.
 *
 * @param mzf MZF data.
 * @return 0 OK, -1 chyba.
 */
int write_mzf_to_stdout ( const st_MZF *mzf ) {
    uint32_t total_size = (uint32_t) MZF_HEADER_SIZE + mzf->body_size;

    st_HANDLER h;
    if ( !generic_driver_open_memory ( &h, &g_memory_driver_static, total_size ) ) {
        fprintf ( stderr, "Error: cannot allocate memory buffer (%u bytes)\n", total_size );
        return -1;
    }

    en_MZF_ERROR rc = mzf_save ( &h, mzf );
    if ( rc != MZF_OK ) {
        fprintf ( stderr, "Error: failed to encode MZF: %s\n", mzf_error_string ( rc ) );
        generic_driver_close ( &h );
        return -1;
    }

    size_t written = fwrite ( h.spec.memspec.ptr, 1, h.spec.memspec.size, stdout );
    if ( written != h.spec.memspec.size ) {
        fprintf ( stderr, "Error: short write on stdout (%zu of %zu bytes)\n",
                  written, h.spec.memspec.size );
        generic_driver_close ( &h );
        return -1;
    }
    generic_driver_close ( &h );
    return 0;
}

/**
 * @brief Načte MZF soubor z disku do nově alokované struktury `st_MZF`.
 *
 * Implementace 1:1 z `mzf_paste.c` (před refactorem - funkce
 * `load_target_mzf`). Použije memory handler pro `mzf_load`, takže
 * automaticky proběhne konverze endianity a alokace těla. Truncated
 * body workaround není zahrnut - volající (`mzf-paste`, `mzf-hdr`)
 * očekávají validní MZF.
 *
 * @param path    Cesta k MZF souboru.
 * @param out_mzf Výstup: alokovaná struktura `st_MZF`.
 * @return 0 OK, -1 chyba.
 */
int load_mzf_from_file ( const char *path, st_MZF **out_mzf ) {
    if ( !path || !out_mzf ) return -1;
    *out_mzf = NULL;

    /* Buffer pro celý MZF soubor */
    size_t buf_cap = (size_t) MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE + 1;
    uint8_t *buf = malloc ( buf_cap );
    if ( !buf ) {
        fprintf ( stderr, "Error: cannot allocate %zu bytes for input buffer\n", buf_cap );
        return -1;
    }

    size_t file_size = 0;
    if ( read_file_to_buffer_at ( path, buf, 0, buf_cap, &file_size ) != 0 ) {
        free ( buf );
        return -1;
    }
    if ( file_size < MZF_HEADER_SIZE ) {
        fprintf ( stderr, "Error: target file '%s' shorter than MZF header (%zu bytes, need at least %u)\n",
                  path, file_size, (unsigned) MZF_HEADER_SIZE );
        free ( buf );
        return -1;
    }

    /* Naplnění memory handleru přes generic_driver */
    st_HANDLER h;
    if ( !generic_driver_open_memory ( &h, &g_memory_driver_static,
                                        (uint32_t) file_size ) ) {
        fprintf ( stderr, "Error: cannot allocate memory handler (%zu bytes)\n", file_size );
        free ( buf );
        return -1;
    }
    if ( generic_driver_write ( &h, 0, buf, (uint32_t) file_size ) != EXIT_SUCCESS ) {
        fprintf ( stderr, "Error: cannot fill memory handler\n" );
        generic_driver_close ( &h );
        free ( buf );
        return -1;
    }

    en_MZF_ERROR err = MZF_OK;
    st_MZF *mzf = mzf_load ( &h, &err );
    generic_driver_close ( &h );
    free ( buf );
    if ( !mzf ) {
        fprintf ( stderr, "Error: cannot parse MZF '%s': %s\n",
                  path, mzf_error_string ( err ) );
        return -1;
    }

    *out_mzf = mzf;
    return 0;
}

/**
 * @brief Zapíše MZF in-place s atomic tmp + rename.
 *
 * Implementace 1:1 z `mzf_paste.c` (před refactorem - funkce
 * `write_target_in_place`). Sestaví dočasné jméno `<path>.tmp`, zapíše
 * data, na Win32 odstraní existující target a pak rename.
 *
 * @param path Cílová cesta.
 * @param mzf  MZF data.
 * @return 0 OK, -1 chyba.
 */
int write_mzf_in_place ( const char *path, const st_MZF *mzf ) {
    if ( !path || !mzf ) return -1;

    size_t tmp_len = strlen ( path ) + 5; /* ".tmp" + '\0' */
    char *tmp_path = malloc ( tmp_len );
    if ( !tmp_path ) {
        fprintf ( stderr, "Error: cannot allocate temporary path buffer\n" );
        return -1;
    }
    snprintf ( tmp_path, tmp_len, "%s.tmp", path );

    if ( write_mzf_to_file ( tmp_path, mzf ) != 0 ) {
        unlink ( tmp_path );
        free ( tmp_path );
        return -1;
    }

#ifdef _WIN32
    /* Windows: rename přes existující soubor selže. Smažeme target nejdřív.
       ENOENT se ignoruje (target nemusí existovat - třeba race s jiným procesem). */
    if ( unlink ( path ) != 0 && errno != ENOENT ) {
        fprintf ( stderr, "Error: cannot remove target '%s' before rename: %s\n",
                  path, strerror ( errno ) );
        unlink ( tmp_path );
        free ( tmp_path );
        return -1;
    }
#endif

    if ( rename ( tmp_path, path ) != 0 ) {
        fprintf ( stderr, "Error: cannot rename '%s' to '%s': %s\n",
                  tmp_path, path, strerror ( errno ) );
        unlink ( tmp_path );
        free ( tmp_path );
        return -1;
    }

    free ( tmp_path );
    return 0;
}
