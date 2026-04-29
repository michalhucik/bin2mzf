/**
 * @file    bin2mzf_cli_version.h
 * @brief   Společná release verze rodiny CLI nástrojů bin2mzf.
 *
 * Rodina CLI nástrojů projektu bin2mzf (bin2mzf, mzf-info, mzf-hdr,
 * mzf-strip, mzf-cat, mzf-paste) se releasuje jako jeden celek pod
 * společnou release verzí. Každá binárka si navíc drží svou vlastní
 * sémantickou verzi (například @c BIN2MZF_VERSION), která odráží
 * změny v daném konkrétním nástroji.
 *
 * Při výpisu @c --version se vypisuje dvojice "tool version" a
 * "CLI release", aby bylo v hlášeních o chybách jednoznačné,
 * o kterou kombinaci jde.
 *
 * Při bumpu release verze CLI upravte @c BIN2MZF_CLI_RELEASE_VERSION
 * v tomto souboru a doplňte záznam do @c docs/cz/Changelog.md a
 * @c docs/en/Changelog.md.
 */

#ifndef BIN2MZF_CLI_VERSION_H
#define BIN2MZF_CLI_VERSION_H

/**
 * @brief Release verze celé rodiny CLI nástrojů bin2mzf.
 *
 * Sdílená napříč všemi binárkami (bin2mzf, mzf-info, mzf-hdr,
 * mzf-strip, mzf-cat, mzf-paste). Hodnotu načítá Makefile přes
 * @c awk pro hlášku v @c make install.
 */
#define BIN2MZF_CLI_RELEASE_VERSION "1.0.0"

/**
 * @brief Jméno release celé rodiny CLI nástrojů bin2mzf.
 *
 * Používá se v hlavičkách výpisů @c --version / @c --lib-versions.
 */
#define BIN2MZF_CLI_RELEASE_NAME "bin2mzf-cli"

#endif /* BIN2MZF_CLI_VERSION_H */
