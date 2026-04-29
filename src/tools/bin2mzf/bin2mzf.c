/**
 * @file   bin2mzf.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  Primární CLI nástroj rodiny bin2mzf - vytváří MZF z binárního vstupu.
 *
 * Vezme binární data (ze souboru, stdin nebo positional argumentu),
 * sestaví 128 bajtů MZF hlavičky podle CLI voleb a zapíše výsledný
 * MZF soubor (hlavička + tělo) do souboru nebo na stdout.
 *
 * Verze 0.2.0 scope:
 *  - argumenty: -t/--type, -n/--name, -l/--load-addr, -e/--exec-addr,
 *    -c/--comment, -o/--output, -i/--input, --version, --lib-versions, --help
 *  - -C/--charset (eu|jp|utf8-eu|utf8-jp|none), default `eu`
 *    (změna defaultu oproti v0.1.0, kde se chovalo jako `none`).
 *  - --upper - aplikuje uppercase a-z -> A-Z PŘED konverzí znakové
 *    sady, default off. Platí stejně pro --name i --comment.
 *  - C-style escape sekvence v -n a -c (`\\`, `\n`, `\r`, `\t`,
 *    `\xNN`). Pokud vstup obsahuje escape sekvence, --charset i --upper
 *    se pro dané pole ignorují (chovají se jako `--charset none`).
 *  - **NOVÉ**: -s/--size N|auto - vynutí přesnou velikost těla (alias
 *    pro --align-to N pro numerickou hodnotu). 'auto' = default.
 *  - **NOVÉ**: --align-block N - zarovná tělo na nejbližší vyšší násobek N.
 *  - **NOVÉ**: --align-to N - vynutí přesnou velikost N (truncate nebo pad).
 *  - **NOVÉ**: --filler BYTE - padding bajt pro align/size (default 0x00).
 *  - **NOVÉ**: --header-only - vyrobí jen 128B hlavičku, vstup ignoruje.
 *  - **NOVÉ**: --auto-size (default) / --no-auto-size - řídí, zda fsize
 *    v hlavičce odpovídá skutečné délce těla nebo zůstává podle --size.
 *  - **NOVÉ**: --cpm - CP/M preset (SOKODI CMT.COM): ftype=0x22,
 *    fstrt=fexec=0x0100, charset=none. Vzájemně výlučné s --type,
 *    --load-addr, --exec-addr, --charset, --name.
 *  - **NOVÉ**: --cpm-name NAME (max 8 ASCII znaků, silent uppercase
 *    + space-pad, vyžaduje --cpm).
 *  - **NOVÉ**: --cpm-ext EXT (max 3 ASCII znaky, default "COM",
 *    vyžaduje --cpm).
 *  - **NOVÉ**: --cpm-attr LIST - kombinace `ro`, `sys`, `arc`
 *    (case-insensitive, comma/space/concatenated). Atributy se
 *    kódují jako bit 7 příslušného bajtu přípony ve fname.
 *    `name[9]` = R/O, `name[10]` = SYS, `name[11]` = ARC.
 *  - fsize se odvozuje automaticky z velikosti vstupu (pokud není
 *    --no-auto-size + --size)
 *  - default ftype = 0x01 (OBJ), default exec-addr = load-addr
 *
 * Poznámka k CP/M layoutu pole `fname.name[]`:
 *  - `[0..7]`  = jméno (uppercase, padding mezerami)
 *  - `[8]`     = '.' (0x2E)
 *  - `[9..11]` = přípona (uppercase, padding mezerami) + bit 7 = atribut
 *  - `[12]`    = 0x0D (vnitřní CR)
 *  - `[13..15]` = 0x0D 0x0D 0x0D z `memset` v `mzf_tools_create_mzfhdr`.
 *    Mzdisk encoder zde dává 0x00 (z úvodního memset celé hlavičky);
 *    `bin2mzf` zde má 0x0D. Sémanticky shodné - decoder mzdisku tyto
 *    pozice ignoruje.
 *
 * Mimo scope (řeší další tasky M3 / v0.3.0):
 *  - --check-overflow (low priority)
 *  - --format json/csv (patří k mzf-info, ne producent)
 *  - --cmnt-bin (binární komentář, low priority)
 *  - --charset-comment, --no-upper-comment (low priority - drží se
 *    jednotné chování pro name i comment)
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
#include "tools/common/mzf_io.h"
#include "tools/common/bin2mzf_cli_version.h"
#include "tools/common/argparse_helpers.h"
#include "tools/common/text_field.h"
#include "tools/common/cpm_constants.h"
#include "tools/common/cpm_helpers.h"

/** @brief Verze nástroje bin2mzf. */
#define BIN2MZF_VERSION "0.2.0"

/**
 * @brief Volby CLI parsované z argumentů.
 *
 * Drží referenční ukazatele do `argv` (žádná vlastní alokace).
 * Životnost těchto polí je tedy svázaná s životností procesu, což
 * je pro CLI dostatečné. Pole `has_load`/`has_exec` slouží k detekci,
 * zda uživatel hodnotu zadal explicitně.
 *
 * Pole `charset` a `upper` ovlivňují jak `name`, tak `comment` stejně.
 * Pokud konkrétní pole obsahuje C-style escape sekvenci (`\\` v textu),
 * `transform_text_field()` pro ně automaticky vynutí `TOOL_CHARSET_NONE`
 * a vypne uppercase, ale jen pro to dané pole (per-pole granularita).
 */
typedef struct st_BIN2MZF_OPTIONS {
    uint8_t ftype;              /**< typ souboru, default MZF_FTYPE_OBJ */
    bool ftype_set;             /**< true pokud uživatel zadal -t/--type explicitně (pro mutex `--cpm`) */
    const char *name;           /**< jméno souboru (povinné, max 16 znaků po konverzi) */
    const char *comment;        /**< komentář (volitelný, ořezává se na 104 B po konverzi) */
    uint16_t load_addr;         /**< startovací adresa, povinná */
    uint16_t exec_addr;         /**< spouštěcí adresa, default = load_addr */
    const char *input_path;     /**< vstupní soubor, NULL = stdin */
    const char *output_path;    /**< výstupní soubor, NULL = stdout */
    bool has_load;              /**< true pokud byl -l/--load-addr zadán */
    bool has_exec;              /**< true pokud byl -e/--exec-addr zadán */
    int charset;                /**< znaková sada pro name/comment, viz en_TOOL_CHARSET (default EU) */
    bool charset_set;           /**< true pokud uživatel zadal -C/--charset explicitně (pro mutex `--cpm`) */
    bool upper;                 /**< true = uppercase a-z -> A-Z před konverzí (default false) */
    uint16_t size;              /**< hodnota --size N (validní jen pokud has_size = true) */
    /* CP/M sub-options - viz `tools/common/cpm_constants.h` pro layout konstanty. */
    bool has_size;              /**< true pokud uživatel zadal --size N (numerické); --size auto -> false */
    uint32_t align_block;       /**< --align-block N v rozsahu 1..MZF_MAX_BODY_SIZE; 0 = nepoužito */
    uint32_t align_to;          /**< --align-to N v rozsahu 1..MZF_MAX_BODY_SIZE; validní jen pokud has_align_to */
    bool has_align_to;          /**< true pokud uživatel zadal --align-to (rozlišení od align_to=0) */
    uint8_t filler;             /**< padding bajt pro --align-* / --size (default 0x00) */
    bool filler_set;            /**< true pokud uživatel zadal --filler explicitně (pro warning) */
    bool header_only;           /**< true = vyrobit jen 128B hlavičku, vstup ignorovat */
    bool auto_size;             /**< true (default) = fsize = délka těla po align/truncate; --no-auto-size -> false */
    bool cpm;                   /**< --cpm aktivní = CP/M preset (ftype 0x22, fstrt/fexec 0x0100, charset NONE) */
    const char *cpm_name;       /**< --cpm-name (max 8 ASCII znaků; uppercase + space-pad uvnitř `mzf_cpm_build_fname`) */
    const char *cpm_ext;        /**< --cpm-ext (max 3 ASCII znaky); NULL = default `MZF_CPM_DEFAULT_EXT` */
    uint8_t cpm_attr_mask;      /**< bitmaska atributů `MZF_CPM_ATTR_*` (default 0 = žádné atributy) */
} st_BIN2MZF_OPTIONS;

/**
 * @brief Vypíše Usage / nápovědu nástroje bin2mzf.
 *
 * Text je v angličtině (locales-ready). Volá se z `--help` (na stdout)
 * i z chybových cest (na stderr).
 *
 * @param out Cílový stream (stdout / stderr).
 */
static void print_usage ( FILE *out ) {
    fprintf ( out,
        "Usage: bin2mzf -n NAME -l LOAD_ADDR [options] [INPUT]\n"
        "\n"
        "Build an MZF file (Sharp MZ tape format) from a binary input.\n"
        "\n"
        "Required options:\n"
        "  -n, --name NAME            File name written into MZF header (max 16 chars).\n"
        "  -l, --load-addr ADDR       Z80 load address (e.g. 0x1200 or 4608).\n"
        "\n"
        "Optional options:\n"
        "  -t, --type TYPE            File type: 0x01..0xff or symbolic\n"
        "                             (obj, btx, bsd, brd, rb). Default: 0x01 (obj).\n"
        "  -e, --exec-addr ADDR       Z80 execution address. Default: equal to load-addr.\n"
        "  -c, --comment TEXT         Comment text (max 104 chars, longer is truncated).\n"
        "  -i, --input FILE           Input binary file (alternative to positional arg).\n"
        "  -o, --output FILE          Output MZF file. Default: write to stdout.\n"
        "  -C, --charset MODE         Charset for --name and --comment: eu (default),\n"
        "                             jp, utf8-eu, utf8-jp, none.\n"
        "      --upper                Uppercase a-z -> A-Z before charset conversion.\n"
        "  -s, --size N|auto          Body size: numeric (1..65535) or 'auto' (default).\n"
        "                             Alias for --align-to N. With --no-auto-size,\n"
        "                             fsize header field is set from this value.\n"
        "      --align-block N        Pad body to next multiple of N bytes (1..65535).\n"
        "      --align-to N           Force body to exactly N bytes (truncate or pad).\n"
        "      --filler BYTE          Padding byte for --align-* / --size (default 0x00).\n"
        "      --header-only          Emit only 128B header, ignore input data.\n"
        "      --auto-size            Set fsize = actual body length (default).\n"
        "      --no-auto-size         Keep fsize from --size; do not auto-update.\n"
        "      --version              Print tool version and exit.\n"
        "      --lib-versions         Print library versions and exit.\n"
        "  -h, --help                 Print this help and exit.\n"
        "\n"
        "If neither --input nor a positional argument is given, the binary is read\n"
        "from stdin. If --output is omitted, the MZF is written to stdout.\n"
        "Numeric values accept decimal or 0x-prefixed hexadecimal notation.\n"
        "\n"
        "Note: --align-block, --align-to and --size are mutually exclusive.\n"
        "\n"
        "CP/M mode (SOKODI CMT.COM convention):\n"
        "      --cpm                  Enable CP/M mode (preset: ftype=0x22,\n"
        "                             load=exec=0x0100, charset=none).\n"
        "      --cpm-name NAME        CP/M filename (max 8 chars, ASCII, uppercased).\n"
        "      --cpm-ext EXT          CP/M extension (max 3 chars, default: COM).\n"
        "      --cpm-attr LIST        CP/M attributes: ro, sys, arc (any combination,\n"
        "                             comma/space/concatenated, e.g. 'ro,sys' or 'roSys').\n"
        "      Note: --cpm is incompatible with --type, --load-addr, --exec-addr,\n"
        "      --charset, and --name. The --comment option remains available.\n"
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
 * Formát: `bin2mzf <tool> (<release-name> <release-version>)`.
 */
static void print_version ( void ) {
    printf ( "bin2mzf %s (%s %s)\n",
             BIN2MZF_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
}

/**
 * @brief Vypíše verze všech linkovaných knihoven.
 *
 * Slouží k diagnostice ABI mismatch a pro reportování v bug reports.
 */
static void print_lib_versions ( void ) {
    printf ( "bin2mzf %s (%s %s)\n\n",
             BIN2MZF_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
    printf ( "Library versions:\n" );
    printf ( "  mzf               %s\n", mzf_version () );
    printf ( "  generic_driver    %s\n", generic_driver_version () );
    printf ( "  endianity         %s\n", endianity_version () );
    printf ( "  sharpmz_ascii     %s\n", sharpmz_ascii_version () );
}

/**
 * @brief Načte celý stdin do předem alokovaného bufferu.
 *
 * Funguje na principu chunked `fread` smyčky. Pokud stdin obsahuje více
 * než `buf_cap` bajtů, vrátí chybu (po pokusu o přečtení dalšího bajtu).
 *
 * @param buf Cílový buffer.
 * @param buf_cap Kapacita bufferu v bajtech.
 * @param out_size Výstup: počet skutečně načtených bajtů.
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
    /* detekce přetečení */
    if ( total == buf_cap ) {
        uint8_t probe;
        if ( fread ( &probe, 1, 1, stdin ) == 1 ) {
            fprintf ( stderr, "Error: stdin input is larger than %u bytes (MZF body limit)\n",
                      MZF_MAX_BODY_SIZE );
            return -1;
        }
    }
    *out_size = total;
    return 0;
}

/**
 * @brief Naparsuje argumenty z `argv` do struktury `st_BIN2MZF_OPTIONS`.
 *
 * Při zachycení `--help`, `--version` nebo `--lib-versions` rovnou
 * vypíše příslušný text a vrátí 1 (signál pro `main`, aby ukončil
 * s `EXIT_SUCCESS`). Při chybě (neznámá volba, neplatná hodnota,
 * konflikt -i a positional) vrátí -1. Při úspěšném naparsování (bez
 * early-exit) vrátí 0.
 *
 * @param argc Standardní argc.
 * @param argv Standardní argv.
 * @param opts Výstupní struktura voleb (musí být předem inicializovaná).
 * @return 0 = pokračovat, 1 = early exit success, -1 = error.
 */
static int parse_args ( int argc, char *argv[], st_BIN2MZF_OPTIONS *opts ) {
    static const struct option long_options[] = {
        { "type",         required_argument, NULL, 't' },
        { "name",         required_argument, NULL, 'n' },
        { "load-addr",    required_argument, NULL, 'l' },
        { "exec-addr",    required_argument, NULL, 'e' },
        { "comment",      required_argument, NULL, 'c' },
        { "output",       required_argument, NULL, 'o' },
        { "input",        required_argument, NULL, 'i' },
        { "charset",      required_argument, NULL, 'C' },
        { "upper",        no_argument,       NULL, 1000 },
        { "size",         required_argument, NULL, 's'  },
        { "align-block",  required_argument, NULL, 1001 },
        { "align-to",     required_argument, NULL, 1002 },
        { "filler",       required_argument, NULL, 1003 },
        { "header-only",  no_argument,       NULL, 1004 },
        { "auto-size",    no_argument,       NULL, 1005 },
        { "no-auto-size", no_argument,       NULL, 1006 },
        { "cpm",          no_argument,       NULL, 1007 },
        { "cpm-name",     required_argument, NULL, 1008 },
        { "cpm-ext",      required_argument, NULL, 1009 },
        { "cpm-attr",     required_argument, NULL, 1010 },
        { "version",      no_argument,       NULL, 'V' },
        { "lib-versions", no_argument,       NULL, 'L' },
        { "help",         no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* vlastní chybové hlášky, getopt ať mlčí */
    opterr = 0;

    int c;
    while ( ( c = getopt_long ( argc, argv, "+t:n:l:e:c:o:i:s:C:VLh", long_options, NULL ) ) != -1 ) {
        switch ( c ) {
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
            case 'i':
                opts->input_path = optarg;
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
                opts->charset_set = true;
                break;
            }
            case 1000:
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
            case 1001:
                if ( parse_align_value ( optarg, &opts->align_block ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --align-block value '%s' (expected: 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                break;
            case 1002:
                if ( parse_align_value ( optarg, &opts->align_to ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --align-to value '%s' (expected: 1..%u)\n",
                              optarg, MZF_MAX_BODY_SIZE );
                    return -1;
                }
                opts->has_align_to = true;
                break;
            case 1003:
                if ( argparse_byte ( optarg, &opts->filler ) != 0 ) {
                    fprintf ( stderr,
                              "Error: invalid --filler value '%s' (expected: 0..255 or 0x00..0xFF)\n",
                              optarg );
                    return -1;
                }
                opts->filler_set = true;
                break;
            case 1004:
                opts->header_only = true;
                break;
            case 1005:
                opts->auto_size = true;
                break;
            case 1006:
                opts->auto_size = false;
                break;
            case 1007:
                opts->cpm = true;
                break;
            case 1008:
                opts->cpm_name = optarg;
                break;
            case 1009:
                opts->cpm_ext = optarg;
                break;
            case 1010:
                if ( mzf_cpm_parse_attr ( optarg, &opts->cpm_attr_mask ) != 0 ) {
                    return -1;
                }
                break;
            case 'V':
                print_version ();
                return 1;
            case 'L':
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

    /* Positional argument - vstupní soubor (alternativa k -i) */
    if ( optind < argc ) {
        if ( opts->input_path ) {
            fprintf ( stderr, "Error: cannot combine --input with positional input file\n" );
            return -1;
        }
        opts->input_path = argv[optind];
        optind++;
        if ( optind < argc ) {
            fprintf ( stderr, "Error: too many positional arguments\n" );
            return -1;
        }
    }

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

    /* Defenzivní warningy pro --header-only kombinace (no-op, ne error). */
    if ( opts->header_only && opts->input_path != NULL ) {
        fprintf ( stderr, "Warning: input file ignored due to --header-only\n" );
    }
    if ( opts->header_only &&
         ( opts->align_block != 0 || opts->has_align_to || opts->has_size ) ) {
        fprintf ( stderr,
                  "Warning: --align-* / --size ignored due to --header-only\n" );
    }
    if ( opts->header_only && opts->filler_set ) {
        fprintf ( stderr, "Warning: --filler ignored due to --header-only\n" );
    }

    /* --no-auto-size bez --size N je no-op - žádný zdroj pro fixní fsize. */
    if ( !opts->auto_size && !opts->has_size ) {
        fprintf ( stderr,
                  "Warning: --no-auto-size without --size has no effect; falling back to auto-size\n" );
        opts->auto_size = true;
    }

    /* --filler bez align/size voleb (a mimo --header-only) je no-op. */
    if ( opts->filler_set && !opts->header_only &&
         opts->align_block == 0 && !opts->has_align_to && !opts->has_size ) {
        fprintf ( stderr,
                  "Warning: --filler has no effect without --align-block / --align-to / --size\n" );
    }

    /* Cross-validace CP/M voleb: hard error pro neslučitelné kombinace.
       `--cpm` přebírá kontrolu nad ftype, fstrt/fexec, charsetem a polem
       fname; explicitní `--type/--load-addr/--exec-addr/--charset/--name`
       by skrytě konfliktovaly s presetem - lepší je tvrdá chyba s jasným
       hláškou než tichý override. */
    if ( opts->cpm ) {
        if ( opts->ftype_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --type\n" );
            return -1;
        }
        if ( opts->has_load ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --load-addr\n" );
            return -1;
        }
        if ( opts->has_exec ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --exec-addr\n" );
            return -1;
        }
        if ( opts->charset_set ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --charset\n" );
            return -1;
        }
        if ( opts->name != NULL ) {
            fprintf ( stderr, "Error: cannot combine --cpm with --name\n" );
            return -1;
        }
    } else {
        /* CP/M sub-options bez --cpm jsou samy o sobě bez efektu - hlásíme
           tvrdě, abychom uživatele upozornili na opomenuté --cpm. */
        if ( opts->cpm_name != NULL ) {
            fprintf ( stderr, "Error: --cpm-name requires --cpm\n" );
            return -1;
        }
        if ( opts->cpm_ext != NULL ) {
            fprintf ( stderr, "Error: --cpm-ext requires --cpm\n" );
            return -1;
        }
        if ( opts->cpm_attr_mask != 0 ) {
            fprintf ( stderr, "Error: --cpm-attr requires --cpm\n" );
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Hlavní vstupní bod nástroje bin2mzf.
 *
 * Postup:
 *  1. Na MSYS2/Windows přepne stdin a stdout do binárního režimu.
 *  2. Inicializuje paměťový driver.
 *  3. Naparsuje CLI argumenty (default `--charset eu`).
 *  4. Načte vstup do paměťového bufferu.
 *  5. Transformuje pole `name` a `comment` přes `transform_text_field`
 *     (escape detect -> uppercase -> charset convert -> validate).
 *  6. Sestaví MZF hlavičku přes `mzf_tools_create_mzfhdr` s pre-konvertovanými
 *     bajty (volání `mzf_tools_set_fname` se vědomě nepoužívá - vždy by
 *     vynutilo Sharp MZ EU konverzi a znemožnilo režimy `jp` / `none`).
 *  7. Zapíše MZF do souboru nebo na stdout.
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

    st_BIN2MZF_OPTIONS opts = {
        .ftype          = MZF_FTYPE_OBJ,
        .ftype_set      = false,
        .name           = NULL,
        .comment        = NULL,
        .load_addr      = 0,
        .exec_addr      = 0,
        .input_path     = NULL,
        .output_path    = NULL,
        .has_load       = false,
        .has_exec       = false,
        .charset        = TOOL_CHARSET_EU,
        .charset_set    = false,
        .upper          = false,
        .size           = 0,
        .has_size       = false,
        .align_block    = 0,
        .align_to       = 0,
        .has_align_to   = false,
        .filler         = 0x00,
        .filler_set     = false,
        .header_only    = false,
        .auto_size      = true,
        .cpm            = false,
        .cpm_name       = NULL,
        .cpm_ext        = NULL,
        .cpm_attr_mask  = 0,
    };

    int pa = parse_args ( argc, argv, &opts );
    if ( pa < 0 ) {
        fprintf ( stderr, "Try 'bin2mzf --help' for more information.\n" );
        return EXIT_FAILURE;
    }
    if ( pa > 0 ) {
        return EXIT_SUCCESS;
    }

    /* CP/M preset: po `parse_args` nastavíme tvrdé defaulty, které
       vzájemnou výlučnost (`--type/--load-addr/--exec-addr/--charset/
       --name`) zaručila cross-validace. Po tomto bloku se zbytek
       pipeline (validace `--load-addr`, body čtení, alignment) chová
       beze změny - jen se sestavení jména provede přes `mzf_cpm_build_fname`
       místo `transform_text_field`. */
    if ( opts.cpm ) {
        opts.ftype     = MZF_CPM_FTYPE;
        opts.load_addr = MZF_CPM_DEFAULT_ADDR;
        opts.exec_addr = MZF_CPM_DEFAULT_ADDR;
        opts.has_load  = true;   /* preset poskytuje load-addr - obejde validaci níže */
        opts.has_exec  = true;
        opts.charset   = TOOL_CHARSET_NONE;
        if ( opts.cpm_ext == NULL ) {
            opts.cpm_ext = MZF_CPM_DEFAULT_EXT;
        }
        if ( opts.cpm_name == NULL ) {
            fprintf ( stderr, "Error: --cpm requires --cpm-name\n" );
            return EXIT_FAILURE;
        }
    }

    /* default exec = load, pokud -e nebyl zadán */
    if ( !opts.has_exec ) {
        opts.exec_addr = opts.load_addr;
    }

    /* validace povinných voleb (v CP/M režimu jsou splněné z presetu) */
    if ( !opts.cpm && !opts.name ) {
        fprintf ( stderr, "Error: missing required option --name\n" );
        return EXIT_FAILURE;
    }
    if ( !opts.has_load ) {
        fprintf ( stderr, "Error: missing required option --load-addr\n" );
        return EXIT_FAILURE;
    }

    /* alokace bufferu pro tělo - +1 pro detekci přetečení nad 65535.
       Pro --header-only nealokujeme - žádný read se neprovádí, body=NULL. */
    size_t buf_cap = (size_t) MZF_MAX_BODY_SIZE + 1;
    uint8_t *body_buf = NULL;
    size_t body_size = 0;

    if ( !opts.header_only ) {
        body_buf = malloc ( buf_cap );
        if ( !body_buf ) {
            fprintf ( stderr, "Error: cannot allocate %zu bytes for body buffer\n", buf_cap );
            return EXIT_FAILURE;
        }

        if ( opts.input_path ) {
            if ( read_file_to_buffer_at ( opts.input_path, body_buf, 0, buf_cap, &body_size ) != 0 ) {
                free ( body_buf );
                return EXIT_FAILURE;
            }
        } else {
            if ( read_stdin_to_buffer ( body_buf, buf_cap, &body_size ) != 0 ) {
                free ( body_buf );
                return EXIT_FAILURE;
            }
        }

        if ( body_size > MZF_MAX_BODY_SIZE ) {
            fprintf ( stderr, "Error: body too large (%zu bytes, max %u)\n",
                      body_size, MZF_MAX_BODY_SIZE );
            free ( body_buf );
            return EXIT_FAILURE;
        }

        /* Aplikace align/size na již načtené tělo. Pro --header-only se
           tato větev přeskočí (tělo neexistuje). */
        if ( opts.align_block != 0 || opts.has_align_to || opts.has_size ) {
            uint32_t effective_align_to =
                opts.has_align_to ? opts.align_to
                : opts.has_size   ? (uint32_t) opts.size
                                  : 0;
            bool effective_has_align_to = opts.has_align_to || opts.has_size;

            if ( apply_alignment ( body_buf, buf_cap, &body_size,
                                   opts.align_block,
                                   effective_align_to,
                                   effective_has_align_to,
                                   opts.filler ) != 0 ) {
                free ( body_buf );
                return EXIT_FAILURE;
            }
        }
    }

    /* Sestavení pole `fname.name[]`: ve standardním režimu prochází
       `transform_text_field` (escape detect -> uppercase -> charset
       convert -> validate). V CP/M režimu se obchází `transform_text_field`
       a sestaví se SOKODI 8.3+CR layout přes `mzf_cpm_build_fname`. Komentář
       (`cmnt`) prochází `transform_text_field` v obou režimech (v CP/M
       s `charset=NONE` z presetu - raw bajty). */
    uint8_t name_buf[MZF_FILE_NAME_LENGTH + 1];
    size_t  name_len = 0;
    if ( opts.cpm ) {
        uint8_t cpm_fname_buf[MZF_CPM_FNAME_LEN];
        if ( mzf_cpm_build_fname ( opts.cpm_name, opts.cpm_ext,
                                opts.cpm_attr_mask, cpm_fname_buf ) != 0 ) {
            free ( body_buf );
            return EXIT_FAILURE;
        }
        memcpy ( name_buf, cpm_fname_buf, MZF_CPM_FNAME_LEN );
        name_len = MZF_CPM_FNAME_LEN;
    } else {
        if ( transform_text_field ( opts.name, opts.charset, opts.upper,
                                    TEXT_FIELD_NAME, MZF_FILE_NAME_LENGTH,
                                    name_buf, &name_len ) != 0 ) {
            free ( body_buf );
            return EXIT_FAILURE;
        }
    }

    /* transformace komentáře: stejná pipeline, jen s warning + truncate
       místo hard erroru při přetečení. cmnt_buf je 104 B, beze null
       terminátoru (komentář je binární pole MZF hlavičky). */
    uint8_t cmnt_buf[MZF_CMNT_LENGTH];
    memset ( cmnt_buf, 0, sizeof ( cmnt_buf ) );
    size_t cmnt_len = 0;
    if ( opts.comment ) {
        if ( transform_text_field ( opts.comment, opts.charset, opts.upper,
                                    TEXT_FIELD_COMMENT, MZF_CMNT_LENGTH,
                                    cmnt_buf, &cmnt_len ) != 0 ) {
            free ( body_buf );
            return EXIT_FAILURE;
        }
    }

    /* Výpočet hodnoty pole fsize v hlavičce. Liší se od skutečné body_size
       jen v případě --header-only + --no-auto-size + --size N (uživatel
       chce tvrdě nastavit fsize i bez fyzických dat). Pro běžný flow
       s tělem auto-size = true vede na fsize == body_size. */
    uint16_t fsize_for_header;
    if ( opts.header_only ) {
        fsize_for_header = ( !opts.auto_size && opts.has_size ) ? opts.size : 0;
    } else if ( opts.auto_size ) {
        fsize_for_header = (uint16_t) body_size;
    } else {
        /* --no-auto-size: opts.has_size garantováno true (jinak fallback
           v parse_args už auto_size vrátil na true) */
        fsize_for_header = opts.has_size ? opts.size : (uint16_t) body_size;
    }

    /* vytvoření hlavičky - předáváme pre-konvertované bajty jména */
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
        free ( body_buf );
        return EXIT_FAILURE;
    }

    st_MZF mzf = {
        .header    = *hdr,
        .body      = body_buf,
        .body_size = (uint32_t) body_size,
    };

    int rc;
    if ( opts.output_path ) {
        rc = write_mzf_to_file ( opts.output_path, &mzf );
    } else {
        rc = write_mzf_to_stdout ( &mzf );
    }

    free ( hdr );
    free ( body_buf );

    return ( rc == 0 ) ? EXIT_SUCCESS : EXIT_FAILURE;
}
