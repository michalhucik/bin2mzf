/**
 * @file   mzf_info.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  CLI nástroj rodiny bin2mzf - read-only inspekce existujícího MZF.
 *
 * Načte MZF soubor (z file argumentu nebo stdin) a vypíše čitelný popis
 * jeho hlavičky (typ, jméno, komentář, adresy, velikost) a volitelně
 * hexdump těla. Slouží pro debug a verifikaci výstupu nástroje `bin2mzf`
 * nebo jiných producentů MZF formátu.
 *
 * Verze 0.2.0 scope:
 *  - file vstup (poziční argument) i stdin (`-`).
 *  - `--charset eu|jp|utf8-eu|utf8-jp|none` (default `eu`) - dekódování
 *    pole `fname` a `cmnt` do UTF-8 textu.
 *  - `--header-only` - vypíše jen text hlavičky bez hexdump těla.
 *  - `--body-only` - vypíše jen hexdump těla, bez popisu hlavičky
 *    (alias k `--hexdump --offset 0 --length fsize`, ale bez header textu).
 *  - `--hexdump [--offset N] [--length N]` - hexdump rozsahu těla.
 *  - `--validate` - tichý režim, jen návratový kód (0 = OK, 1 = chyba).
 *  - symbolické dekódování CP/M konvence SOKODI CMT.COM (`ftype == 0x22`),
 *    8.3 layout pole `fname.name`, atributové bity R/O/SYS/ARC z bitu 7
 *    přípony, toggle `--no-cpm-decode`.
 *  - `--format text|json|csv` (default `text`) - výstupní formát hlavičky
 *    přes knihovnu `output_format`. JSON/CSV jsou nekompatibilní
 *    s `--hexdump` a `--body-only` (text-only debug aid).
 *  - `--version`, `--lib-versions`, `--help`.
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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "libs/mzf/mzf.h"
#include "libs/mzf/mzf_tools.h"
#include "libs/generic_driver/generic_driver.h"
#include "libs/generic_driver/memory_driver.h"
#include "libs/sharpmz_ascii/sharpmz_ascii.h"
#include "libs/endianity/endianity.h"
#include "libs/output_format/output_format.h"
#include "tools/common/bin2mzf_cli_version.h"
#include "tools/common/argparse_helpers.h"
#include "tools/common/cpm_constants.h"
#include "tools/common/cpm_helpers.h"
#include "tools/common/text_field.h"

/** @brief Verze nástroje mzf-info. */
#define MZF_INFO_VERSION "0.2.0"

/**
 * @brief Kapacita vstupního bufferu pro načtení MZF.
 *
 * Hodnota = `MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE + 1` (= 65664). Extra
 * bajt slouží k detekci přetečení - pokud načtení vrátí přesně tuto
 * hodnotu, vstup byl větší než maximální MZF a hlásíme chybu.
 */
#define MZF_INFO_INPUT_BUF_CAP ( (size_t) MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE + 1 )

/**
 * @brief Volby CLI parsované z argumentů `mzf-info`.
 *
 * Drží referenční ukazatele do `argv` (žádná vlastní alokace), životnost
 * vázaná na životnost procesu. Booleovské příznaky `header_only`,
 * `body_only`, `hexdump`, `validate` jsou navzájem částečně výlučné -
 * validace probíhá v `parse_args` (mutual exclusion).
 *
 * Pole `has_offset`/`has_length` rozlišují, zda uživatel hodnotu zadal
 * explicitně (jinak se použije default - 0 pro offset, body_size pro length).
 */
typedef struct st_MZF_INFO_OPTIONS {
    en_TOOL_CHARSET charset;    /**< znaková sada pro dekódování fname/cmnt (default EU) */
    bool header_only;           /**< true = vypsat jen text hlavičky */
    bool body_only;             /**< true = vypsat jen hexdump těla (bez header textu) */
    bool hexdump;               /**< true = připojit hexdump těla za header text */
    bool validate;              /**< true = tichý režim, jen návratový kód */
    uint16_t hexdump_offset;    /**< offset počátku hexdumpu v těle (validní jen při has_offset) */
    uint16_t hexdump_length;    /**< počet bajtů k zobrazení (validní jen při has_length) */
    bool has_offset;            /**< true pokud uživatel zadal --offset */
    bool has_length;            /**< true pokud uživatel zadal --length */
    const char *input_path;     /**< vstupní soubor; NULL = stdin */
    bool use_stdin;             /**< true pokud nebyla pozice argumentu - čte se stdin */
    bool cpm_decode;            /**< true = u ftype 0x22 vypsat CP/M (SOKODI CMT.COM) sekci (default true) */
    en_OUTFMT format;           /**< výstupní formát (text/json/csv), default text */
} st_MZF_INFO_OPTIONS;

/**
 * @brief Vypíše Usage / nápovědu nástroje mzf-info.
 *
 * Text je v angličtině (locales-ready). Volá se z `--help` (na stdout)
 * a z chybových cest (na stderr).
 *
 * @param out Cílový stream (stdout / stderr).
 */
static void print_usage ( FILE *out ) {
    fprintf ( out,
        "Usage: mzf-info [options] [INPUT.mzf]\n"
        "\n"
        "Inspect an MZF file (Sharp MZ tape format). Read-only operation.\n"
        "If INPUT is omitted, read MZF from stdin.\n"
        "\n"
        "Options:\n"
        "  -C, --charset MODE         Charset for fname/comment decoding:\n"
        "                             eu (default), jp, utf8-eu, utf8-jp, none.\n"
        "      --header-only          Print header text only (no body hexdump).\n"
        "      --body-only            Print body hexdump only (no header text).\n"
        "      --hexdump              Print body hexdump after header text.\n"
        "      --offset N             Hexdump start offset within body (default 0).\n"
        "      --length N             Hexdump length in bytes (default: full body).\n"
        "      --validate             Quiet mode; exit 0 if MZF is valid, 1 otherwise.\n"
        "      --no-cpm-decode        Disable CP/M (SOKODI CMT.COM) name decoding\n"
        "                             when ftype == 0x22.\n"
        "      --format MODE          Output format: text (default), json, csv.\n"
        "                             json/csv are incompatible with --hexdump\n"
        "                             and --body-only.\n"
        "      --version              Print tool version and exit.\n"
        "      --lib-versions         Print library versions and exit.\n"
        "  -h, --help                 Print this help and exit.\n"
        "\n"
        "Numeric values accept decimal or 0x-prefixed hexadecimal notation.\n"
        "\n"
        "--header-only, --body-only and --validate are mutually exclusive.\n"
        "--validate cannot be combined with --hexdump.\n"
    );
}

/**
 * @brief Vypíše verzi nástroje a verzi celé rodiny CLI.
 *
 * Formát: `mzf-info <tool> (<release-name> <release-version>)`.
 */
static void print_version ( void ) {
    printf ( "mzf-info %s (%s %s)\n",
             MZF_INFO_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
}

/**
 * @brief Vypíše verze všech linkovaných knihoven.
 *
 * Slouží k diagnostice ABI mismatch a pro reportování v bug reports.
 */
static void print_lib_versions ( void ) {
    printf ( "mzf-info %s (%s %s)\n\n",
             MZF_INFO_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
    printf ( "Library versions:\n" );
    printf ( "  mzf               %s\n", mzf_version () );
    printf ( "  generic_driver    %s\n", generic_driver_version () );
    printf ( "  endianity         %s\n", endianity_version () );
    printf ( "  sharpmz_ascii     %s\n", sharpmz_ascii_version () );
    printf ( "  output_format     %s\n", output_format_version () );
}

/**
 * @brief Naparsuje argumenty z `argv` do struktury `st_MZF_INFO_OPTIONS`.
 *
 * Podporuje krátké volby `-h`, `-V`, `-C` a sadu dlouhých voleb
 * (`--header-only`, `--body-only`, `--hexdump`, `--offset`, `--length`,
 * `--validate`, `--no-cpm-decode`, `--format`, `--lib-versions`). Při
 * zachycení `--help`, `--version` nebo `--lib-versions` rovnou vypíše
 * příslušný text a vrátí 1.
 *
 * Cross-validace (mutual exclusion) probíhá až po hlavní getopt smyčce -
 * uživatelův záměr je jasný až podle finální kombinace všech voleb.
 *
 * @param argc Standardní argc.
 * @param argv Standardní argv.
 * @param opts Výstupní struktura voleb (musí být předem inicializovaná).
 * @return 0 = pokračovat, 1 = early exit success, -1 = error.
 */
static int parse_args ( int argc, char *argv[], st_MZF_INFO_OPTIONS *opts ) {
    static const struct option long_options[] = {
        { "charset",       required_argument, NULL, 'C' },
        { "header-only",   no_argument,       NULL, 1001 },
        { "body-only",     no_argument,       NULL, 1002 },
        { "hexdump",       no_argument,       NULL, 1003 },
        { "offset",        required_argument, NULL, 1004 },
        { "length",        required_argument, NULL, 1005 },
        { "validate",      no_argument,       NULL, 1006 },
        { "no-cpm-decode", no_argument,       NULL, 1007 },
        { "format",        required_argument, NULL, 1008 },
        { "version",       no_argument,       NULL, 'V' },
        { "lib-versions",  no_argument,       NULL, 1000 },
        { "help",          no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* vlastní chybové hlášky, getopt ať mlčí */
    opterr = 0;

    int c;
    while ( ( c = getopt_long ( argc, argv, "+hVC:", long_options, NULL ) ) != -1 ) {
        switch ( c ) {
            case 'C':
                if ( argparse_charset ( optarg, &opts->charset ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --charset value '%s' (expected: eu, jp, utf8-eu, utf8-jp, none)\n",
                              optarg );
                    return -1;
                }
                break;
            case 1001:
                opts->header_only = true;
                break;
            case 1002:
                opts->body_only = true;
                break;
            case 1003:
                opts->hexdump = true;
                break;
            case 1004:
                if ( argparse_uint16 ( optarg, &opts->hexdump_offset ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --offset value '%s'\n", optarg );
                    return -1;
                }
                opts->has_offset = true;
                break;
            case 1005:
                if ( argparse_uint16 ( optarg, &opts->hexdump_length ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --length value '%s'\n", optarg );
                    return -1;
                }
                opts->has_length = true;
                break;
            case 1006:
                opts->validate = true;
                break;
            case 1007:
                opts->cpm_decode = false;
                break;
            case 1008:
                if ( outfmt_parse ( optarg, &opts->format ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --format value '%s' (expected: text, json, csv)\n",
                              optarg );
                    return -1;
                }
                break;
            case 'V':
                print_version ();
                return 1;
            case 1000:
                print_lib_versions ();
                return 1;
            case 'h':
                print_usage ( stdout );
                return 1;
            case '?':
            default:
                if ( optopt ) {
                    fprintf ( stderr, "Error: unknown or malformed option '-%c'\n", optopt );
                } else {
                    fprintf ( stderr, "Error: unknown option '%s'\n",
                              argv[optind - 1] ? argv[optind - 1] : "?" );
                }
                return -1;
        }
    }

    /* Positional argument - vstupní soubor */
    if ( optind < argc ) {
        opts->input_path = argv[optind];
        opts->use_stdin = false;
        optind++;
        if ( optind < argc ) {
            fprintf ( stderr, "Error: too many positional arguments\n" );
            return -1;
        }
    } else {
        opts->use_stdin = true;
    }

    /* Mutual exclusion: header-only vs body-only vs validate vs hexdump. */
    if ( opts->header_only && opts->body_only ) {
        fprintf ( stderr, "Error: --header-only and --body-only are mutually exclusive\n" );
        return -1;
    }
    if ( opts->validate && ( opts->header_only || opts->body_only || opts->hexdump ) ) {
        fprintf ( stderr,
                  "Error: --validate cannot be combined with --header-only, --body-only or --hexdump\n" );
        return -1;
    }

    /* Format-vs-hexdump mutex: hexdump je text-only debug aid (od-style),
       v JSON/CSV nemá smysl. Hard error má deterministický exit pro skripty. */
    if ( opts->format != OUTFMT_TEXT ) {
        if ( opts->hexdump ) {
            fprintf ( stderr,
                      "Error: --hexdump is incompatible with --format json/csv (text-only)\n" );
            return -1;
        }
        if ( opts->body_only ) {
            fprintf ( stderr,
                      "Error: --body-only is incompatible with --format json/csv (text-only)\n" );
            return -1;
        }
    }

    /* Defenzivní warning: --offset / --length bez --hexdump ani --body-only. */
    if ( ( opts->has_offset || opts->has_length ) && !opts->hexdump && !opts->body_only ) {
        fprintf ( stderr,
                  "Warning: --offset / --length have no effect without --hexdump or --body-only\n" );
    }

    return 0;
}

/**
 * @brief Načte celý stdin do předem alokovaného bufferu.
 *
 * Funguje na principu chunked `fread` smyčky. Pokud stdin obsahuje více
 * než `buf_cap` bajtů, vrátí chybu (po pokusu o přečtení dalšího bajtu).
 *
 * @param buf       Cílový buffer.
 * @param buf_cap   Kapacita bufferu v bajtech.
 * @param out_size  Výstup: počet skutečně načtených bajtů.
 * @return 0 při úspěchu, -1 při I/O chybě nebo přetečení.
 */
static int read_stdin_to_buffer ( uint8_t *buf, size_t buf_cap, size_t *out_size ) {
    size_t total = 0;
    while ( total < buf_cap ) {
        size_t n = fread ( buf + total, 1, buf_cap - total, stdin );
        if ( n == 0 ) break;
        total += n;
    }
    if ( ferror ( stdin ) ) {
        fprintf ( stderr, "Error: read error on stdin\n" );
        return -1;
    }
    *out_size = total;
    return 0;
}

/**
 * @brief Načte celý obsah souboru do předem alokovaného bufferu.
 *
 * Pokud soubor obsahuje více bajtů než `buf_cap`, funkce volajícímu
 * vrátí počet rovný `buf_cap` (volající má detekovat přetečení tím,
 * že použije buffer s kapacitou `MZF expected max + 1`).
 *
 * @param path     Cesta k souboru.
 * @param buf      Cílový buffer.
 * @param buf_cap  Kapacita bufferu v bajtech.
 * @param out_size Výstup: počet skutečně načtených bajtů.
 * @return 0 při úspěchu, -1 při I/O chybě.
 */
static int load_file_to_buffer ( const char *path, uint8_t *buf, size_t buf_cap,
                                 size_t *out_size ) {
    FILE *fp = fopen ( path, "rb" );
    if ( !fp ) {
        fprintf ( stderr, "Error: cannot open input file '%s': %s\n", path, strerror ( errno ) );
        return -1;
    }
    size_t total = 0;
    while ( total < buf_cap ) {
        size_t n = fread ( buf + total, 1, buf_cap - total, fp );
        if ( n == 0 ) break;
        total += n;
    }
    int err = ferror ( fp );
    fclose ( fp );
    if ( err ) {
        fprintf ( stderr, "Error: read error on '%s'\n", path );
        return -1;
    }
    *out_size = total;
    return 0;
}

/**
 * @brief Vrátí symbolický řetězec pro hodnotu `ftype`.
 *
 * Pokrývá pět standardních typů (OBJ/BTX/BSD/BRD/RB). Pro neznámé
 * hodnoty vrací konstantní řetězec `"unknown"`. Vrácený řetězec je
 * statický, nikdy NULL.
 *
 * @param ftype Hodnota pole `ftype` z MZF hlavičky (0x00..0xFF).
 * @return Symbolický název typu (statický řetězec).
 */
static const char *ftype_to_symbol ( uint8_t ftype ) {
    switch ( ftype ) {
        case MZF_FTYPE_OBJ: return "OBJ";
        case MZF_FTYPE_BTX: return "BTX";
        case MZF_FTYPE_BSD: return "BSD";
        case MZF_FTYPE_BRD: return "BRD";
        case MZF_FTYPE_RB:  return "RB";
        case MZF_CPM_FTYPE: return "CPM";
        default: return "unknown";
    }
}

/**
 * @brief Spočítá 8-bitový součet (sum8) přes blok dat.
 *
 * Vrací modulo-256 součet všech bajtů. Použití: orientační kontrolní
 * součet pro debug porovnání těla MZF.
 *
 * @param data Vstupní blok dat (NULL je povolený, vrací 0).
 * @param size Délka bloku v bajtech.
 * @return Sum8 hodnota (0..255).
 */
static uint8_t compute_sum8 ( const uint8_t *data, size_t size ) {
    uint8_t s = 0;
    if ( !data ) return 0;
    for ( size_t i = 0; i < size; i++ ) {
        s = (uint8_t) ( s + data[i] );
    }
    return s;
}

/**
 * @brief Spočítá XOR fold přes blok dat.
 *
 * Vrací XOR všech bajtů. Pro debug porovnání těla MZF (XOR je citlivější
 * na permutace než sum8).
 *
 * @param data Vstupní blok dat (NULL je povolený, vrací 0).
 * @param size Délka bloku v bajtech.
 * @return XOR hodnota (0..255).
 */
static uint8_t compute_xor ( const uint8_t *data, size_t size ) {
    uint8_t x = 0;
    if ( !data ) return 0;
    for ( size_t i = 0; i < size; i++ ) {
        x ^= data[i];
    }
    return x;
}

/**
 * @brief Vypíše hexdump zadaného rozsahu těla v `od`-style formátu.
 *
 * Formát řádku:
 *  - 8 hex znaků offsetu (relativně k začátku těla, ne k začátku
 *    zobrazovaného rozsahu)
 *  - dvě skupiny po 8 hex bajtech (`%02x ` s extra mezerou mezi 8. a
 *    9. bajtem)
 *  - ASCII gutter `|...|`, znaky < 0x20 nebo > 0x7e zobrazené jako `.`
 *
 * Pokud `offset > total_size` nebo `offset + length > total_size`,
 * funkce vypíše chybu na stderr a vrátí -1 (volající skončí EXIT_FAILURE).
 *
 * @param data       Ukazatel na začátek těla MZF (může být NULL pokud size==0).
 * @param total_size Celková velikost těla v bajtech.
 * @param offset     Offset začátku zobrazovaného rozsahu.
 * @param length     Počet bajtů k zobrazení.
 * @return 0 při úspěchu, -1 při překročení rozsahu.
 */
static int hexdump_range ( const uint8_t *data, size_t total_size,
                           size_t offset, size_t length ) {
    if ( offset > total_size ) {
        fprintf ( stderr, "Error: --offset 0x%zx out of body range (body size %zu)\n",
                  offset, total_size );
        return -1;
    }
    if ( offset + length > total_size ) {
        fprintf ( stderr,
                  "Error: --offset/--length out of body range (offset+length %zu > body size %zu)\n",
                  offset + length, total_size );
        return -1;
    }

    size_t pos = offset;
    size_t end = offset + length;
    while ( pos < end ) {
        size_t line_end = pos + 16;
        if ( line_end > end ) line_end = end;

        printf ( "%08zx ", pos );
        /* hex bajty - 16 slotů, mezera mezi 8. a 9. */
        for ( size_t i = 0; i < 16; i++ ) {
            size_t cur = pos + i;
            if ( i == 8 ) printf ( " " );
            if ( cur < line_end ) {
                printf ( " %02x", data[cur] );
            } else {
                printf ( "   " );
            }
        }
        /* ASCII gutter */
        printf ( "  |" );
        for ( size_t cur = pos; cur < line_end; cur++ ) {
            uint8_t ch = data[cur];
            putchar ( ( ch >= 0x20 && ch <= 0x7e ) ? (int) ch : '.' );
        }
        printf ( "|\n" );

        pos = line_end;
    }
    return 0;
}

/**
 * @brief Vypíše tří-řádkovou CP/M sekci (jméno, přípona, atributy).
 *
 * Formát výstupu (sloupce zarovnané jako stávající `Name:` řádek):
 * ```
 * CP/M name:     "HELLO"
 * CP/M ext:      "COM"
 * CP/M attrs:    R/O,SYS,ARC
 * ```
 *
 * Pravidla:
 *  - Jméno a přípona v dvojitých uvozovkách, již oříznuté trailing
 *    mezerami (volající přes `mzf_cpm_decode_header`).
 *  - Atributy jako comma-separated `R/O,SYS,ARC` (jen aktivní). Pokud
 *    `attr_mask == 0`, vypsat `(none)` - vždy 3 řádky pro snazší grep
 *    v testech.
 *
 * @param name      Bajty CP/M jména (ASCII, bez paddingu).
 * @param name_len  Efektivní délka `name` (0..8).
 * @param ext       Bajty CP/M přípony (ASCII, bez paddingu, bit 7 vymazán).
 * @param ext_len   Efektivní délka `ext` (0..3).
 * @param attr_mask Bitmaska atributů (`MZF_CPM_ATTR_RO|_SYS|_ARC`).
 */
static void print_cpm_section ( const uint8_t *name, size_t name_len,
                                const uint8_t *ext,  size_t ext_len,
                                uint8_t attr_mask ) {
    printf ( "CP/M name:     \"%.*s\"\n", (int) name_len, (const char *) name );
    printf ( "CP/M ext:      \"%.*s\"\n", (int) ext_len,  (const char *) ext );

    printf ( "CP/M attrs:    " );
    if ( attr_mask == 0 ) {
        printf ( "(none)" );
    } else {
        bool first = true;
        if ( attr_mask & MZF_CPM_ATTR_RO ) {
            printf ( "R/O" );
            first = false;
        }
        if ( attr_mask & MZF_CPM_ATTR_SYS ) {
            printf ( "%sSYS", first ? "" : "," );
            first = false;
        }
        if ( attr_mask & MZF_CPM_ATTR_ARC ) {
            printf ( "%sARC", first ? "" : "," );
            first = false;
        }
    }
    printf ( "\n" );
}

/**
 * @brief Vypíše čitelný textový popis MZF hlavičky.
 *
 * Výstup obsahuje typ souboru (číselně + symbolicky), jméno (UTF-8 podle
 * volby `charset` + raw hex bajty), velikost (hex + decimal), startovací
 * a spouštěcí adresu, komentář (UTF-8 + raw hex prvních 16 bajtů),
 * checksumy těla (sum8, XOR) a sekci validace.
 *
 * Validace pokrývá:
 *  - terminátor 0x0D na pozici fname[16] (přes `mzf_header_validate`)
 *  - shoda velikosti (`128 + fsize` vs `total_input_size`):
 *    - rovno -> `OK`
 *    - vstup je delší -> warning `trailing bytes`
 *    - vstup je kratší -> chyba `truncated`
 *
 * @param mzf              Načtená MZF struktura (nesmí být NULL).
 * @param total_input_size Celková velikost načteného vstupu (header + body).
 * @param charset          Volba znakové sady pro dekódování textu.
 * @param cpm_decode       true = u `ftype == 0x22` se SOKODI 8.3 layoutem
 *                         vypsat tří-řádkovou CP/M sekci pod raw bytes
 *                         fname; false = sekci přeskočit (toggle
 *                         `--no-cpm-decode`).
 */
static void print_header_text ( const st_MZF *mzf, size_t total_input_size,
                                en_TOOL_CHARSET charset, bool cpm_decode ) {
    const st_MZF_HEADER *hdr = &mzf->header;

    /* Typ souboru */
    printf ( "File type:     0x%02x (%s)\n", hdr->ftype, ftype_to_symbol ( hdr->ftype ) );

    /* Jméno - dekódování dle charset, mimo NONE */
    char fname_utf8[MZF_FNAME_UTF8_BUF_SIZE];
    fname_utf8[0] = '\0';
    bool show_utf8_fname = true;
    switch ( charset ) {
        case TOOL_CHARSET_EU:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_ASCII_EU );
            break;
        case TOOL_CHARSET_UTF8_EU:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_UTF8_EU );
            break;
        case TOOL_CHARSET_JP:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_ASCII_JP );
            break;
        case TOOL_CHARSET_UTF8_JP:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_UTF8_JP );
            break;
        case TOOL_CHARSET_NONE:
        default:
            show_utf8_fname = false;
            break;
    }

    if ( show_utf8_fname ) {
        printf ( "Name:          \"%s\"\n", fname_utf8 );
    } else {
        printf ( "Name:          (raw bytes only)\n" );
    }
    printf ( "  raw bytes:  " );
    for ( size_t i = 0; i < MZF_FILE_NAME_LENGTH; i++ ) {
        printf ( " %02x", hdr->fname.name[i] );
    }
    printf ( "\n" );

    /* CP/M (SOKODI CMT.COM) sekce - vypisuje se jen pokud je dekódování
       povoleno (`--no-cpm-decode` ji vypne) a hlavička odpovídá 8.3
       layoutu. Sekce stojí paralelně k `Name:` a raw bytes řádku, neni
       jejich náhradou. */
    if ( cpm_decode ) {
        uint8_t cpm_name[MZF_CPM_NAME_LEN];
        uint8_t cpm_ext[MZF_CPM_EXT_LEN];
        size_t cpm_name_len = 0;
        size_t cpm_ext_len = 0;
        uint8_t cpm_attr = 0;
        if ( mzf_cpm_decode_header ( hdr, cpm_name, &cpm_name_len,
                                 cpm_ext, &cpm_ext_len, &cpm_attr ) ) {
            print_cpm_section ( cpm_name, cpm_name_len,
                                cpm_ext, cpm_ext_len, cpm_attr );
        }
    }

    /* Velikost a adresy */
    printf ( "fsize:         0x%04x (%u bytes)\n", hdr->fsize, hdr->fsize );
    printf ( "fstrt (load):  0x%04x\n", hdr->fstrt );
    printf ( "fexec (exec):  0x%04x\n", hdr->fexec );

    /* Komentář - UTF-8 (mimo NONE) + raw prvních 16 bajtů */
    if ( show_utf8_fname ) {
        char cmnt_utf8[MZF_CMNT_LENGTH * 4 + 1];
        sharpmz_charset_t sharp_cs =
            ( charset == TOOL_CHARSET_JP || charset == TOOL_CHARSET_UTF8_JP )
                ? SHARPMZ_CHARSET_JP
                : SHARPMZ_CHARSET_EU;
        sharpmz_str_to_utf8 ( hdr->cmnt, MZF_CMNT_LENGTH,
                              cmnt_utf8, sizeof ( cmnt_utf8 ), sharp_cs );
        printf ( "Comment:       \"%s\"\n", cmnt_utf8 );
    } else {
        printf ( "Comment:       (raw bytes only)\n" );
    }
    printf ( "  raw (16):   " );
    for ( size_t i = 0; i < 16; i++ ) {
        printf ( " %02x", hdr->cmnt[i] );
    }
    printf ( "\n" );

    /* Checksumy těla */
    uint8_t s8  = compute_sum8 ( mzf->body, mzf->body_size );
    uint8_t xr  = compute_xor  ( mzf->body, mzf->body_size );
    printf ( "Body checksum: sum8=0x%02x  xor=0x%02x\n", s8, xr );

    /* Validace */
    printf ( "Validation:\n" );
    en_MZF_ERROR herr = mzf_header_validate ( hdr );
    if ( herr == MZF_OK ) {
        printf ( "  Header terminator (0x0d):  OK\n" );
    } else {
        printf ( "  Header terminator (0x0d):  MISSING\n" );
    }

    /* Lokální size check (mzf_file_validate neřeší trailing bytes). */
    size_t expected = (size_t) MZF_HEADER_SIZE + (size_t) hdr->fsize;
    if ( total_input_size == expected ) {
        printf ( "  File size match:           OK (128 + fsize == filesize)\n" );
    } else if ( total_input_size > expected ) {
        printf ( "  File size match:           WARNING (trailing bytes: filesize %zu > 128 + fsize %zu)\n",
                 total_input_size, expected );
    } else {
        printf ( "  File size match:           ERROR (truncated: filesize %zu < 128 + fsize %zu)\n",
                 total_input_size, expected );
    }
}

/**
 * @brief Vypíše hlavičku MZF ve strukturovaném formátu (JSON/CSV).
 *
 * Idiomatický pattern dle vzoru `mzdsk-info` (z mzdisku): používá
 * `outfmt_init` + `outfmt_doc_begin` + řadu `outfmt_kv_*` na kořenové
 * úrovni + `outfmt_doc_end`. Knihovna `output_format` zajistí JSON
 * escapování i lazy CSV hlavičku `key,value` (kv mód, ne tabulkový).
 *
 * Klíče (snake_case, stabilní pro v0.2.0) v pořadí výstupu:
 *  - `ftype` (uint), `ftype_hex` (string "0xHH"), `ftype_symbol` (string)
 *  - `name` (UTF-8 dekódované jméno; prázdný řetězec při charset=NONE)
 *  - **CP/M sekce** (jen u ftype 0x22 + SOKODI layout + cpm_decode):
 *    `cpm_name`, `cpm_ext`, `cpm_attr_ro`, `cpm_attr_sys`, `cpm_attr_arc`
 *    (booly jako string "true"/"false" - root-level kv API neumí bool),
 *    `cpm_attrs` (agregovaný string "R/O,SYS,ARC" nebo "")
 *  - `fsize` (uint), `fstrt` (string "0xHHHH"), `fexec` (string "0xHHHH")
 *  - `comment` (UTF-8 dekódovaný 104B komentář; prázdný při NONE)
 *  - `body_sum8` (uint 0..255), `body_xor` (uint 0..255)
 *  - `header_terminator_ok` (string true/false), `size_match`
 *    (string "ok"/"trailing_bytes"/"truncated"), `size_expected` (uint),
 *    `size_actual` (uint), `valid` (string true/false - agregát)
 *
 * @param mzf              Načtený MZF (nesmí být NULL).
 * @param total_input_size Celková velikost vstupu (header + body).
 * @param charset          Charset pro dekódování fname/cmnt.
 * @param cpm_decode       true = u ftype=0x22 SOKODI vypsat CP/M sekci.
 * @param format           OUTFMT_JSON nebo OUTFMT_CSV (TEXT není volaný).
 *
 * @pre format != OUTFMT_TEXT
 */
static void print_header_structured ( const st_MZF *mzf,
                                      size_t total_input_size,
                                      en_TOOL_CHARSET charset,
                                      bool cpm_decode,
                                      en_OUTFMT format ) {
    const st_MZF_HEADER *hdr = &mzf->header;

    st_OUTFMT_CTX ctx;
    outfmt_init ( &ctx, format );
    outfmt_doc_begin ( &ctx );

    /* Typ souboru: dec uint + redundantní hex string + symbol. */
    outfmt_kv_uint ( &ctx, "ftype", (unsigned long) hdr->ftype );
    char ftype_hex[8];
    snprintf ( ftype_hex, sizeof ( ftype_hex ), "0x%02x", hdr->ftype );
    outfmt_kv_str ( &ctx, "ftype_hex", ftype_hex );
    outfmt_kv_str ( &ctx, "ftype_symbol", ftype_to_symbol ( hdr->ftype ) );

    /* Jméno - UTF-8 dekódované dle charset, nebo prázdné při NONE. */
    char fname_utf8[MZF_FNAME_UTF8_BUF_SIZE];
    fname_utf8[0] = '\0';
    bool show_utf8 = true;
    switch ( charset ) {
        case TOOL_CHARSET_EU:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_ASCII_EU );
            break;
        case TOOL_CHARSET_UTF8_EU:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_UTF8_EU );
            break;
        case TOOL_CHARSET_JP:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_ASCII_JP );
            break;
        case TOOL_CHARSET_UTF8_JP:
            mzf_tools_get_fname_ex ( hdr, fname_utf8, sizeof ( fname_utf8 ),
                                     MZF_NAME_UTF8_JP );
            break;
        case TOOL_CHARSET_NONE:
        default:
            show_utf8 = false;
            break;
    }
    outfmt_kv_str ( &ctx, "name", show_utf8 ? fname_utf8 : "" );

    /* CP/M sekce (paralela k textovému `print_cpm_section`) - jen pokud
       je decode povolený a hlavička odpovídá SOKODI 8.3 layoutu. */
    if ( cpm_decode ) {
        uint8_t cpm_name[MZF_CPM_NAME_LEN];
        uint8_t cpm_ext[MZF_CPM_EXT_LEN];
        size_t cpm_name_len = 0;
        size_t cpm_ext_len = 0;
        uint8_t cpm_attr = 0;
        if ( mzf_cpm_decode_header ( hdr, cpm_name, &cpm_name_len,
                                     cpm_ext, &cpm_ext_len, &cpm_attr ) ) {
            char cpm_name_buf[MZF_CPM_NAME_LEN + 1];
            char cpm_ext_buf[MZF_CPM_EXT_LEN + 1];
            memcpy ( cpm_name_buf, cpm_name, cpm_name_len );
            cpm_name_buf[cpm_name_len] = '\0';
            memcpy ( cpm_ext_buf, cpm_ext, cpm_ext_len );
            cpm_ext_buf[cpm_ext_len] = '\0';

            outfmt_kv_str ( &ctx, "cpm_name", cpm_name_buf );
            outfmt_kv_str ( &ctx, "cpm_ext",  cpm_ext_buf );

            /* Booleany na kořeni: API nemá outfmt_kv_bool, použijeme
               outfmt_kv_str s "true"/"false" - JSON konzument je rozpozná
               jako string, ale je to dohodnuté pro v0.2.0 kontrakt. */
            outfmt_kv_str ( &ctx, "cpm_attr_ro",
                            ( cpm_attr & MZF_CPM_ATTR_RO ) ? "true" : "false" );
            outfmt_kv_str ( &ctx, "cpm_attr_sys",
                            ( cpm_attr & MZF_CPM_ATTR_SYS ) ? "true" : "false" );
            outfmt_kv_str ( &ctx, "cpm_attr_arc",
                            ( cpm_attr & MZF_CPM_ATTR_ARC ) ? "true" : "false" );

            /* Agregovaný comma-separated string atributů. */
            char cpm_attrs_buf[16];
            cpm_attrs_buf[0] = '\0';
            bool first = true;
            if ( cpm_attr & MZF_CPM_ATTR_RO ) {
                strcat ( cpm_attrs_buf, "R/O" );
                first = false;
            }
            if ( cpm_attr & MZF_CPM_ATTR_SYS ) {
                if ( !first ) strcat ( cpm_attrs_buf, "," );
                strcat ( cpm_attrs_buf, "SYS" );
                first = false;
            }
            if ( cpm_attr & MZF_CPM_ATTR_ARC ) {
                if ( !first ) strcat ( cpm_attrs_buf, "," );
                strcat ( cpm_attrs_buf, "ARC" );
                first = false;
            }
            outfmt_kv_str ( &ctx, "cpm_attrs", cpm_attrs_buf );
        }
    }

    /* Velikost a adresy. fsize jako uint (numerická hodnota), adresy
       jako hex string s prefixem 0x (čitelnější pro Z80 paměťové lokace). */
    outfmt_kv_uint ( &ctx, "fsize", (unsigned long) hdr->fsize );
    char addr_hex[8];
    snprintf ( addr_hex, sizeof ( addr_hex ), "0x%04x", hdr->fstrt );
    outfmt_kv_str ( &ctx, "fstrt", addr_hex );
    snprintf ( addr_hex, sizeof ( addr_hex ), "0x%04x", hdr->fexec );
    outfmt_kv_str ( &ctx, "fexec", addr_hex );

    /* Komentář - plný 104B blok dekódovaný do UTF-8, nebo prázdný při NONE. */
    if ( show_utf8 ) {
        char cmnt_utf8[MZF_CMNT_LENGTH * 4 + 1];
        sharpmz_charset_t sharp_cs =
            ( charset == TOOL_CHARSET_JP || charset == TOOL_CHARSET_UTF8_JP )
                ? SHARPMZ_CHARSET_JP
                : SHARPMZ_CHARSET_EU;
        sharpmz_str_to_utf8 ( hdr->cmnt, MZF_CMNT_LENGTH,
                              cmnt_utf8, sizeof ( cmnt_utf8 ), sharp_cs );
        outfmt_kv_str ( &ctx, "comment", cmnt_utf8 );
    } else {
        outfmt_kv_str ( &ctx, "comment", "" );
    }

    /* Checksumy těla. */
    outfmt_kv_uint ( &ctx, "body_sum8",
                     (unsigned long) compute_sum8 ( mzf->body, mzf->body_size ) );
    outfmt_kv_uint ( &ctx, "body_xor",
                     (unsigned long) compute_xor  ( mzf->body, mzf->body_size ) );

    /* Validace - flat klíče. */
    en_MZF_ERROR herr = mzf_header_validate ( hdr );
    bool term_ok = ( herr == MZF_OK );
    outfmt_kv_str ( &ctx, "header_terminator_ok", term_ok ? "true" : "false" );

    size_t expected = (size_t) MZF_HEADER_SIZE + (size_t) hdr->fsize;
    const char *size_match;
    if ( total_input_size == expected ) {
        size_match = "ok";
    } else if ( total_input_size > expected ) {
        size_match = "trailing_bytes";
    } else {
        size_match = "truncated";
    }
    outfmt_kv_str  ( &ctx, "size_match", size_match );
    outfmt_kv_uint ( &ctx, "size_expected", (unsigned long) expected );
    outfmt_kv_uint ( &ctx, "size_actual",   (unsigned long) total_input_size );

    bool valid = term_ok && ( total_input_size == expected );
    outfmt_kv_str ( &ctx, "valid", valid ? "true" : "false" );

    outfmt_doc_end ( &ctx );
}

/**
 * @brief Spustí kompletní validaci MZF a vrátí návratový kód pro `--validate`.
 *
 * Tichý režim: žádný stdout výstup. Při neúspěchu vypíše jednu řádku
 * popisu chyby na stderr (užitečné pro shell skripty, které loguj do
 * souboru). Pravidla validace:
 *  - terminátor 0x0D v fname (přes `mzf_header_validate`),
 *  - velikost vstupu **přesně rovna** `128 + fsize` - oba směry odchylky
 *    znamenají chybu (truncated nebo trailing bytes).
 *
 * @param mzf              Načtená MZF struktura.
 * @param total_input_size Celková velikost načteného vstupu (header + body).
 * @return EXIT_SUCCESS pokud všechny kontroly projdou, EXIT_FAILURE jinak.
 */
static int validate_quiet ( const st_MZF *mzf, size_t total_input_size ) {
    const st_MZF_HEADER *hdr = &mzf->header;

    en_MZF_ERROR herr = mzf_header_validate ( hdr );
    if ( herr != MZF_OK ) {
        fprintf ( stderr, "mzf-info: validation failed: missing fname terminator\n" );
        return EXIT_FAILURE;
    }

    size_t expected = (size_t) MZF_HEADER_SIZE + (size_t) hdr->fsize;
    if ( total_input_size < expected ) {
        fprintf ( stderr, "mzf-info: validation failed: truncated (filesize %zu < 128 + fsize %zu)\n",
                  total_input_size, expected );
        return EXIT_FAILURE;
    }
    if ( total_input_size > expected ) {
        fprintf ( stderr, "mzf-info: validation failed: trailing bytes (filesize %zu > 128 + fsize %zu)\n",
                  total_input_size, expected );
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Hlavní vstupní bod nástroje mzf-info.
 *
 * Postup:
 *  1. Na MSYS2/Windows přepne stdin a stdout do binárního režimu.
 *  2. Inicializuje paměťový driver.
 *  3. Naparsuje CLI argumenty.
 *  4. Načte vstup (file nebo stdin) do paměťového bufferu.
 *  5. Otevře paměťový handler s odpovídající velikostí, naplní ho
 *     a předá `mzf_load`, který provede konverzi endianity hlavičky
 *     a alokaci těla.
 *  6. Podle voleb vytiskne text hlavičky, hexdump těla nebo pouze
 *     vrátí návratový kód (`--validate`).
 *
 * @param argc Počet argumentů.
 * @param argv Pole argumentů.
 * @return EXIT_SUCCESS při úspěchu (nebo validním souboru ve `--validate`),
 *         jinak EXIT_FAILURE.
 */
int main ( int argc, char *argv[] ) {
#ifdef _WIN32
    /* Bez tohoto by stdin přerušil 0x1A a stdout by konvertoval LF -> CRLF. */
    setmode ( fileno ( stdin ),  _O_BINARY );
    setmode ( fileno ( stdout ), _O_BINARY );
#endif

    /* Inicializace globálních paměťových driverů (zabudované callbacky). */
    memory_driver_init ();

    st_MZF_INFO_OPTIONS opts = {
        .charset        = TOOL_CHARSET_EU,
        .header_only    = false,
        .body_only      = false,
        .hexdump        = false,
        .validate       = false,
        .hexdump_offset = 0,
        .hexdump_length = 0,
        .has_offset     = false,
        .has_length     = false,
        .input_path     = NULL,
        .use_stdin      = false,
        .cpm_decode     = true,
        .format         = OUTFMT_TEXT,
    };

    int pa = parse_args ( argc, argv, &opts );
    if ( pa < 0 ) {
        fprintf ( stderr, "Try 'mzf-info --help' for more information.\n" );
        return EXIT_FAILURE;
    }
    if ( pa > 0 ) {
        return EXIT_SUCCESS;
    }

    /* Alokace bufferu pro celý MZF (header + max body + 1 pro overflow detekci). */
    uint8_t *input_buf = malloc ( MZF_INFO_INPUT_BUF_CAP );
    if ( !input_buf ) {
        fprintf ( stderr, "Error: cannot allocate %zu bytes for input buffer\n",
                  MZF_INFO_INPUT_BUF_CAP );
        return EXIT_FAILURE;
    }

    size_t input_size = 0;
    int rc;
    if ( opts.use_stdin ) {
        rc = read_stdin_to_buffer ( input_buf, MZF_INFO_INPUT_BUF_CAP, &input_size );
    } else {
        rc = load_file_to_buffer ( opts.input_path, input_buf, MZF_INFO_INPUT_BUF_CAP, &input_size );
    }
    if ( rc != 0 ) {
        free ( input_buf );
        return EXIT_FAILURE;
    }

    /* Detekce přetečení (vstup větší než MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE). */
    if ( input_size > (size_t) MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE ) {
        fprintf ( stderr, "Error: input is larger than maximum MZF size (%u bytes)\n",
                  (unsigned) ( MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE ) );
        free ( input_buf );
        return EXIT_FAILURE;
    }
    if ( input_size < MZF_HEADER_SIZE ) {
        fprintf ( stderr, "Error: input shorter than MZF header (%zu bytes, need at least %u)\n",
                  input_size, (unsigned) MZF_HEADER_SIZE );
        free ( input_buf );
        return EXIT_FAILURE;
    }

    /* Předem si přečteme fsize z hlavičky (offset 0x12, little-endian)
       a porovnáme s reálnou velikostí vstupu. Knihovní `mzf_load` při
       truncated MZF (`input_size < 128 + fsize`) vrátí MZF_ERROR_IO,
       což ztrácí informaci, že příčinou bylo zkrácení. Tuto detekci
       chceme prezentovat uživateli explicitně - ve `--validate` jako
       jedinou stderr řádku, v default režimu jako součást validační
       sekce. Proto v případě truncated body krátíme cestu a sami
       voláme `validate_quiet` nebo vypisujeme zkrácenou diagnostiku. */
    uint16_t expected_fsize = (uint16_t) input_buf[0x12]
                              | ( (uint16_t) input_buf[0x13] << 8 );
    size_t expected_total = (size_t) MZF_HEADER_SIZE + (size_t) expected_fsize;
    bool truncated_body = ( input_size < expected_total );

    st_MZF *mzf = NULL;
    st_HANDLER handler;
    if ( !truncated_body ) {
        /* Otevření paměťového handleru a naplnění obsahem. */
        if ( !generic_driver_open_memory ( &handler, &g_memory_driver_static,
                                           (uint32_t) input_size ) ) {
            fprintf ( stderr, "Error: cannot allocate memory handler (%zu bytes)\n", input_size );
            free ( input_buf );
            return EXIT_FAILURE;
        }
        if ( generic_driver_write ( &handler, 0, input_buf, (uint32_t) input_size ) != EXIT_SUCCESS ) {
            fprintf ( stderr, "Error: cannot fill memory handler\n" );
            generic_driver_close ( &handler );
            free ( input_buf );
            return EXIT_FAILURE;
        }

        /* Načtení MZF (auto konverze endianity hlavičky a alokace body). */
        en_MZF_ERROR mzf_err = MZF_OK;
        mzf = mzf_load ( &handler, &mzf_err );
        generic_driver_close ( &handler );
        if ( !mzf ) {
            fprintf ( stderr, "Error: cannot parse MZF: %s\n", mzf_error_string ( mzf_err ) );
            free ( input_buf );
            return EXIT_FAILURE;
        }
    } else {
        /* Truncated body: zkonstruujeme minimální `st_MZF` pouze z hlavičky
           (body=NULL, body_size=0) tak, že naplníme handler jen prvními
           128 bajty. mzf_load tak prochází, fsize v `mzf->header` bude
           odpovídat tomu, co bylo v souboru, a `validate_quiet` /
           `print_header_text` rozpoznají truncated stav skrze
           `total_input_size < 128 + fsize`. */
        if ( !generic_driver_open_memory ( &handler, &g_memory_driver_static,
                                           (uint32_t) MZF_HEADER_SIZE ) ) {
            fprintf ( stderr, "Error: cannot allocate memory handler (128 bytes)\n" );
            free ( input_buf );
            return EXIT_FAILURE;
        }
        if ( generic_driver_write ( &handler, 0, input_buf, (uint32_t) MZF_HEADER_SIZE ) != EXIT_SUCCESS ) {
            fprintf ( stderr, "Error: cannot fill memory handler\n" );
            generic_driver_close ( &handler );
            free ( input_buf );
            return EXIT_FAILURE;
        }
        /* Trik: dočasně ručně přepíšeme fsize na 0 v hlavičce na pozici
           0x12 v handleru, aby mzf_load nealokoval truncated body buffer
           (a nečetl mimo dostupný rozsah). Skutečnou fsize v `mzf->header`
           obnovíme po loadu, aby `validate_quiet` viděla tu skutečnou. */
        uint8_t zero_fsize[2] = { 0, 0 };
        if ( generic_driver_write ( &handler, 0x12, zero_fsize, 2 ) != EXIT_SUCCESS ) {
            fprintf ( stderr, "Error: cannot patch handler\n" );
            generic_driver_close ( &handler );
            free ( input_buf );
            return EXIT_FAILURE;
        }
        en_MZF_ERROR mzf_err = MZF_OK;
        mzf = mzf_load ( &handler, &mzf_err );
        generic_driver_close ( &handler );
        if ( !mzf ) {
            fprintf ( stderr, "Error: cannot parse truncated MZF header: %s\n",
                      mzf_error_string ( mzf_err ) );
            free ( input_buf );
            return EXIT_FAILURE;
        }
        /* Obnovení skutečné fsize - print_header_text/validate_quiet ji
           porovnají proti `input_size` a detekují truncation. */
        mzf->header.fsize = expected_fsize;
    }

    int exit_code = EXIT_SUCCESS;

    if ( opts.validate ) {
        exit_code = validate_quiet ( mzf, input_size );
    } else if ( opts.body_only ) {
        size_t off = opts.has_offset ? (size_t) opts.hexdump_offset : 0;
        size_t len = opts.has_length ? (size_t) opts.hexdump_length
                                      : ( mzf->body_size - off );
        if ( hexdump_range ( mzf->body, mzf->body_size, off, len ) != 0 ) {
            exit_code = EXIT_FAILURE;
        }
    } else {
        /* Default: header text nebo strukturovaný výstup (JSON/CSV). Při
           --header-only stejně, jen bez hexdumpu. Hexdump je text-only -
           do JSON/CSV větve nepatří (mutex zajištěn v parse_args). */
        if ( opts.format == OUTFMT_TEXT ) {
            print_header_text ( mzf, input_size, opts.charset, opts.cpm_decode );
        } else {
            print_header_structured ( mzf, input_size, opts.charset,
                                      opts.cpm_decode, opts.format );
        }

        if ( opts.hexdump && !opts.header_only ) {
            size_t off = opts.has_offset ? (size_t) opts.hexdump_offset : 0;
            size_t len = opts.has_length ? (size_t) opts.hexdump_length
                                          : ( mzf->body_size - off );
            printf ( "\nBody hexdump:\n" );
            if ( hexdump_range ( mzf->body, mzf->body_size, off, len ) != 0 ) {
                exit_code = EXIT_FAILURE;
            }
        }
    }

    mzf_free ( mzf );
    free ( input_buf );
    return exit_code;
}
