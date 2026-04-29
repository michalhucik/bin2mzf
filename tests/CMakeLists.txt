# ===========================================================================
#  Testy bin2mzf - C unit testy knihoven + scaffolding pro CLI testy
#
#  Adresářová struktura:
#    tests/libs/  - C unit testy knihoven z src/libs/
#    tests/cli/   - CLI integrační testy (bash skripty, source helpers.sh)
#                   spouští je přímo Makefile target `test-cli`, ne CMake.
#
#  C testy se kompilují do build-tests/ a linkují .a z build-libs/.
# ===========================================================================

cmake_minimum_required(VERSION 3.16)
project(bin2mzf-tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -pipe)
add_link_options(-pipe)

add_compile_definitions(GENERIC_DRIVER_NO_BASEUI)
add_compile_definitions(GENERIC_DRIVER_FILE_CB)
add_compile_definitions(GENERIC_DRIVER_MEMORY_CB)

# Include cesty:
#   src/   - pro "libs/mzf/mzf.h" atd.
#   tests/ - pro "test_framework.h"
set(BIN2MZF_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${BIN2MZF_SRC_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Cesta k předkompilovaným .a souborům (sourozenecký projekt)
set(BIN2MZF_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../build-libs)

# --- Linker pattern ---
# BIN2MZF_ALL_LIBS pro test executables. Group linking řeší vzájemné závislosti
# mezi statickými knihovnami bez závislosti na pořadí.
set(BIN2MZF_ALL_LIBS
    -Wl,--start-group
    ${BIN2MZF_LIBS_DIR}/libmzf.a
    ${BIN2MZF_LIBS_DIR}/liboutput_format.a
    ${BIN2MZF_LIBS_DIR}/libsharpmz_ascii.a
    ${BIN2MZF_LIBS_DIR}/libendianity.a
    ${BIN2MZF_LIBS_DIR}/libgeneric_driver.a
    -Wl,--end-group
)

# Preventivně přidáváme libintl pro budoucí gettext lokalizaci na MSYS2/MinGW.
if(WIN32)
    list(APPEND BIN2MZF_ALL_LIBS intl)
endif()

# Memory driver jako extra zdroj pro testy, které volají memory I/O přímo.
# Standalone realloc varianta - viz memory_driver.h.
set(MEMORY_DRIVER_SRC ${BIN2MZF_SRC_DIR}/libs/generic_driver/memory_driver.c)

# --- Test executables ---
add_executable(test_endianity libs/test_endianity.c)
target_link_libraries(test_endianity ${BIN2MZF_ALL_LIBS})

add_executable(test_sharpmz_ascii libs/test_sharpmz_ascii.c)
target_link_libraries(test_sharpmz_ascii ${BIN2MZF_ALL_LIBS})

add_executable(test_memory_driver libs/test_memory_driver.c ${MEMORY_DRIVER_SRC})
target_link_libraries(test_memory_driver ${BIN2MZF_ALL_LIBS})

add_executable(test_mzf libs/test_mzf.c ${MEMORY_DRIVER_SRC})
target_link_libraries(test_mzf ${BIN2MZF_ALL_LIBS})

add_executable(test_output_format libs/test_output_format.c)
target_link_libraries(test_output_format ${BIN2MZF_ALL_LIBS})
