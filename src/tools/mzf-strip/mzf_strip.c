/**
 * @file   mzf_strip.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  CLI nástroj rodiny bin2mzf - extrakce těla z MZF (inverze bin2mzf).
 *
 * Načte MZF soubor (z file argumentu nebo stdin), validuje velikost vůči
 * poli `fsize` v hlavičce a zapíše tělo (raw bajty z offsetu 128 do
 * 128 + fsize) na stdout nebo do souboru zadaného přes `--output`.
 *
 * Verze 0.1.0 (MVP) scope:
 *  - file vstup (poziční argument) i stdin (default).
 *  - file výstup (`-o, --output FILE`) i stdout (default).
 *  - strict velikostní kontrola: `n != 128 + fsize` -> error + exit 1
 *    (truncated i oversized vstup je odmítnut).
 *  - speciální případ `fsize == 0`: prázdný výstup, rc=0 (analogie
 *    k `cat /dev/null > out`).
 *  - `--version`, `--lib-versions`, `--help`.
 *
 * Mimo scope tohoto MVP:
 *  - **Žádné transformace dat** - výstup je 1:1 raw bajty z těla.
 *  - **Žádný JSON/CSV výstup** - tělo je binární data.
 *  - **Žádná validace polí `cmnt`, `fname`** - jen `fsize` musí odpovídat.
 *  - **Žádný `--lenient` flag** pro truncated body (low priority,
 *    samostatný task).
 *  - **Žádná TTY stdin detekce** přes `isatty(0)` (UX nadstavba,
 *    standardní Unix utility se tak nechovají).
 *  - **Bez `mzf_load`** - nástroj přistupuje k bufferu přímo (čte
 *    `fsize` z bajtů 0x12..0x13 v LE, tělo z `buf+128`). To je
 *    jednodušší, rychlejší a umožňuje vlastní strict size detekci
 *    bez workaroundu kolem knihovní `MZF_ERROR_IO` u truncated body.
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
#include "libs/generic_driver/generic_driver.h"
#include "libs/endianity/endianity.h"
#include "libs/sharpmz_ascii/sharpmz_ascii.h"
#include "tools/common/bin2mzf_cli_version.h"

/** @brief Verze nástroje mzf-strip. */
#define MZF_STRIP_VERSION "0.1.0"

/**
 * @brief Kapacita vstupního bufferu pro načtení MZF.
 *
 * Hodnota = `MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE + 1` (= 65664). Extra
 * bajt slouží k detekci přetečení - pokud načtení vrátí přesně tuto
 * hodnotu, vstup byl větší než maximální možný MZF a hlásíme chybu.
 */
#define MZF_STRIP_INPUT_BUF_CAP ( (size_t) MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE + 1 )

/**
 * @brief Volby CLI parsované z argumentů `mzf-strip`.
 *
 * Drží referenční ukazatele do `argv` (žádná vlastní alokace), životnost
 * vázaná na životnost procesu.
 */
typedef struct st_MZF_STRIP_OPTIONS {
    const char *input_path;     /**< vstupní soubor; NULL = stdin */
    const char *output_path;    /**< výstupní soubor; NULL = stdout */
    bool use_stdin;             /**< true pokud nebyl pozičně zadán argument - čte se stdin */
} st_MZF_STRIP_OPTIONS;

/**
 * @brief Vypíše Usage / nápovědu nástroje mzf-strip.
 *
 * Text je v angličtině (locales-ready). Volá se z `--help` (na stdout)
 * a z chybových cest (na stderr).
 *
 * @param out Cílový stream (stdout / stderr).
 */
static void print_usage ( FILE *out ) {
    fprintf ( out,
        "Usage: mzf-strip [options] [INPUT.mzf]\n"
        "\n"
        "Extract the body bytes from an MZF file (inverse of bin2mzf).\n"
        "Reads INPUT.mzf (or stdin if INPUT is omitted), validates the\n"
        "input size against the fsize field in the MZF header, then writes\n"
        "the raw body bytes (offset 128 .. 128+fsize) to the output.\n"
        "\n"
        "The input size must match exactly 128 + fsize bytes. Truncated or\n"
        "oversized inputs are rejected with an error.\n"
        "\n"
        "Options:\n"
        "  -o, --output FILE          Write body to FILE (default: stdout).\n"
        "      --version              Print tool version and exit.\n"
        "      --lib-versions         Print library versions and exit.\n"
        "  -h, --help                 Print this help and exit.\n"
        "\n"
        "Examples:\n"
        "  mzf-strip foo.mzf -o foo.bin\n"
        "  mzf-strip < foo.mzf -o foo.bin\n"
        "  mzf-strip foo.mzf > foo.bin\n"
        "  cat foo.mzf | mzf-strip > foo.bin\n"
        "\n"
        "Exit status:\n"
        "  0  body extracted successfully (or empty body for fsize == 0).\n"
        "  1  input is invalid (size mismatch, missing header, I/O error).\n"
    );
}

/**
 * @brief Vypíše verzi nástroje a verzi celé rodiny CLI.
 *
 * Formát: `mzf-strip <tool> (<release-name> <release-version>)`.
 */
static void print_version ( void ) {
    printf ( "mzf-strip %s (%s %s)\n",
             MZF_STRIP_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
}

/**
 * @brief Vypíše verze všech linkovaných knihoven.
 *
 * Slouží k diagnostice ABI mismatch a pro reportování v bug reports.
 */
static void print_lib_versions ( void ) {
    printf ( "mzf-strip %s (%s %s)\n\n",
             MZF_STRIP_VERSION,
             BIN2MZF_CLI_RELEASE_NAME,
             BIN2MZF_CLI_RELEASE_VERSION );
    printf ( "Library versions:\n" );
    printf ( "  mzf               %s\n", mzf_version () );
    printf ( "  generic_driver    %s\n", generic_driver_version () );
    printf ( "  endianity         %s\n", endianity_version () );
    printf ( "  sharpmz_ascii     %s\n", sharpmz_ascii_version () );
}

/**
 * @brief Naparsuje argumenty z `argv` do struktury `st_MZF_STRIP_OPTIONS`.
 *
 * Podporuje krátké volby `-h`, `-o` a sadu dlouhých voleb (`--output`,
 * `--version`, `--lib-versions`, `--help`). Při zachycení `--help`,
 * `--version` nebo `--lib-versions` rovnou vypíše příslušný text a
 * vrátí 1.
 *
 * Pozičně přijímá 0 nebo 1 argument (vstupní soubor). Více pozičních
 * argumentů vrátí chybu. Při 0 pozičních argumentech se nastavuje
 * `use_stdin = true`.
 *
 * @param argc Standardní argc.
 * @param argv Standardní argv.
 * @param opts Výstupní struktura voleb (musí být předem inicializovaná).
 * @return 0 = pokračovat, 1 = early exit success, -1 = error.
 */
static int parse_args ( int argc, char *argv[], st_MZF_STRIP_OPTIONS *opts ) {
    static const struct option long_options[] = {
        { "output",       required_argument, NULL, 'o'  },
        { "version",      no_argument,       NULL, 1000 },
        { "lib-versions", no_argument,       NULL, 1001 },
        { "help",         no_argument,       NULL, 'h'  },
        { NULL, 0, NULL, 0 }
    };

    /* Vlastní chybové hlášky, getopt ať mlčí. */
    opterr = 0;

    int c;
    while ( ( c = getopt_long ( argc, argv, "ho:", long_options, NULL ) ) != -1 ) {
        switch ( c ) {
            case 'o':
                opts->output_path = optarg;
                break;
            case 1000:
                print_version ();
                return 1;
            case 1001:
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

    /* Poziční argument - vstupní soubor (volitelný, max 1). */
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

    return 0;
}

/**
 * @brief Načte celý stdin do předem alokovaného bufferu.
 *
 * Funguje na principu chunked `fread` smyčky. Pokud stdin obsahuje více
 * než `buf_cap` bajtů, smyčka se zastaví na hranici a volající má
 * detekovat přetečení tím, že načtená délka == `buf_cap`.
 *
 * @param buf       Cílový buffer (nesmí být NULL).
 * @param buf_cap   Kapacita bufferu v bajtech.
 * @param out_size  Výstup: počet skutečně načtených bajtů.
 * @return 0 při úspěchu, -1 při I/O chybě.
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
 * @param path     Cesta k souboru (nesmí být NULL).
 * @param buf      Cílový buffer (nesmí být NULL).
 * @param buf_cap  Kapacita bufferu v bajtech.
 * @param out_size Výstup: počet skutečně načtených bajtů.
 * @return 0 při úspěchu, -1 při I/O chybě (open / read).
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
 * @brief Vrátí pole `fsize` z MZF hlavičky (LE 16 bit na offsetu 0x12).
 *
 * Funkce nepoužívá knihovní `mzf_load` ani `endianity_*` - čte přímo
 * dva bajty z bufferu a skládá je v little-endian pořadí, což odpovídá
 * specifikaci MZF formátu na cílové platformě Sharp MZ (Z80 = LE).
 *
 * @param buf Buffer obsahující alespoň 0x14 bajtů MZF souboru.
 * @return Velikost těla v bajtech (0..65535).
 */
static uint16_t extract_fsize_le ( const uint8_t *buf ) {
    return (uint16_t) buf[0x12] | ( (uint16_t) buf[0x13] << 8 );
}

/**
 * @brief Hlavní vstupní bod nástroje mzf-strip.
 *
 * Postup:
 *  1. Na MSYS2/Windows přepne stdin a stdout do binárního režimu
 *     (jinak by stdin přerušil 0x1A a stdout by konvertoval LF -> CRLF).
 *  2. Naparsuje CLI argumenty.
 *  3. Načte vstup (file nebo stdin) do statického bufferu.
 *  4. Ověří velikost vstupu vůči poli `fsize` v hlavičce. Vstup musí
 *     být přesně `128 + fsize` bajtů; jinak chyba + exit 1.
 *  5. Otevře výstup (file nebo stdout) a zapíše bajty `buf+128` až
 *     `buf+128+fsize`. Pokud `fsize == 0`, výstup je prázdný (rc=0).
 *  6. Uzavře výstup (pokud je to file, ne stdout).
 *
 * Knihovní `mzf_load` se záměrně nepoužívá - viz file-level Doxygen
 * (sekce "Mimo scope").
 *
 * @param argc Počet argumentů.
 * @param argv Pole argumentů.
 * @return EXIT_SUCCESS při úspěšné extrakci, EXIT_FAILURE jinak.
 */
int main ( int argc, char *argv[] ) {
#ifdef _WIN32
    /* Bez tohoto by stdin přerušil 0x1A a stdout by konvertoval LF -> CRLF. */
    setmode ( fileno ( stdin ),  _O_BINARY );
    setmode ( fileno ( stdout ), _O_BINARY );
#endif

    st_MZF_STRIP_OPTIONS opts = {
        .input_path  = NULL,
        .output_path = NULL,
        .use_stdin   = false,
    };

    int pa = parse_args ( argc, argv, &opts );
    if ( pa < 0 ) {
        fprintf ( stderr, "Try 'mzf-strip --help' for more information.\n" );
        return EXIT_FAILURE;
    }
    if ( pa > 0 ) {
        return EXIT_SUCCESS;
    }

    /* Alokace bufferu pro celý MZF (header + max body + 1 pro overflow detekci). */
    uint8_t *input_buf = malloc ( MZF_STRIP_INPUT_BUF_CAP );
    if ( !input_buf ) {
        fprintf ( stderr, "Error: cannot allocate %zu bytes for input buffer\n",
                  MZF_STRIP_INPUT_BUF_CAP );
        return EXIT_FAILURE;
    }

    size_t input_size = 0;
    int rc;
    if ( opts.use_stdin ) {
        rc = read_stdin_to_buffer ( input_buf, MZF_STRIP_INPUT_BUF_CAP, &input_size );
    } else {
        rc = load_file_to_buffer ( opts.input_path, input_buf,
                                   MZF_STRIP_INPUT_BUF_CAP, &input_size );
    }
    if ( rc != 0 ) {
        free ( input_buf );
        return EXIT_FAILURE;
    }

    /* Detekce minimální velikosti hlavičky. */
    if ( input_size < (size_t) MZF_HEADER_SIZE ) {
        fprintf ( stderr,
                  "mzf-strip: input shorter than MZF header (got %zu bytes, need at least %u)\n",
                  input_size, (unsigned) MZF_HEADER_SIZE );
        free ( input_buf );
        return EXIT_FAILURE;
    }

    /* Strict size check: vstup musí být přesně 128 + fsize bajtů.
       Truncated (n < expected) i oversized (n > expected) je odmítnut. */
    uint16_t fsize = extract_fsize_le ( input_buf );
    size_t expected = (size_t) MZF_HEADER_SIZE + (size_t) fsize;
    if ( input_size != expected ) {
        fprintf ( stderr,
                  "mzf-strip: input size mismatch (expected %zu bytes for fsize=%u, got %zu)\n",
                  expected, (unsigned) fsize, input_size );
        free ( input_buf );
        return EXIT_FAILURE;
    }

    /* Otevření výstupu - file nebo stdout. */
    FILE *fp_out;
    if ( opts.output_path ) {
        fp_out = fopen ( opts.output_path, "wb" );
        if ( !fp_out ) {
            fprintf ( stderr, "mzf-strip: cannot open output '%s': %s\n",
                      opts.output_path, strerror ( errno ) );
            free ( input_buf );
            return EXIT_FAILURE;
        }
    } else {
        fp_out = stdout;
    }

    /* Zápis těla. Explicitní guard pro fsize == 0: některé libc
       implementace mohou na fwrite(buf, 1, 0, fp) vrátit ferror. */
    int exit_code = EXIT_SUCCESS;
    if ( fsize > 0 ) {
        size_t written = fwrite ( input_buf + MZF_HEADER_SIZE, 1, (size_t) fsize, fp_out );
        if ( written != (size_t) fsize ) {
            fprintf ( stderr, "mzf-strip: write error (%zu of %u bytes written): %s\n",
                      written, (unsigned) fsize, strerror ( errno ) );
            exit_code = EXIT_FAILURE;
        }
    }

    if ( fp_out != stdout ) {
        if ( fclose ( fp_out ) != 0 ) {
            fprintf ( stderr, "mzf-strip: close error on '%s': %s\n",
                      opts.output_path, strerror ( errno ) );
            exit_code = EXIT_FAILURE;
        }
    }

    free ( input_buf );
    return exit_code;
}
