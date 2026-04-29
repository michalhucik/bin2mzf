/**
 * @file   mzf_cat.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  CLI nástroj rodiny bin2mzf - konkatenace více binárních souborů
 *         do jednoho MZF s jedinou load adresou.
 *
 * Typický use case: CP/M loader (CCP + BDOS + BIOS) jako souvislý blok
 * v paměti Z80, nebo segmenty kódu, které musí jít na sebe v jednom
 * MZF souboru.
 *
 * Verze 0.1.0 scope:
 *  - variadic positional vstup: alespoň 1 binárka, max omezený součtem
 *    velikostí <= MZF_MAX_BODY_SIZE (65535 B)
 *  - argumenty hlavičky 1:1 s `bin2mzf` (ftype, name, load/exec adresa,
 *    comment, charset, upper)
 *  - `--pad-between N` - zarovnání mezi binárkami na hranici N bajtů
 *    (typicky 128, 256), výplň `--filler`
 *  - `--align-block N` / `--align-to N` - finální zarovnání těla
 *    (mutex)
 *  - `-s/--size N|auto` - alias pro `--align-to` (mutex s align-*)
 *  - `--filler BYTE` - výplň pro pad-between i finální align (default
 *    0x00)
 *  - `--charset eu|jp|utf8-eu|utf8-jp|none` (default eu)
 *  - `--upper`
 *  - `--version`, `--lib-versions`, `--help`
 *
 * Mimo scope (řeší další tasky):
 *  - `--cpm` (CP/M konvence pro `mzf-cat`) - samostatný task po M4.
 *  - `--header-only` (sémanticky nedává smysl pro multi-input
 *    concatenation - žádné body k spojování).
 *  - `-i/--input` - variadické vstupy jdou výhradně přes positional
 *    argumenty. Volba není v `long_options[]`, getopt vyhodí
 *    "unknown option".
 *  - `--auto-size` / `--no-auto-size` - bez `--header-only` cesty
 *    nemá variant smysl. Default = auto-size.
 *  - variadický stdin (`mzf-cat - - -` neexistuje).
 *  - JSON/CSV výstup.
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

/** @brief Verze nástroje mzf-cat. */
#define MZF_CAT_VERSION "0.1.0"

/**
 * @brief Volby CLI parsované z argumentů.
 *
 * Drží referenční ukazatele do `argv` (žádná vlastní alokace).
 * Variadické pozicní vstupy se uloží jako `input_paths` a `input_count`
 * - obě pole odkazují přímo do `argv` (žádná kopie).
 *
 * Pole `charset` a `upper` ovlivňují jak `name`, tak `comment` stejně.
 * Pokud konkrétní pole obsahuje C-style escape sekvenci (`\\` v textu),
 * `transform_text_field()` pro ně automaticky vynutí `TOOL_CHARSET_NONE`
 * a vypne uppercase, ale jen pro to dané pole (per-pole granularita).
 */
typedef struct st_MZF_CAT_OPTIONS {
    uint8_t ftype;              /**< typ souboru, default MZF_FTYPE_OBJ */
    bool ftype_set;              /**< true pokud uživatel zadal -t/--type explicitně */
    const char *name;            /**< jméno souboru (povinné, max 16 znaků po konverzi) */
    const char *comment;         /**< komentář (volitelný, ořezává se na 104 B) */
    uint16_t load_addr;          /**< startovací adresa, povinná */
    uint16_t exec_addr;          /**< spouštěcí adresa, default = load_addr */
    bool has_load;               /**< true pokud byl -l/--load-addr zadán */
    bool has_exec;               /**< true pokud byl -e/--exec-addr zadán */
    int charset;                 /**< znaková sada (en_TOOL_CHARSET, default EU) */
    bool upper;                  /**< true = uppercase a-z -> A-Z před konverzí */
    uint16_t size;               /**< hodnota -s/--size N (validní jen pokud has_size) */
    bool has_size;               /**< true pokud --size N (numerické) */
    uint32_t align_block;        /**< --align-block N (1..65535); 0 = nepoužito */
    uint32_t align_to;           /**< --align-to N (1..65535); platné jen při has_align_to */
    bool has_align_to;           /**< true pokud uživatel zadal --align-to */
    uint32_t pad_between;        /**< --pad-between N (1..65535); 0 = nepoužito */
    uint8_t filler;              /**< padding bajt (default 0x00) */
    bool filler_set;             /**< true pokud uživatel zadal --filler */
    const char *output_path;     /**< -o/--output, NULL = stdout */
    const char **input_paths;    /**< pole pozičních vstupů (do argv) */
    int input_count;             /**< počet pozičních vstupů (>= 1) */
} st_MZF_CAT_OPTIONS;

static int parse_args ( int argc, char *argv[], st_MZF_CAT_OPTIONS *opts );
static void print_usage ( FILE *out );
static void print_version ( void );
static void print_lib_versions ( void );

/**
 * @brief Vypíše Usage / nápovědu nástroje mzf-cat.
 *
 * Text je v angličtině (locales-ready). Volá se z `--help` (na stdout)
 * i z chybových cest (na stderr).
 *
 * @param out Cílový stream (stdout / stderr).
 */
static void print_usage ( FILE *out ) {
    fprintf ( out,
        "Usage: mzf-cat -n NAME -l LOAD_ADDR [options] -o <output.mzf> <input1.bin> [<input2.bin> ...]\n"
        "\n"
        "Concatenate one or more binary inputs into a single MZF file with\n"
        "a single load address. Typical use case: CP/M loader (CCP+BDOS+BIOS)\n"
        "as a contiguous block of bytes.\n"
        "\n"
        "Required options:\n"
        "  -n, --name NAME            File name written into MZF header (max 16 chars).\n"
        "  -l, --load-addr ADDR       Z80 load address (e.g. 0x1200 or 4608).\n"
        "\n"
        "Header options:\n"
        "  -t, --type TYPE            File type: 0x01..0xff or symbolic\n"
        "                             (obj, btx, bsd, brd, rb). Default: 0x01 (obj).\n"
        "  -e, --exec-addr ADDR       Z80 execution address. Default: equal to load-addr.\n"
        "  -c, --comment TEXT         Comment text (max 104 chars, longer is truncated).\n"
        "  -C, --charset MODE         Charset for --name and --comment: eu (default),\n"
        "                             jp, utf8-eu, utf8-jp, none.\n"
        "      --upper                Uppercase a-z -> A-Z before charset conversion.\n"
        "\n"
        "Layout options:\n"
        "      --pad-between N        Pad each non-last input up to next multiple of N\n"
        "                             bytes (e.g. 128, 256). Padding uses --filler.\n"
        "  -s, --size N|auto          Final body size: numeric (1..65535) or 'auto'.\n"
        "                             Alias for --align-to N.\n"
        "      --align-block N        Pad final body to next multiple of N bytes.\n"
        "      --align-to N           Force final body to exactly N bytes (truncate or pad).\n"
        "      --filler BYTE          Padding byte for pad-between and --align-* / --size.\n"
        "                             Default: 0x00.\n"
        "\n"
        "Output options:\n"
        "  -o, --output FILE          Output MZF file. Default: write to stdout.\n"
        "      --version              Print tool version and exit.\n"
        "      --lib-versions         Print library versions and exit.\n"
        "  -h, --help                 Print this help and exit.\n"
        "\n"
        "Numeric values accept decimal or 0x-prefixed hexadecimal notation.\n"
        "\n"
        "Note: --align-block, --align-to and --size are mutually exclusive.\n"
        "      Inputs are taken as positional arguments only (no -i/--input).\n"
        "\n"
        "Escape sequences in --name and --comment:\n"
        "  \\\\, \\n, \\r, \\t, \\xNN are recognized. If any escape sequence\n"
        "  is present in the field, --charset and --upper are ignored for\n"
        "  that field (charset is treated as 'none').\n"
    );
}

/**
 * @brief Vypíše verzi nástroje a verzi celé rodiny CLI.
 *
 * Formát: `mzf-cat <tool> (<release-name> <release-version>)`.
 */
static void print_version ( void ) {
    printf ( "mzf-cat %s (%s %s)\n",
             MZF_CAT_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
}

/**
 * @brief Vypíše verze všech linkovaných knihoven.
 *
 * Slouží k diagnostice ABI mismatch a pro reportování v bug reports.
 */
static void print_lib_versions ( void ) {
    printf ( "mzf-cat %s (%s %s)\n\n",
             MZF_CAT_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
    printf ( "Library versions:\n" );
    printf ( "  mzf               %s\n", mzf_version () );
    printf ( "  generic_driver    %s\n", generic_driver_version () );
    printf ( "  endianity         %s\n", endianity_version () );
    printf ( "  sharpmz_ascii     %s\n", sharpmz_ascii_version () );
}

/**
 * @brief Naparsuje argumenty z `argv` do struktury `st_MZF_CAT_OPTIONS`.
 *
 * Při zachycení `--help`, `--version` nebo `--lib-versions` rovnou
 * vypíše příslušný text a vrátí 1 (signál pro `main`, aby ukončil
 * s `EXIT_SUCCESS`). Při chybě (neznámá volba, neplatná hodnota,
 * mutex porušení, žádný positional) vrátí -1. Při úspěšném naparsování
 * (bez early-exit) vrátí 0.
 *
 * Variadické vstupy: po skončení `getopt_long` smyčky se očekává
 * `optind < argc`. Všechny zbylé argumenty se zaregistrují jako
 * pozicní vstupy (`opts->input_paths`, `opts->input_count`). Volba
 * `-i/--input` v `long_options[]` cíleně chybí, getopt vyhodí
 * "unknown option" (defenzivní hard error - variadic positional je
 * standard pro `cat`-like nástroje).
 *
 * @param argc Standardní argc.
 * @param argv Standardní argv.
 * @param opts Výstupní struktura voleb (musí být předem inicializovaná).
 * @return 0 = pokračovat, 1 = early exit success, -1 = error.
 */
static int parse_args ( int argc, char *argv[], st_MZF_CAT_OPTIONS *opts ) {
    static const struct option long_options[] = {
        { "help",         no_argument,       NULL, 'h'  },
        { "version",      no_argument,       NULL, 'V'  },
        { "lib-versions", no_argument,       NULL, 1000 },
        { "type",         required_argument, NULL, 't'  },
        { "name",         required_argument, NULL, 'n'  },
        { "load-addr",    required_argument, NULL, 'l'  },
        { "exec-addr",    required_argument, NULL, 'e'  },
        { "comment",      required_argument, NULL, 'c'  },
        { "output",       required_argument, NULL, 'o'  },
        { "charset",      required_argument, NULL, 'C'  },
        { "upper",        no_argument,       NULL, 1001 },
        { "size",         required_argument, NULL, 's'  },
        { "align-block",  required_argument, NULL, 1002 },
        { "align-to",     required_argument, NULL, 1003 },
        { "pad-between",  required_argument, NULL, 1004 },
        { "filler",       required_argument, NULL, 1005 },
        { NULL, 0, NULL, 0 }
    };

    /* vlastní chybové hlášky, getopt ať mlčí */
    opterr = 0;

    int c;
    while ( ( c = getopt_long ( argc, argv, "+hVt:n:l:e:c:o:C:s:",
                                 long_options, NULL ) ) != -1 ) {
        switch ( c ) {
            case 'h':
                print_usage ( stdout );
                return 1;
            case 'V':
                print_version ();
                return 1;
            case 1000:
                print_lib_versions ();
                return 1;
            case 't':
                if ( argparse_ftype ( optarg, &opts->ftype ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --type value '%s'\n", optarg );
                    return -1;
                }
                opts->ftype_set = true;
                break;
            case 'n':
                opts->name = optarg;
                break;
            case 'l':
                if ( argparse_uint16 ( optarg, &opts->load_addr ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --load-addr value '%s'\n", optarg );
                    return -1;
                }
                opts->has_load = true;
                break;
            case 'e':
                if ( argparse_uint16 ( optarg, &opts->exec_addr ) != 0 ) {
                    fprintf ( stderr, "Error: invalid --exec-addr value '%s'\n", optarg );
                    return -1;
                }
                opts->has_exec = true;
                break;
            case 'c':
                opts->comment = optarg;
                break;
            case 'o':
                opts->output_path = optarg;
                break;
            case 'C': {
                en_TOOL_CHARSET cs = TOOL_CHARSET_EU;
                if ( argparse_charset ( optarg, &cs ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --charset value '%s' (expected: eu, jp, utf8-eu, utf8-jp, none)\n",
                              optarg );
                    return -1;
                }
                opts->charset = (int) cs;
                break;
            }
            case 1001:
                opts->upper = true;
                break;
            case 's':
                if ( parse_size_arg ( optarg, &opts->size, &opts->has_size ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --size value '%s' (expected: 'auto' or 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                break;
            case 1002:
                if ( parse_align_value ( optarg, &opts->align_block ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --align-block value '%s' (expected: 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                break;
            case 1003:
                if ( parse_align_value ( optarg, &opts->align_to ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --align-to value '%s' (expected: 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                opts->has_align_to = true;
                break;
            case 1004:
                if ( parse_align_value ( optarg, &opts->pad_between ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --pad-between value '%s' (expected: 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                break;
            case 1005:
                if ( argparse_byte ( optarg, &opts->filler ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --filler value '%s' (expected: 0..255 or 0x00..0xFF)\n",
                              optarg );
                    return -1;
                }
                opts->filler_set = true;
                break;
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

    /* Variadické pozicní vstupy: alespoň 1 povinný. */
    if ( optind >= argc ) {
        fprintf ( stderr, "Error: at least one input file is required\n" );
        return -1;
    }
    opts->input_paths = (const char **) ( argv + optind );
    opts->input_count = argc - optind;

    /* Cross-validace: vzájemná výlučnost size voleb. Detekce uvnitř
       switch case by neviděla pozdější opakované zadání protistrany. */
    if ( opts->align_block != 0 && opts->has_align_to ) {
        fprintf ( stderr, "Error: --align-block and --align-to are mutually exclusive\n" );
        return -1;
    }
    if ( opts->has_size && opts->align_block != 0 ) {
        fprintf ( stderr, "Error: --size and --align-block are mutually exclusive\n" );
        return -1;
    }
    if ( opts->has_size && opts->has_align_to ) {
        fprintf ( stderr, "Error: --size and --align-to are mutually exclusive\n" );
        return -1;
    }

    return 0;
}

/**
 * @brief Hlavní vstupní bod nástroje mzf-cat.
 *
 * Postup:
 *  1. Na MSYS2/Windows přepne stdin a stdout do binárního režimu.
 *  2. Inicializuje paměťový driver.
 *  3. Naparsuje CLI argumenty (default `--charset eu`).
 *  4. Postupně načte všechny pozicní vstupy do bufferu, mezi non-last
 *     inputs aplikuje pad-between (pokud zadán).
 *  5. Aplikuje finální zarovnání: `--align-block` nebo `--align-to`
 *     (alias `--size`) - mutex.
 *  6. Transformuje pole `name` a `comment` přes `transform_text_field`.
 *  7. Sestaví MZF hlavičku a zapíše MZF do souboru nebo na stdout.
 *
 * @param argc Počet argumentů.
 * @param argv Pole argumentů.
 * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při jakékoli chybě.
 */
int main ( int argc, char *argv[] ) {
#ifdef _WIN32
    /* Bez tohoto by stdin přerušil 0x1A a stdout by konvertoval LF -> CRLF. */
    setmode ( fileno ( stdin ),  _O_BINARY );
    setmode ( fileno ( stdout ), _O_BINARY );
#endif

    /* Inicializace globálních paměťových driverů (zabudované callbacky). */
    memory_driver_init ();

    st_MZF_CAT_OPTIONS opts = {
        .ftype          = MZF_FTYPE_OBJ,
        .ftype_set      = false,
        .name           = NULL,
        .comment        = NULL,
        .load_addr      = 0,
        .exec_addr      = 0,
        .has_load       = false,
        .has_exec       = false,
        .charset        = TOOL_CHARSET_EU,
        .upper          = false,
        .size           = 0,
        .has_size       = false,
        .align_block    = 0,
        .align_to       = 0,
        .has_align_to   = false,
        .pad_between    = 0,
        .filler         = 0x00,
        .filler_set     = false,
        .output_path    = NULL,
        .input_paths    = NULL,
        .input_count    = 0,
    };

    int pa = parse_args ( argc, argv, &opts );
    if ( pa < 0 ) {
        fprintf ( stderr, "Try 'mzf-cat --help' for more information.\n" );
        return EXIT_FAILURE;
    }
    if ( pa > 0 ) {
        return EXIT_SUCCESS;
    }

    /* default exec = load, pokud -e nebyl zadán */
    if ( !opts.has_exec ) {
        opts.exec_addr = opts.load_addr;
    }

    /* validace povinných voleb */
    if ( !opts.name ) {
        fprintf ( stderr, "Error: missing required option --name\n" );
        return EXIT_FAILURE;
    }
    if ( !opts.has_load ) {
        fprintf ( stderr, "Error: missing required option --load-addr\n" );
        return EXIT_FAILURE;
    }

    /* Stack buffer pro tělo - +1 pro detekci přetečení nad 65535.
       Na MSYS2/MinGW default 8MB stack, 64KB je únosné. */
    uint8_t total_buf[ MZF_MAX_BODY_SIZE + 1 ];
    size_t body_size = 0;
    uint8_t filler = opts.filler;

    /* Načtení N pozičních vstupů s případným pad-between mezi nimi. */
    for ( int i = 0; i < opts.input_count; i++ ) {
        size_t nread = 0;
        if ( read_file_to_buffer_at ( opts.input_paths[i],
                                       total_buf, body_size,
                                       sizeof ( total_buf ),
                                       &nread ) != 0 ) {
            return EXIT_FAILURE;
        }
        body_size += nread;

        /* defenzivní kontrola - read_file_to_buffer_at by to měl detekovat
           jako "exceeds maximum body size", ale pro jistotu */
        if ( body_size > MZF_MAX_BODY_SIZE ) {
            fprintf ( stderr,
                      "Error: combined body size %zu exceeds maximum %u bytes\n",
                      body_size, MZF_MAX_BODY_SIZE );
            return EXIT_FAILURE;
        }

        /* pad-between mezi non-last inputs */
        if ( i < opts.input_count - 1 && opts.pad_between > 0 ) {
            size_t modv = body_size % opts.pad_between;
            size_t pad = ( modv == 0 ) ? 0 : ( opts.pad_between - modv );
            if ( pad > 0 ) {
                if ( body_size + pad > MZF_MAX_BODY_SIZE ) {
                    fprintf ( stderr,
                              "Error: pad-between would push body to %zu bytes (max %u)\n",
                              body_size + pad, MZF_MAX_BODY_SIZE );
                    return EXIT_FAILURE;
                }
                memset ( total_buf + body_size, filler, pad );
                body_size += pad;
            }
        }
    }

    /* Aplikace finálního alignmentu / size na konkatenované tělo.
       Mapování --size na --align-to je shodné s bin2mzf. */
    if ( opts.align_block != 0 || opts.has_align_to || opts.has_size ) {
        uint32_t effective_align_to =
            opts.has_align_to ? opts.align_to
            : opts.has_size   ? (uint32_t) opts.size
                              : 0;
        bool effective_has_align_to = opts.has_align_to || opts.has_size;

        if ( apply_alignment ( total_buf, sizeof ( total_buf ), &body_size,
                                opts.align_block,
                                effective_align_to,
                                effective_has_align_to,
                                filler ) != 0 ) {
            return EXIT_FAILURE;
        }
    }

    /* finální kontrola velikosti těla */
    if ( body_size > MZF_MAX_BODY_SIZE ) {
        fprintf ( stderr, "Error: body too large (%zu bytes, max %u)\n",
                  body_size, MZF_MAX_BODY_SIZE );
        return EXIT_FAILURE;
    }

    /* Sestavení pole `fname.name[]` přes pipeline transform_text_field. */
    uint8_t name_buf[ MZF_FILE_NAME_LENGTH + 1 ];
    size_t  name_len = 0;
    if ( transform_text_field ( opts.name, opts.charset, opts.upper,
                                 TEXT_FIELD_NAME, MZF_FILE_NAME_LENGTH,
                                 name_buf, &name_len ) != 0 ) {
        return EXIT_FAILURE;
    }

    /* Transformace komentáře - stejná pipeline, warning + truncate
       místo hard erroru. cmnt_buf je 104 B, nullbyte-filled. */
    uint8_t cmnt_buf[ MZF_CMNT_LENGTH ];
    memset ( cmnt_buf, 0, sizeof ( cmnt_buf ) );
    size_t cmnt_len = 0;
    if ( opts.comment ) {
        if ( transform_text_field ( opts.comment, opts.charset, opts.upper,
                                     TEXT_FIELD_COMMENT, MZF_CMNT_LENGTH,
                                     cmnt_buf, &cmnt_len ) != 0 ) {
            return EXIT_FAILURE;
        }
    }

    /* fsize v hlavičce odpovídá skutečné délce těla po align (auto-size). */
    uint16_t fsize_for_header = (uint16_t) body_size;

    st_MZF_HEADER *hdr = mzf_tools_create_mzfhdr (
        opts.ftype,
        fsize_for_header,
        opts.load_addr,
        opts.exec_addr,
        name_buf,
        (unsigned) name_len,
        opts.comment ? cmnt_buf : NULL );
    if ( !hdr ) {
        fprintf ( stderr, "Error: cannot allocate MZF header\n" );
        return EXIT_FAILURE;
    }

    st_MZF mzf = {
        .header    = *hdr,
        .body      = total_buf,
        .body_size = (uint32_t) body_size,
    };

    int rc;
    if ( opts.output_path ) {
        rc = write_mzf_to_file ( opts.output_path, &mzf );
    } else {
        rc = write_mzf_to_stdout ( &mzf );
    }

    free ( hdr );

    return ( rc == 0 ) ? EXIT_SUCCESS : EXIT_FAILURE;
}
