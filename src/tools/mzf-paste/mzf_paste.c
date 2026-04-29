/**
 * @file   mzf_paste.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  CLI nástroj rodiny bin2mzf - vložení/přepsání binárních dat
 *         na offset těla existujícího MZF souboru.
 *
 * Načte cílový MZF (`target.mzf`) a vstupní binární data (`input.bin`),
 * provede in-memory operaci insert/overwrite/gap nad tělem cíle podle
 * voleb CLI a zapíše výsledný MZF buď in-place (atomic tmp + rename)
 * nebo do souboru zadaného přes `-o`.
 *
 * Verze 0.1.0 (MVP) scope:
 *  - `--insert` (default) - data se vloží na offset, zbytek těla se posune.
 *    Velikost těla roste o input_size.
 *  - `--overwrite` (mutex s --insert) - data přepíšou existující bajty od offsetu.
 *  - `--extend` - povolí, aby `--overwrite` rozšířil tělo, pokud vstup
 *    přesahuje konec.
 *  - `--at OFFSET` - offset v těle cíle. Hex `0x100`, decimal nebo
 *    keyword `end` (= aktuální fsize).
 *  - `--filler BYTE` - výplň pro gap (offset > current_size). Default 0x00.
 *  - `--from-mzf` - vstup je MZF (přeskoč 128B hlavičku, použij jen tělo).
 *  - `-o, --output FILE` - výstupní MZF. Bez tohoto flagu se target
 *    modifikuje in-place atomicky přes tmp + rename.
 *  - `--version`, `--lib-versions`, `--help`.
 *
 * Mimo scope MVP (řeší další tasky / release v1.0.0):
 *  - Změna polí hlavičky (`-n`/`-c`/`-l`/`-e`) - to dělá `mzf-hdr`.
 *  - `--charset`/`--upper` transformace - target hlavička se zachovává tak,
 *    jak je.
 *  - `--cpm` mód - paste data nemají CP/M sémantiku.
 *  - `--auto-size`/`--no-auto-size` - auto-size je default a jediné chování.
 *  - `--align-block`/`--align-to` - paste neformátuje výsledek.
 *  - Streaming - paste celé fitne do paměti (max 65535 bajtů).
 *  - Multi-input - jen jeden positional input + jeden positional target.
 *  - stdin pro vstup - paste je file-only.
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "libs/mzf/mzf.h"
#include "libs/generic_driver/generic_driver.h"
#include "libs/generic_driver/memory_driver.h"
#include "libs/sharpmz_ascii/sharpmz_ascii.h"
#include "libs/endianity/endianity.h"
#include "tools/common/bin2mzf_cli_version.h"
#include "tools/common/argparse_helpers.h"
#include "tools/common/mzf_io.h"

/** @brief Verze nástroje mzf-paste. */
#define MZF_PASTE_VERSION "0.1.0"

/**
 * @brief Volby CLI parsované z argumentů `mzf-paste`.
 *
 * Drží referenční ukazatele do `argv` (žádná vlastní alokace), životnost
 * vázaná na životnost procesu.
 *
 * Mutex/povinná pole (`mode_insert` × `mode_overwrite`, `has_at`) jsou
 * validovány v `parse_args` po zpracování všech argumentů.
 */
typedef struct st_MZF_PASTE_OPTIONS {
    const char *input_path;     /**< vstupní soubor (povinný, positional) */
    const char *target_path;    /**< cílový MZF (povinný, positional) */
    const char *output_path;    /**< -o/--output, NULL = in-place modifikace */
    bool mode_insert;           /**< true pokud --insert (nebo default) */
    bool mode_overwrite;        /**< true pokud --overwrite (mutex s --insert) */
    bool allow_extend;          /**< true pokud --extend (jen s --overwrite) */
    bool from_mzf;              /**< true pokud --from-mzf */
    bool has_at;                /**< true pokud uživatel zadal --at */
    uint32_t at_value;          /**< hodnota --at (validní jen pokud !at_is_end) */
    bool at_is_end;             /**< true pokud --at end (resolved v main) */
    uint8_t filler;             /**< --filler BYTE (default 0x00) */
} st_MZF_PASTE_OPTIONS;

static int parse_args ( int argc, char *argv[], st_MZF_PASTE_OPTIONS *opts );
static void print_usage ( FILE *out );
static void print_version ( void );
static void print_lib_versions ( void );
static int argparse_offset ( const char *s, uint32_t *out, bool *is_end );
static int apply_paste_operation (
    const uint8_t *target, size_t target_size,
    const uint8_t *input, size_t input_size,
    size_t offset,
    bool mode_insert,
    bool allow_extend,
    uint8_t filler,
    uint8_t *result, size_t *result_size, size_t result_cap );
/* load_mzf_from_file a write_mzf_in_place jsou ve sdíleném modulu
   `tools/common/mzf_io.h` - bývaly lokální (`load_mzf_from_file`,
   `write_mzf_in_place`), refactor na 2. konzumenta (mzf-hdr). */

/**
 * @brief Vypíše Usage / nápovědu nástroje mzf-paste.
 *
 * Text je v angličtině (locales-ready). Volá se z `--help` (na stdout)
 * i z chybových cest (na stderr).
 *
 * @param out Cílový stream (stdout / stderr).
 */
static void print_usage ( FILE *out ) {
    fprintf ( out,
        "Usage: mzf-paste [options] --at OFFSET [-o OUTPUT.mzf] <INPUT> <TARGET.mzf>\n"
        "\n"
        "Insert or overwrite binary data at a given offset of the body of\n"
        "an existing MZF file. Without -o, TARGET.mzf is modified in place\n"
        "(atomic via temporary file + rename).\n"
        "\n"
        "Required options:\n"
        "      --at OFFSET            Offset within the target body. Accepts\n"
        "                             decimal, 0x-prefixed hex, or the keyword\n"
        "                             'end' (= current body size).\n"
        "\n"
        "Operation mode (mutually exclusive):\n"
        "      --insert               Insert input at OFFSET (default). The\n"
        "                             rest of the body is shifted right and\n"
        "                             body size grows by input length.\n"
        "      --overwrite            Overwrite existing body bytes from\n"
        "                             OFFSET. Body size is preserved unless\n"
        "                             input would extend past the end (see\n"
        "                             --extend).\n"
        "\n"
        "Modifiers:\n"
        "      --extend               With --overwrite, allow input to extend\n"
        "                             body past current end. Without this flag\n"
        "                             such an overwrite is an error.\n"
        "      --filler BYTE          Filler byte for the gap when OFFSET is\n"
        "                             greater than current body size. Default: 0x00.\n"
        "      --from-mzf             Treat INPUT as an MZF file - skip the\n"
        "                             128-byte header and use only its body.\n"
        "\n"
        "Output:\n"
        "  -o, --output FILE          Write the modified MZF to FILE. Without\n"
        "                             this flag TARGET.mzf is modified in place.\n"
        "      --version              Print tool version and exit.\n"
        "      --lib-versions         Print library versions and exit.\n"
        "  -h, --help                 Print this help and exit.\n"
        "\n"
        "Numeric values accept decimal or 0x-prefixed hexadecimal notation.\n"
        "The resulting body size must not exceed 65535 bytes (MZF limit).\n"
    );
}

/**
 * @brief Vypíše verzi nástroje a verzi celé rodiny CLI.
 *
 * Formát: `mzf-paste <tool> (<release-name> <release-version>)`.
 */
static void print_version ( void ) {
    printf ( "mzf-paste %s (%s %s)\n",
             MZF_PASTE_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
}

/**
 * @brief Vypíše verze všech linkovaných knihoven.
 *
 * Slouží k diagnostice ABI mismatch a pro reportování v bug reports.
 */
static void print_lib_versions ( void ) {
    printf ( "mzf-paste %s (%s %s)\n\n",
             MZF_PASTE_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
    printf ( "Library versions:\n" );
    printf ( "  mzf               %s\n", mzf_version () );
    printf ( "  generic_driver    %s\n", generic_driver_version () );
    printf ( "  endianity         %s\n", endianity_version () );
    printf ( "  sharpmz_ascii     %s\n", sharpmz_ascii_version () );
}

/**
 * @brief Naparsuje hodnotu volby `--at OFFSET`.
 *
 * Akceptuje:
 *  - keyword `"end"` -> nastaví `*is_end = true`, `*out` se nemění.
 *  - decimální nebo `0x..` hexadecimální číslo v rozsahu 0..0xFFFF
 *    -> `*is_end = false`, `*out` = hodnota.
 *
 * Záporné hodnoty, řetězce s netriviálními znaky za číslem nebo
 * hodnoty mimo rozsah 0..0xFFFF jsou odmítnuty.
 *
 * @param s      Vstupní řetězec (NULL = chyba).
 * @param out    Výstupní hodnota (zapsána pouze při úspěšném parse čísla).
 * @param is_end Výstup: true pokud `s == "end"`.
 * @return 0 při úspěchu, -1 při chybě (neplatný formát, mimo rozsah, NULL).
 */
static int argparse_offset ( const char *s, uint32_t *out, bool *is_end ) {
    if ( !s || !out || !is_end ) return -1;
    if ( strcmp ( s, "end" ) == 0 ) {
        *is_end = true;
        return 0;
    }
    *is_end = false;
    /* Reuse argparse_uint16 - akceptuje stejné formáty (decimal, 0x..),
       validuje rozsah 0..0xFFFF. */
    uint16_t v;
    if ( argparse_uint16 ( s, &v ) != 0 ) return -1;
    *out = (uint32_t) v;
    return 0;
}

/**
 * @brief Aplikuje paste operaci (insert/overwrite/gap) nad bufferem.
 *
 * Vlastní algoritmus paste choreografie - vstupní `target` a `input`
 * se zkopírují do `result` v správném pořadí podle režimu a offsetu.
 *
 * Režimy:
 *  - INSERT, offset <= target_size: result = target[0..offset]
 *      + input[input_size] + target[offset..target_size].
 *      Velikost: target_size + input_size.
 *  - INSERT, offset > target_size (gap): result = target[0..target_size]
 *      + filler[gap_size] + input[input_size]. gap_size = offset - target_size.
 *      Velikost: target_size + gap_size + input_size = offset + input_size.
 *  - OVERWRITE, end_offset <= target_size: result = target[0..offset]
 *      + input[input_size] + target[end_offset..target_size]. Velikost:
 *      target_size (zachována).
 *  - OVERWRITE, end_offset > target_size (extend nebo gap):
 *      vyžaduje `allow_extend == true`. result = target[0..min(offset,target_size)]
 *      + filler[gap_size pokud offset > target_size] + input[input_size].
 *      Velikost: max(target_size, end_offset).
 *
 * Bounds check: výsledná velikost > MZF_MAX_BODY_SIZE -> error.
 * Buffer overflow: výsledná velikost > result_cap -> error.
 *
 * @param target       Vstup: existující tělo cíle.
 * @param target_size  Velikost cílového těla.
 * @param input        Vstupní data k vložení/přepsání.
 * @param input_size   Velikost vstupních dat.
 * @param offset       Offset (už resolved - end -> target_size).
 * @param mode_insert  true = insert, false = overwrite.
 * @param allow_extend Pro overwrite: povolení extend přes konec.
 * @param filler       Filler bajt pro gap.
 * @param result       Výstupní buffer (předem alokován).
 * @param result_size  Výstup: výsledná velikost těla.
 * @param result_cap   Kapacita výstupního bufferu.
 *
 * @return 0 při úspěchu, -1 při chybě (overflow, buffer overflow,
 *         overwrite past end without --extend). Při chybě píše hlášku
 *         na stderr.
 */
static int apply_paste_operation (
    const uint8_t *target, size_t target_size,
    const uint8_t *input, size_t input_size,
    size_t offset,
    bool mode_insert,
    bool allow_extend,
    uint8_t filler,
    uint8_t *result, size_t *result_size, size_t result_cap )
{
    if ( !target || !input || !result || !result_size ) return -1;

    size_t new_size;
    size_t gap_size = 0;

    if ( mode_insert ) {
        if ( offset > target_size ) {
            /* INSERT s gap: target + filler[gap] + input */
            gap_size = offset - target_size;
            new_size = target_size + gap_size + input_size;
        } else {
            /* INSERT bez gap: target[0..off] + input + target[off..end] */
            new_size = target_size + input_size;
        }
    } else {
        /* OVERWRITE */
        size_t end_offset = offset + input_size;
        if ( offset > target_size ) {
            /* OVERWRITE s gap (offset za koncem) - vyžaduje --extend, jinak
               by tiše vznikl extend těla, což --overwrite default neumožňuje. */
            if ( !allow_extend ) {
                fprintf ( stderr,
                          "Error: --overwrite at offset %zu beyond body end (%zu); use --extend\n",
                          offset, target_size );
                return -1;
            }
            gap_size = offset - target_size;
            new_size = offset + input_size;
        } else if ( end_offset > target_size ) {
            /* OVERWRITE přesahuje konec těla */
            if ( !allow_extend ) {
                fprintf ( stderr,
                          "Error: --overwrite would extend body past current end (%zu -> %zu); use --extend\n",
                          target_size, end_offset );
                return -1;
            }
            new_size = end_offset;
        } else {
            /* OVERWRITE celý uvnitř těla - velikost zachována */
            new_size = target_size;
        }
    }

    /* Bounds check */
    if ( new_size > (size_t) MZF_MAX_BODY_SIZE ) {
        fprintf ( stderr,
                  "Error: resulting body size %zu would exceed maximum %u bytes\n",
                  new_size, MZF_MAX_BODY_SIZE );
        return -1;
    }
    if ( new_size > result_cap ) {
        fprintf ( stderr,
                  "Error: result buffer overflow (need %zu, have %zu)\n",
                  new_size, result_cap );
        return -1;
    }

    /* Vlastní kopírování. Odlišný layout pro insert / overwrite +
       gap / no-gap kombinace. */
    if ( mode_insert ) {
        if ( gap_size > 0 ) {
            /* INSERT gap: target + filler + input */
            memcpy ( result, target, target_size );
            memset ( result + target_size, filler, gap_size );
            memcpy ( result + target_size + gap_size, input, input_size );
        } else {
            /* INSERT no-gap: target[0..off] + input + target[off..end] */
            memcpy ( result, target, offset );
            memcpy ( result + offset, input, input_size );
            if ( target_size > offset ) {
                memcpy ( result + offset + input_size,
                         target + offset,
                         target_size - offset );
            }
        }
    } else {
        /* OVERWRITE */
        if ( gap_size > 0 ) {
            /* OVERWRITE gap (offset > target_size, allow_extend = true) */
            memcpy ( result, target, target_size );
            memset ( result + target_size, filler, gap_size );
            memcpy ( result + target_size + gap_size, input, input_size );
        } else {
            /* OVERWRITE bez gap. Skopíruj target a přepiš okno [offset..offset+input_size).
               Pokud allow_extend a end_offset > target_size, target se kopíruje jen
               do target_size; zbytek (vyplněný inputem) přesahuje konec. */
            size_t prefix_len = ( offset < target_size ) ? offset : target_size;
            memcpy ( result, target, prefix_len );
            memcpy ( result + offset, input, input_size );
            size_t end_offset = offset + input_size;
            if ( end_offset < target_size ) {
                /* tail z target za přepsaným oknem */
                memcpy ( result + end_offset,
                         target + end_offset,
                         target_size - end_offset );
            }
        }
    }

    *result_size = new_size;
    return 0;
}

/**
 * @brief Naparsuje argumenty z `argv` do struktury `st_MZF_PASTE_OPTIONS`.
 *
 * Při zachycení `--help`, `--version` nebo `--lib-versions` rovnou
 * vypíše příslušný text a vrátí 1 (signál pro `main`, aby ukončil
 * s `EXIT_SUCCESS`). Při chybě vrátí -1. Při úspěšném naparsování
 * pozičních argumentů vrátí 0.
 *
 * Mutex/povinné validace:
 *  - `--insert` a `--overwrite` jsou vzájemně výlučné (hláška
 *    obsahuje `"mutually exclusive"`).
 *  - `--at` je povinné.
 *  - Žádný mode = default `--insert`.
 *  - Pokud není ani `--insert` ani `--overwrite` zadán, použije se insert.
 *  - Vyžadovány jsou právě 2 positional argumenty: `<input> <target>`.
 *
 * @param argc Počet argumentů.
 * @param argv Pole argumentů.
 * @param opts Výstup: naparsované volby.
 * @return 0 OK, 1 = help/version vypsáno (exit success), -1 = chyba.
 */
static int parse_args ( int argc, char *argv[], st_MZF_PASTE_OPTIONS *opts ) {
    /* Defaulty */
    memset ( opts, 0, sizeof ( *opts ) );
    opts->filler = 0x00;

    /* Hodnoty pro long-only options bez krátkého ekvivalentu */
    enum {
        OPT_INSERT = 1000,
        OPT_OVERWRITE,
        OPT_EXTEND,
        OPT_AT,
        OPT_FILLER,
        OPT_FROM_MZF,
        OPT_VERSION,
        OPT_LIB_VERSIONS
    };

    static struct option long_options[] = {
        { "insert",       no_argument,       0, OPT_INSERT },
        { "overwrite",    no_argument,       0, OPT_OVERWRITE },
        { "extend",       no_argument,       0, OPT_EXTEND },
        { "at",           required_argument, 0, OPT_AT },
        { "filler",       required_argument, 0, OPT_FILLER },
        { "from-mzf",     no_argument,       0, OPT_FROM_MZF },
        { "output",       required_argument, 0, 'o' },
        { "help",         no_argument,       0, 'h' },
        { "version",      no_argument,       0, OPT_VERSION },
        { "lib-versions", no_argument,       0, OPT_LIB_VERSIONS },
        { 0, 0, 0, 0 }
    };

    /* opterr=0 -> sami píšeme chybové hlášky pro neznámé volby */
    opterr = 0;
    int c;
    while ( ( c = getopt_long ( argc, argv, "o:h", long_options, NULL ) ) != -1 ) {
        switch ( c ) {
            case OPT_INSERT:
                opts->mode_insert = true;
                break;
            case OPT_OVERWRITE:
                opts->mode_overwrite = true;
                break;
            case OPT_EXTEND:
                opts->allow_extend = true;
                break;
            case OPT_AT:
                if ( argparse_offset ( optarg, &opts->at_value, &opts->at_is_end ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --at value '%s' (expected 0..0xFFFF or 'end')\n",
                              optarg );
                    return -1;
                }
                opts->has_at = true;
                break;
            case OPT_FILLER:
                if ( argparse_byte ( optarg, &opts->filler ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --filler value '%s' (expected 0..0xFF)\n",
                              optarg );
                    return -1;
                }
                break;
            case OPT_FROM_MZF:
                opts->from_mzf = true;
                break;
            case 'o':
                opts->output_path = optarg;
                break;
            case 'h':
                print_usage ( stdout );
                return 1;
            case OPT_VERSION:
                print_version ();
                return 1;
            case OPT_LIB_VERSIONS:
                print_lib_versions ();
                return 1;
            case '?':
            default:
                /* getopt_long s opterr=0 -> sami */
                if ( optopt != 0 ) {
                    fprintf ( stderr, "Error: unknown or invalid option '-%c'\n", optopt );
                } else {
                    fprintf ( stderr, "Error: unknown option '%s'\n",
                              ( optind > 0 && optind <= argc ) ? argv[optind - 1] : "?" );
                }
                return -1;
        }
    }

    /* Mutex --insert × --overwrite */
    if ( opts->mode_insert && opts->mode_overwrite ) {
        fprintf ( stderr, "Error: --insert and --overwrite are mutually exclusive\n" );
        return -1;
    }
    /* Default mode: insert */
    if ( !opts->mode_insert && !opts->mode_overwrite ) {
        opts->mode_insert = true;
    }
    /* --extend bez --overwrite nemá smysl (silent ignore) */
    /* (žádná hard error - plánem to není) */

    /* --at je povinné */
    if ( !opts->has_at ) {
        fprintf ( stderr, "Error: option --at OFFSET is required\n" );
        return -1;
    }

    /* Positional: přesně 2 argumenty (input, target) */
    int npos = argc - optind;
    if ( npos != 2 ) {
        fprintf ( stderr, "Error: exactly 2 positional arguments required: <input> <target>\n" );
        return -1;
    }
    opts->input_path = argv[optind];
    opts->target_path = argv[optind + 1];

    return 0;
}

/**
 * @brief Hlavní vstupní bod nástroje mzf-paste.
 *
 * Pipeline:
 *  1. `setmode(stdout, _O_BINARY)` na Win32 (defenzivně, mzf-paste
 *     primárně nepíše na stdout, ale konzistence s ostatními nástroji).
 *  2. `memory_driver_init()` - povinné před použitím memory handlerů.
 *  3. `parse_args` - validace voleb a positional argumentů.
 *  4. `load_mzf_from_file` - načtení a validace cílového MZF.
 *  5. Načtení input dat (`--from-mzf`: přes `load_mzf_from_file` analog,
 *     použije se jen body; jinak: `read_file_to_buffer_at`).
 *  6. Resolve `--at end` -> aktuální fsize cíle.
 *  7. `apply_paste_operation` nad heap bufferem.
 *  8. Sestavení nové `st_MZF` s upravenou fsize.
 *  9. Zápis: `-o` -> `write_mzf_to_file`, jinak `write_mzf_in_place`.
 * 10. Cleanup všech bufferů a `st_MZF*`.
 *
 * @param argc Počet argumentů.
 * @param argv Pole argumentů.
 * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při jakékoli chybě.
 */
int main ( int argc, char *argv[] ) {
#ifdef _WIN32
    setmode ( fileno ( stdout ), _O_BINARY );
#endif

    memory_driver_init ();

    st_MZF_PASTE_OPTIONS opts;
    int prc = parse_args ( argc, argv, &opts );
    if ( prc == 1 ) return EXIT_SUCCESS; /* --help/--version/--lib-versions */
    if ( prc != 0 ) {
        fprintf ( stderr, "Try 'mzf-paste --help' for more information.\n" );
        return EXIT_FAILURE;
    }

    int exit_code = EXIT_FAILURE;
    st_MZF *target_mzf = NULL;
    st_MZF *input_mzf = NULL;
    uint8_t *input_buf = NULL;
    uint8_t *result_buf = NULL;

    /* 1. Načtení cíle */
    if ( load_mzf_from_file ( opts.target_path, &target_mzf ) != 0 ) {
        goto cleanup;
    }
    size_t target_size = (size_t) target_mzf->header.fsize;
    const uint8_t *target_data = ( target_mzf->body ) ? target_mzf->body : (const uint8_t *) "";

    /* 2. Načtení vstupu */
    const uint8_t *input_data = NULL;
    size_t input_size = 0;
    if ( opts.from_mzf ) {
        if ( load_mzf_from_file ( opts.input_path, &input_mzf ) != 0 ) {
            goto cleanup;
        }
        input_size = (size_t) input_mzf->header.fsize;
        input_data = ( input_mzf->body ) ? input_mzf->body : (const uint8_t *) "";
    } else {
        size_t buf_cap = (size_t) MZF_MAX_BODY_SIZE + 1;
        input_buf = malloc ( buf_cap );
        if ( !input_buf ) {
            fprintf ( stderr, "Error: cannot allocate %zu bytes for input buffer\n", buf_cap );
            goto cleanup;
        }
        if ( read_file_to_buffer_at ( opts.input_path, input_buf, 0, buf_cap, &input_size ) != 0 ) {
            goto cleanup;
        }
        input_data = input_buf;
    }

    /* 3. Resolve --at end -> target_size */
    size_t resolved_offset = opts.at_is_end ? target_size : (size_t) opts.at_value;

    /* 4. Aplikace paste operace */
    size_t result_cap = (size_t) MZF_MAX_BODY_SIZE + 1;
    result_buf = malloc ( result_cap );
    if ( !result_buf ) {
        fprintf ( stderr, "Error: cannot allocate %zu bytes for result buffer\n", result_cap );
        goto cleanup;
    }
    size_t result_size = 0;
    if ( apply_paste_operation ( target_data, target_size,
                                  input_data, input_size,
                                  resolved_offset,
                                  opts.mode_insert,
                                  opts.allow_extend,
                                  opts.filler,
                                  result_buf, &result_size, result_cap ) != 0 ) {
        goto cleanup;
    }

    /* 5. Sestavení nové st_MZF (kopie hlavičky cíle, přepočet fsize) */
    st_MZF new_mzf = {
        .header    = target_mzf->header,
        .body      = result_buf,
        .body_size = (uint32_t) result_size,
    };
    new_mzf.header.fsize = (uint16_t) result_size;

    /* 6. Zápis */
    int wrc;
    if ( opts.output_path ) {
        wrc = write_mzf_to_file ( opts.output_path, &new_mzf );
    } else {
        wrc = write_mzf_in_place ( opts.target_path, &new_mzf );
    }
    if ( wrc != 0 ) {
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if ( result_buf ) free ( result_buf );
    if ( input_buf ) free ( input_buf );
    if ( input_mzf ) mzf_free ( input_mzf );
    if ( target_mzf ) mzf_free ( target_mzf );
    return exit_code;
}
