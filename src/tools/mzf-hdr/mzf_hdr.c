/**
 * @file   mzf_hdr.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  CLI nástroj rodiny bin2mzf - modifikace hlavičky existujícího MZF.
 *
 * Načte existující MZF soubor (positional argument), aplikuje změny
 * vybraných polí hlavičky podle CLI voleb a zapíše výsledek buď in-place
 * (atomic tmp + rename) nebo do souboru zadaného přes `-o`. **Tělo MZF
 * zůstává byte-identicky zachované.**
 *
 * Sémantika je read-modify-write nad hlavičkou: každé pole, pro které
 * uživatel zadal příslušnou volbu, se přepíše; ostatní pole zůstávají
 * z původního MZF beze změny. Při zadání `--cpm` se hlavička přepíše
 * SOKODI CMT.COM presetem (analogicky k `bin2mzf --cpm`).
 *
 * Verze 0.1.0 (MVP) scope:
 *  - argumenty: `-t/--type`, `-n/--name`, `-l/--load-addr`, `-e/--exec-addr`,
 *    `-c/--comment`, `--cmnt-bin FILE`, `-s/--size N`, `--no-auto-size`,
 *    `--charset`, `--upper`, `-o/--output`, positional `<input.mzf>`.
 *  - CP/M plně: `--cpm`, `--cpm-name`, `--cpm-ext`, `--cpm-attr`
 *    (sémantika 1:1 s `bin2mzf --cpm`).
 *  - Default `--auto-size`: `fsize` se přepočítá na `body_size` po loadu.
 *    `-s N` implicitně vypne auto-size (DWIM). `--no-auto-size` zachová
 *    původní `fsize` z input.mzf.
 *  - In-place vs `-o`: bez `-o` modifikuje `input.mzf` atomic přes
 *    `<input>.tmp` + rename.
 *  - `--cmnt-bin FILE`: načte raw bajty z FILE jako pole `cmnt` (104 B),
 *    paduje nulami pokud kratší, trunkuje s warningem pokud delší.
 *    Mutex s `-c/--comment`.
 *  - `--version`, `--lib-versions`, `--help`.
 *
 * Mimo scope tohoto MVP / v0.4.0:
 *  - **`--new` mode** - tvorba MZF od nuly z raw binárky. Duplikace `bin2mzf`,
 *    samostatný task po M4 pokud bude potřeba.
 *  - **`--header-only`** - input je vždy MZF s tělem.
 *  - **Modifikace těla** (`mzf-paste` task).
 *  - **`--align-block`/`--align-to`** - mzf-hdr neformátuje výsledek;
 *    tělo zůstává byte-identické.
 *  - **`--format json|csv`** výstup - závisí na knihovně `output_format`,
 *    plánováno v0.5.0.
 *
 * @par Layout pole `fname.name[]` v CP/M módu (přepis SOKODI):
 *  - `[0..7]`   = jméno (uppercase, padding mezerami)
 *  - `[8]`      = '.' (0x2E)
 *  - `[9..11]`  = přípona + bit 7 = atribut (R/O/SYS/ARC)
 *  - `[12]`     = 0x0D
 *  - `[13..15]` = 0x0D 0x0D 0x0D (parita s `bin2mzf --cpm`)
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
#include "tools/common/bin2mzf_cli_version.h"
#include "tools/common/argparse_helpers.h"
#include "tools/common/text_field.h"
#include "tools/common/mzf_io.h"
#include "tools/common/cpm_constants.h"
#include "tools/common/cpm_helpers.h"

/** @brief Verze nástroje mzf-hdr. */
#define MZF_HDR_VERSION "0.1.0"

/**
 * @brief Volby CLI parsované z argumentů `mzf-hdr`.
 *
 * Drží referenční ukazatele do `argv` (žádná vlastní alokace), životnost
 * vázaná na životnost procesu.
 *
 * Pro každé volitelné pole je samostatný `*_set` flag, který rozliší
 * "uživatel zadal explicitně" (pole se přepíše) od "uživatel nezadal"
 * (pole z input.mzf se zachová). Toto je klíčový rozdíl proti
 * `st_BIN2MZF_OPTIONS` v `bin2mzf.c`, kde má každé pole povinnou
 * default hodnotu.
 *
 * Mutex/cross-validace probíhá v `validate_mutex` po `parse_args`.
 */
typedef struct st_MZF_HDR_OPTIONS {
    const char    *input_path;       /**< positional `<input.mzf>` (povinný) */
    const char    *output_path;      /**< -o/--output, NULL = in-place */

    en_TOOL_CHARSET charset;         /**< --charset (default EU) */
    bool            charset_set;     /**< true = uživatel zadal --charset */

    uint8_t         ftype;           /**< -t/--type */
    bool            ftype_set;       /**< true = uživatel zadal -t/--type */

    const char     *name;            /**< -n/--name */
    bool            name_set;        /**< true = uživatel zadal -n/--name */

    bool            upper;           /**< --upper */
    bool            upper_set;       /**< true = uživatel zadal --upper */

    const char     *comment;         /**< -c/--comment */
    bool            comment_set;     /**< true = uživatel zadal -c/--comment */

    const char     *cmnt_bin_path;   /**< --cmnt-bin FILE */
    bool            cmnt_bin_set;    /**< true = uživatel zadal --cmnt-bin */

    uint16_t        load_addr;       /**< -l/--load-addr */
    bool            load_addr_set;   /**< true = uživatel zadal -l/--load-addr */

    uint16_t        exec_addr;       /**< -e/--exec-addr */
    bool            exec_addr_set;   /**< true = uživatel zadal -e/--exec-addr */

    uint16_t        size;            /**< -s/--size N */
    bool            size_set;        /**< true = uživatel zadal -s/--size */
    bool            no_auto_size;    /**< --no-auto-size (default false = auto-size on) */

    bool            cpm;             /**< --cpm */
    const char     *cpm_name;        /**< --cpm-name */
    bool            cpm_name_set;
    const char     *cpm_ext;         /**< --cpm-ext */
    bool            cpm_ext_set;
    uint8_t         cpm_attr_mask;   /**< --cpm-attr (bitmaska MZF_CPM_ATTR_*) */
    bool            cpm_attr_set;
} st_MZF_HDR_OPTIONS;

/**
 * @brief Vypíše Usage / nápovědu nástroje mzf-hdr.
 *
 * Text je v angličtině (locales-ready). Volá se z `--help` (na stdout)
 * i z chybových cest (na stderr).
 *
 * @param out Cílový stream (stdout / stderr).
 */
static void print_usage ( FILE *out ) {
    fprintf ( out,
        "Usage: mzf-hdr [options] [-o OUTPUT.mzf] <INPUT.mzf>\n"
        "\n"
        "Modify the header of an existing MZF file in place or to a new file.\n"
        "Body bytes remain unchanged. Each option that is not given on the\n"
        "command line preserves the corresponding header field from INPUT.\n"
        "\n"
        "Options (header fields):\n"
        "  -t, --type TYPE            File type: 0x01..0xff or symbolic\n"
        "                             (obj, btx, bsd, brd, rb).\n"
        "  -n, --name NAME            File name (max 16 chars after charset conversion).\n"
        "  -l, --load-addr ADDR       Z80 load address (decimal or 0x-prefixed hex).\n"
        "  -e, --exec-addr ADDR       Z80 execution address.\n"
        "  -c, --comment TEXT         Comment text (max 104 chars, longer is truncated).\n"
        "      --cmnt-bin FILE        Read raw 104 bytes from FILE into the comment\n"
        "                             field (zero-pad if shorter, truncate with warning\n"
        "                             if longer). Mutually exclusive with -c/--comment.\n"
        "  -s, --size N               Set fsize to N bytes (decimal or 0x-prefixed hex,\n"
        "                             1..65535). Implies --no-auto-size.\n"
        "      --no-auto-size         Keep the original fsize from INPUT.mzf instead of\n"
        "                             auto-recomputing it from the body length.\n"
        "      --charset MODE         Charset for --name and --comment: eu (default),\n"
        "                             jp, utf8-eu, utf8-jp, none.\n"
        "      --upper                Uppercase a-z -> A-Z before charset conversion.\n"
        "\n"
        "CP/M mode (SOKODI CMT.COM convention):\n"
        "      --cpm                  Enable CP/M mode (rewrites ftype=0x22,\n"
        "                             load=exec=0x0100, fname using SOKODI 8.3 layout).\n"
        "      --cpm-name NAME        CP/M filename (max 8 chars, ASCII, uppercased).\n"
        "      --cpm-ext EXT          CP/M extension (max 3 chars, default: COM).\n"
        "      --cpm-attr LIST        CP/M attributes: ro, sys, arc.\n"
        "\n"
        "Output:\n"
        "  -o, --output FILE          Write modified MZF to FILE. Without this flag,\n"
        "                             INPUT.mzf is modified in place (atomic tmp + rename).\n"
        "      --version              Print tool version and exit.\n"
        "      --lib-versions         Print library versions and exit.\n"
        "  -h, --help                 Print this help and exit.\n"
        "\n"
        "Note: --cpm is incompatible with --type, --load-addr, --exec-addr,\n"
        "--charset, and --name. The --comment / --cmnt-bin options remain available.\n"
    );
}

/**
 * @brief Vypíše verzi nástroje a verzi celé rodiny CLI.
 *
 * Formát: `mzf-hdr <tool> (<release-name> <release-version>)`.
 */
static void print_version ( void ) {
    printf ( "mzf-hdr %s (%s %s)\n",
             MZF_HDR_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
}

/**
 * @brief Vypíše verze všech linkovaných knihoven.
 *
 * Slouží k diagnostice ABI mismatch a pro reportování v bug reports.
 */
static void print_lib_versions ( void ) {
    printf ( "mzf-hdr %s (%s %s)\n\n",
             MZF_HDR_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
    printf ( "Library versions:\n" );
    printf ( "  mzf               %s\n", mzf_version () );
    printf ( "  generic_driver    %s\n", generic_driver_version () );
    printf ( "  endianity         %s\n", endianity_version () );
    printf ( "  sharpmz_ascii     %s\n", sharpmz_ascii_version () );
}

/**
 * @brief Naparsuje argumenty z `argv` do struktury `st_MZF_HDR_OPTIONS`.
 *
 * Při zachycení `--help`, `--version` nebo `--lib-versions` rovnou
 * vypíše příslušný text a vrátí 1 (signál pro `main`, aby ukončil
 * s `EXIT_SUCCESS`). Při chybě vrátí -1. Při úspěšném naparsování
 * vrátí 0.
 *
 * Mutex/povinné validace probíhají v `validate_mutex` po této funkci.
 *
 * Positional argument: právě jeden, `<input.mzf>`. Žádný `-i/--input`
 * flag (konzistence s `mzf-info`, `mzf-strip`).
 *
 * @param argc Počet argumentů.
 * @param argv Pole argumentů.
 * @param opts Výstup: naparsované volby (předpokládá memset(0) + defaulty).
 * @return 0 OK, 1 = help/version vypsáno (exit success), -1 = chyba.
 */
static int parse_args ( int argc, char *argv[], st_MZF_HDR_OPTIONS *opts ) {
    /* Hodnoty pro long-only options bez krátkého ekvivalentu */
    enum {
        OPT_CMNT_BIN     = 1000,
        OPT_NO_AUTO_SIZE,
        OPT_CHARSET,
        OPT_UPPER,
        OPT_CPM,
        OPT_CPM_NAME,
        OPT_CPM_EXT,
        OPT_CPM_ATTR,
        OPT_VERSION,
        OPT_LIB_VERSIONS
    };

    static const struct option long_options[] = {
        { "output",        required_argument, NULL, 'o' },
        { "type",          required_argument, NULL, 't' },
        { "name",          required_argument, NULL, 'n' },
        { "comment",       required_argument, NULL, 'c' },
        { "cmnt-bin",      required_argument, NULL, OPT_CMNT_BIN },
        { "load-addr",     required_argument, NULL, 'l' },
        { "exec-addr",     required_argument, NULL, 'e' },
        { "size",          required_argument, NULL, 's' },
        { "no-auto-size",  no_argument,       NULL, OPT_NO_AUTO_SIZE },
        { "charset",       required_argument, NULL, OPT_CHARSET },
        { "upper",         no_argument,       NULL, OPT_UPPER },
        { "cpm",           no_argument,       NULL, OPT_CPM },
        { "cpm-name",      required_argument, NULL, OPT_CPM_NAME },
        { "cpm-ext",       required_argument, NULL, OPT_CPM_EXT },
        { "cpm-attr",      required_argument, NULL, OPT_CPM_ATTR },
        { "version",       no_argument,       NULL, OPT_VERSION },
        { "lib-versions",  no_argument,       NULL, OPT_LIB_VERSIONS },
        { "help",          no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* vlastní chybové hlášky, getopt ať mlčí */
    opterr = 0;

    int c;
    while ( ( c = getopt_long ( argc, argv, "o:t:n:c:l:e:s:h", long_options, NULL ) ) != -1 ) {
        switch ( c ) {
            case 'o':
                opts->output_path = optarg;
                break;
            case 't':
                if ( argparse_ftype ( optarg, &opts->ftype ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --type value '%s'\n", optarg );
                    return -1;
                }
                opts->ftype_set = true;
                break;
            case 'n':
                opts->name = optarg;
                opts->name_set = true;
                break;
            case 'c':
                opts->comment = optarg;
                opts->comment_set = true;
                break;
            case OPT_CMNT_BIN:
                opts->cmnt_bin_path = optarg;
                opts->cmnt_bin_set = true;
                break;
            case 'l':
                if ( argparse_uint16 ( optarg, &opts->load_addr ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --load-addr value '%s'\n", optarg );
                    return -1;
                }
                opts->load_addr_set = true;
                break;
            case 'e':
                if ( argparse_uint16 ( optarg, &opts->exec_addr ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --exec-addr value '%s'\n", optarg );
                    return -1;
                }
                opts->exec_addr_set = true;
                break;
            case 's': {
                /* `-s N` v mzf-hdr nepodporuje keyword "auto" - default je
                   auto-size, takže explicitní `-s N` znamená "fixní hodnotu".
                   `-s N` implicitně vypne auto-size (DWIM). */
                uint16_t v = 0;
                if ( argparse_uint16 ( optarg, &v ) != 0 || v == 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --size value '%s' (expected: 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                opts->size = v;
                opts->size_set = true;
                opts->no_auto_size = true;
                break;
            }
            case OPT_NO_AUTO_SIZE:
                opts->no_auto_size = true;
                break;
            case OPT_CHARSET: {
                en_TOOL_CHARSET cs = TOOL_CHARSET_EU;
                if ( argparse_charset ( optarg, &cs ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --charset value '%s' (expected: eu, jp, utf8-eu, utf8-jp, none)\n",
                              optarg );
                    return -1;
                }
                opts->charset = cs;
                opts->charset_set = true;
                break;
            }
            case OPT_UPPER:
                opts->upper = true;
                opts->upper_set = true;
                break;
            case OPT_CPM:
                opts->cpm = true;
                break;
            case OPT_CPM_NAME:
                opts->cpm_name = optarg;
                opts->cpm_name_set = true;
                break;
            case OPT_CPM_EXT:
                opts->cpm_ext = optarg;
                opts->cpm_ext_set = true;
                break;
            case OPT_CPM_ATTR:
                if ( mzf_cpm_parse_attr ( optarg, &opts->cpm_attr_mask ) != 0 ) {
                    return -1;
                }
                opts->cpm_attr_set = true;
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
                if ( optopt ) {
                    fprintf ( stderr, "Error: unknown or malformed option '-%c'\n", optopt );
                } else {
                    fprintf ( stderr, "Error: unknown option '%s'\n",
                              ( optind > 0 && optind <= argc ) ? argv[optind - 1] : "?" );
                }
                return -1;
        }
    }

    /* Positional: právě 1 argument (input.mzf) */
    int npos = argc - optind;
    if ( npos != 1 ) {
        fprintf ( stderr, "Error: exactly 1 positional argument required: <input.mzf>\n" );
        return -1;
    }
    opts->input_path = argv[optind];

    return 0;
}

/**
 * @brief Validuje vzájemnou výlučnost voleb (mutex check).
 *
 * Pravidla:
 *  - `--cpm` je vzájemně výlučný s `-t/--type`, `-l/--load-addr`,
 *    `-e/--exec-addr`, `--charset`, `-n/--name` (preset přepíše
 *    všechny tyto fields a explicitní volby by skrytě konfliktovaly).
 *    Volby `-c/--comment` a `--cmnt-bin` jsou v CP/M módu povolené.
 *  - `--cpm-name`, `--cpm-ext`, `--cpm-attr` bez `--cpm` = hard error.
 *  - `-c/--comment` a `--cmnt-bin` jsou vzájemně výlučné.
 *  - `--cpm` && !`--cpm-name` = hard error (povinné jméno v CP/M módu).
 *
 * @param opts Naparsované volby.
 * @return 0 OK, -1 chyba (hláška vypsána na stderr).
 */
static int validate_mutex ( const st_MZF_HDR_OPTIONS *opts ) {
    if ( opts->cpm ) {
        if ( opts->ftype_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --type\n" );
            return -1;
        }
        if ( opts->load_addr_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --load-addr\n" );
            return -1;
        }
        if ( opts->exec_addr_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --exec-addr\n" );
            return -1;
        }
        if ( opts->charset_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --charset\n" );
            return -1;
        }
        if ( opts->name_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --name\n" );
            return -1;
        }
        if ( !opts->cpm_name_set ) {
            fprintf ( stderr, "Error: --cpm requires --cpm-name\n" );
            return -1;
        }
    } else {
        if ( opts->cpm_name_set ) {
            fprintf ( stderr, "Error: --cpm-name requires --cpm\n" );
            return -1;
        }
        if ( opts->cpm_ext_set ) {
            fprintf ( stderr, "Error: --cpm-ext requires --cpm\n" );
            return -1;
        }
        if ( opts->cpm_attr_set ) {
            fprintf ( stderr, "Error: --cpm-attr requires --cpm\n" );
            return -1;
        }
    }

    if ( opts->comment_set && opts->cmnt_bin_set ) {
        fprintf ( stderr, "Error: --comment and --cmnt-bin are mutually exclusive\n" );
        return -1;
    }

    return 0;
}

/**
 * @brief Načte soubor s binárním komentářem do bufferu (104 B).
 *
 * Otevře `path` pro binární čtení a načte až `MZF_CMNT_LENGTH` bajtů.
 *  - Soubor přesně 104 B: zkopíruje raw.
 *  - Soubor < 104 B: zkopíruje a paduje nulami do 104 B (silent).
 *  - Soubor > 104 B: zkopíruje prvních 104 B, vypíše warning na stderr
 *    se slovem "truncated" (regression invariant).
 *
 * @param path     Cesta k souboru s binárním komentářem.
 * @param out_buf  Výstupní buffer délky `MZF_CMNT_LENGTH` (104 B).
 *                 Vyplní se na celých 104 B (nuly tam, kde nebyly data).
 *
 * @return 0 OK, -1 chyba (I/O, NULL parametry).
 */
static int load_cmnt_bin ( const char *path, uint8_t out_buf[MZF_CMNT_LENGTH] ) {
    if ( !path || !out_buf ) return -1;

    FILE *fp = fopen ( path, "rb" );
    if ( !fp ) {
        fprintf ( stderr, "Error: cannot open --cmnt-bin file '%s': %s\n",
                  path, strerror ( errno ) );
        return -1;
    }

    /* Pre-fill nulami pro případ kratšího souboru. */
    memset ( out_buf, 0x00, MZF_CMNT_LENGTH );

    size_t total = 0;
    while ( total < MZF_CMNT_LENGTH ) {
        size_t n = fread ( out_buf + total, 1, MZF_CMNT_LENGTH - total, fp );
        if ( n == 0 ) break;
        total += n;
    }

    /* Detekce přetečení - pokud načtení naplnilo plnou kapacitu,
       zkusíme jeden bajt navíc. Když projde, soubor je delší. */
    bool truncated = false;
    if ( total == MZF_CMNT_LENGTH ) {
        uint8_t probe;
        if ( fread ( &probe, 1, 1, fp ) == 1 ) {
            truncated = true;
        }
    }
    int err = ferror ( fp );
    fclose ( fp );

    if ( err ) {
        fprintf ( stderr, "Error: read error on --cmnt-bin file '%s'\n", path );
        return -1;
    }
    if ( truncated ) {
        /* Slovo "truncated" je povinný invariant pro testy. */
        fprintf ( stderr,
                  "Warning: --cmnt-bin file truncated to %u bytes\n",
                  (unsigned) MZF_CMNT_LENGTH );
    }
    return 0;
}

/**
 * @brief Hlavní vstupní bod nástroje mzf-hdr.
 *
 * Pipeline:
 *  1. `setmode(stdout, _O_BINARY)` na Win32 (defenzivně, mzf-hdr
 *     primárně nepíše na stdout, ale konzistence s ostatními nástroji).
 *  2. `memory_driver_init()` - povinné před použitím memory handlerů.
 *  3. `parse_args` + `validate_mutex`.
 *  4. `load_mzf_from_file(input_path)` - načtení a validace.
 *  5. Aplikace modifikací podle voleb (CP/M preset nebo individuální fields).
 *  6. Přepočet `fsize` (auto-size default, `-s N` / `--no-auto-size`)
 *     včetně validace `fsize <= body_size` (jinak hard error - `mzf-hdr`
 *     neumí rozšířit tělo).
 *  7. Zápis: `-o` -> `write_mzf_to_file`, jinak `write_mzf_in_place`.
 *  8. Cleanup `mzf_free`.
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

    st_MZF_HDR_OPTIONS opts;
    memset ( &opts, 0, sizeof ( opts ) );
    opts.charset = TOOL_CHARSET_EU;

    int prc = parse_args ( argc, argv, &opts );
    if ( prc == 1 ) return EXIT_SUCCESS;
    if ( prc != 0 ) {
        fprintf ( stderr, "Try 'mzf-hdr --help' for more information.\n" );
        return EXIT_FAILURE;
    }

    if ( validate_mutex ( &opts ) != 0 ) {
        fprintf ( stderr, "Try 'mzf-hdr --help' for more information.\n" );
        return EXIT_FAILURE;
    }

    int exit_code = EXIT_FAILURE;
    st_MZF *mzf = NULL;

    /* Načtení vstupního MZF. */
    if ( load_mzf_from_file ( opts.input_path, &mzf ) != 0 ) {
        goto cleanup;
    }

    /* Aplikace modifikací. */
    if ( opts.cpm ) {
        /* CP/M preset: ftype, fstrt/fexec, sestavení fname přes SOKODI
           layout. Tělo zůstává beze změny. */
        mzf->header.ftype = MZF_CPM_FTYPE;
        mzf->header.fstrt = MZF_CPM_DEFAULT_ADDR;
        mzf->header.fexec = MZF_CPM_DEFAULT_ADDR;

        uint8_t cpm_fname_buf[MZF_CPM_FNAME_LEN];
        const char *ext = opts.cpm_ext_set ? opts.cpm_ext : MZF_CPM_DEFAULT_EXT;
        if ( mzf_cpm_build_fname ( opts.cpm_name, ext,
                                   opts.cpm_attr_mask, cpm_fname_buf ) != 0 ) {
            goto cleanup;
        }
        memcpy ( mzf->header.fname.name, cpm_fname_buf, MZF_CPM_FNAME_LEN );
        /* Zbytek pole `fname.name[13..15]` přepíšeme na 0x0D - parita
           s `bin2mzf --cpm` (knihovní default z `mzf_tools_create_mzfhdr`). */
        memset ( &mzf->header.fname.name[MZF_CPM_FNAME_LEN], 0x0D,
                 MZF_FILE_NAME_LENGTH - MZF_CPM_FNAME_LEN );

        /* Komentář: -c nebo --cmnt-bin (povolené v CP/M módu).
           transform_text_field s charset=NONE (CP/M preset). */
        if ( opts.comment_set ) {
            uint8_t cmnt_buf[MZF_CMNT_LENGTH];
            memset ( cmnt_buf, 0, sizeof ( cmnt_buf ) );
            size_t cmnt_len = 0;
            if ( transform_text_field ( opts.comment, TOOL_CHARSET_NONE,
                                        opts.upper, TEXT_FIELD_COMMENT,
                                        MZF_CMNT_LENGTH, cmnt_buf, &cmnt_len ) != 0 ) {
                goto cleanup;
            }
            memset ( mzf->header.cmnt, 0, MZF_CMNT_LENGTH );
            memcpy ( mzf->header.cmnt, cmnt_buf, cmnt_len );
        } else if ( opts.cmnt_bin_set ) {
            if ( load_cmnt_bin ( opts.cmnt_bin_path, mzf->header.cmnt ) != 0 ) {
                goto cleanup;
            }
        }
    } else {
        /* Individuální modifikace polí. */
        if ( opts.ftype_set ) {
            mzf->header.ftype = opts.ftype;
        }
        if ( opts.load_addr_set ) {
            mzf->header.fstrt = opts.load_addr;
        }
        if ( opts.exec_addr_set ) {
            mzf->header.fexec = opts.exec_addr;
        }
        if ( opts.name_set ) {
            uint8_t name_buf[MZF_FILE_NAME_LENGTH];
            size_t  name_len = 0;
            if ( transform_text_field ( opts.name, opts.charset, opts.upper,
                                        TEXT_FIELD_NAME, MZF_FILE_NAME_LENGTH,
                                        name_buf, &name_len ) != 0 ) {
                goto cleanup;
            }
            /* Inicializace celého pole 0x0D (knihovní konvence terminátoru). */
            memset ( mzf->header.fname.name, 0x0D, MZF_FILE_NAME_LENGTH );
            memcpy ( mzf->header.fname.name, name_buf, name_len );
        }
        if ( opts.comment_set ) {
            uint8_t cmnt_buf[MZF_CMNT_LENGTH];
            memset ( cmnt_buf, 0, sizeof ( cmnt_buf ) );
            size_t cmnt_len = 0;
            if ( transform_text_field ( opts.comment, opts.charset, opts.upper,
                                        TEXT_FIELD_COMMENT, MZF_CMNT_LENGTH,
                                        cmnt_buf, &cmnt_len ) != 0 ) {
                goto cleanup;
            }
            memset ( mzf->header.cmnt, 0, MZF_CMNT_LENGTH );
            memcpy ( mzf->header.cmnt, cmnt_buf, cmnt_len );
        }
        if ( opts.cmnt_bin_set ) {
            if ( load_cmnt_bin ( opts.cmnt_bin_path, mzf->header.cmnt ) != 0 ) {
                goto cleanup;
            }
        }
    }

    /* Výpočet fsize:
       - opts.size_set: explicit value, použije se přesně. Pokud je
         hodnota větší než velikost těla, jde o uživatelskou chybu -
         `mzf-hdr` nemodifikuje tělo, takže nemůže rozšířit body buffer.
         Hard error s konkrétní hláškou (substring "exceeds body size"
         je invariant pro testy).
       - opts.no_auto_size (bez size_set): zachová původní fsize.
       - jinak (default): fsize = body_size (auto-size). */
    if ( opts.size_set ) {
        if ( (size_t) opts.size > mzf->body_size ) {
            fprintf ( stderr,
                      "mzf-hdr: error: size 0x%X exceeds body size 0x%X "
                      "(mzf-hdr cannot extend body; use bin2mzf or mzf-paste)\n",
                      opts.size, (unsigned) mzf->body_size );
            goto cleanup;
        }
        mzf->header.fsize = opts.size;
    } else if ( !opts.no_auto_size ) {
        mzf->header.fsize = (uint16_t) mzf->body_size;
    }
    /* opts.no_auto_size && !opts.size_set: ponech původní fsize. */

    /* Finální invariant před zápisem - obrana proti rozbitému vstupu.
       Knihovna `mzf_save` čte `header.fsize` bajtů z body bufferu velikosti
       `body_size`. Pokud `fsize > body_size`, dojde k heap OOB read
       (segfault/silent corruption). Tento check zachytí situaci, kdy
       vstupní MZF byl už rozbitý a `--no-auto-size` ponechal nekonzistentní
       hodnotu fsize. */
    if ( (size_t) mzf->header.fsize > mzf->body_size ) {
        fprintf ( stderr,
                  "mzf-hdr: error: header fsize 0x%X exceeds body size 0x%X "
                  "(input MZF is malformed or --no-auto-size kept inconsistent fsize)\n",
                  mzf->header.fsize, (unsigned) mzf->body_size );
        goto cleanup;
    }

    /* Zápis. */
    int wrc;
    if ( opts.output_path ) {
        wrc = write_mzf_to_file ( opts.output_path, mzf );
    } else {
        wrc = write_mzf_in_place ( opts.input_path, mzf );
    }
    if ( wrc != 0 ) {
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if ( mzf ) mzf_free ( mzf );
    return exit_code;
}
