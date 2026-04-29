# ===========================================================================
#  CLI nástroje rodiny bin2mzf
#
#  Závisí na statických knihovnách v build-libs/ (sourozenecký CMake projekt).
#  Cílové binárky: bin2mzf, mzf-info, mzf-hdr, mzf-strip, mzf-cat, mzf-paste.
# ===========================================================================

cmake_minimum_required(VERSION 3.16)
project(bin2mzf-tools C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -pipe)
add_link_options(-pipe)

add_compile_definitions(GENERIC_DRIVER_NO_BASEUI)
add_compile_definitions(GENERIC_DRIVER_FILE_CB)
add_compile_definitions(GENERIC_DRIVER_MEMORY_CB)

# Include path pro hlavičky knihoven (styl "libs/mzf/mzf.h")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# Binárky přímo do kořene build adresáře (build-cli/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Cesta k předkompilovaným .a souborům (sourozenecký projekt)
set(LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../build-libs)

# --- Linker pattern ---
# BIN2MZF_ALL_LIBS používají všechny binárky rodiny bin2mzf. Group linking
# (--start-group/--end-group) řeší vzájemné závislosti mezi statickými knihovnami
# bez nutnosti explicitního pořadí.
set(BIN2MZF_ALL_LIBS
    -Wl,--start-group
    ${LIBS_DIR}/libmzf.a
    ${LIBS_DIR}/liboutput_format.a
    ${LIBS_DIR}/libsharpmz_ascii.a
    ${LIBS_DIR}/libendianity.a
    ${LIBS_DIR}/libgeneric_driver.a
    -Wl,--end-group
)

# Preventivně přidáváme libintl pro budoucí gettext lokalizaci na MSYS2/MinGW.
# Knihovny v M1 gettext nepoužívají, takže link je 0-cost (žádný symbol nevolán).
if(WIN32)
    list(APPEND BIN2MZF_ALL_LIBS intl)
endif()

# --- Sdílená statická knihovna common modulů ---
#
# Zaveden při 4. konzumentovi (mzf-hdr task). Sdružuje argparse_helpers,
# text_field, mzf_io a cpm_helpers do jedné statické knihovny. Compile
# flags `-ffunction-sections -fdata-sections` + linker `--gc-sections`
# zajistí, že každá binárka linkuje jen funkce, které skutečně používá
# (např. mzf-paste nelinkuje text_field nebo cpm_helpers).
#
# `mzf-strip` se NEPŘILINKOVÁVÁ - nepoužívá žádný common helper.
add_library(bin2mzf_common STATIC
    common/argparse_helpers.c
    common/text_field.c
    common/mzf_io.c
    common/cpm_helpers.c)
target_include_directories(bin2mzf_common PUBLIC ${CMAKE_SOURCE_DIR}/..)
target_compile_options(bin2mzf_common PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(bin2mzf_common PUBLIC ${BIN2MZF_ALL_LIBS})

# Linker flag `--gc-sections` aplikujeme na binárky, které linkují
# `bin2mzf_common` - eliminuje nepoužité funkce common knihovny.
set(BIN2MZF_GC_LINK_FLAGS -Wl,--gc-sections)

# --- Binárky ---
add_executable(bin2mzf bin2mzf/bin2mzf.c)
target_link_libraries(bin2mzf PRIVATE bin2mzf_common)
target_link_options(bin2mzf PRIVATE ${BIN2MZF_GC_LINK_FLAGS})

add_executable(mzf-info mzf-info/mzf_info.c)
target_link_libraries(mzf-info PRIVATE bin2mzf_common)
target_link_options(mzf-info PRIVATE ${BIN2MZF_GC_LINK_FLAGS})

# mzf-strip: extrakce těla z MZF (inverze bin2mzf). Nepoužívá žádný
# parser číselných hodnot ani charset - common knihovna se nelinkuje.
add_executable(mzf-strip mzf-strip/mzf_strip.c)
target_link_libraries(mzf-strip PRIVATE ${BIN2MZF_ALL_LIBS})

add_executable(mzf-cat mzf-cat/mzf_cat.c)
target_link_libraries(mzf-cat PRIVATE bin2mzf_common)
target_link_options(mzf-cat PRIVATE ${BIN2MZF_GC_LINK_FLAGS})

# mzf-paste: insert/overwrite binárních dat na offset těla MZF.
add_executable(mzf-paste mzf-paste/mzf_paste.c)
target_link_libraries(mzf-paste PRIVATE bin2mzf_common)
target_link_options(mzf-paste PRIVATE ${BIN2MZF_GC_LINK_FLAGS})

# mzf-hdr: modifikace hlavičky existujícího MZF beze změny těla.
add_executable(mzf-hdr mzf-hdr/mzf_hdr.c)
target_link_libraries(mzf-hdr PRIVATE bin2mzf_common)
target_link_options(mzf-hdr PRIVATE ${BIN2MZF_GC_LINK_FLAGS})
