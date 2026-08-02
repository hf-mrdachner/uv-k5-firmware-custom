#!/bin/sh
# Build and run tools/host_tests/test_spectrum.c against the two German
# band-list flags (ENABLE_DE_HAM_BANDS, ENABLE_DE_EXTRA_BANDS), independently
# and combined -- build.sh only covers the default (both flags off) config.
#
# Keep the base CFLAGS in sync with build.sh's (and, per build.sh's own
# comment, with the real Makefile's spectrum-related -D flags) -- this is
# the same class of drift that caused three rounds of "clean in host tests,
# broken on hardware" earlier in this project.
#
#   docker run --rm -v "$PWD":/app uvk5-hosttest /app/tools/host_tests/build_band_list_de.sh

set -e
cd "$(dirname "$0")/../.."   # repo root

BASE_CFLAGS="-I. -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -DENABLE_SCAN_RANGES -DSPECTRUM_AUTOMATIC_SQUELCH -DENABLE_AM_FIX -Wall -Wextra -fsanitize=address -g"

build_and_run() {
    NAME="$1"
    EXTRA_CFLAGS="$2"
    OUT="/tmp/uvk5_host_tests_$NAME"
    mkdir -p "$OUT"
    CFLAGS="$BASE_CFLAGS $EXTRA_CFLAGS"

    gcc $CFLAGS -c font.c -o "$OUT/font.o"
    gcc $CFLAGS -c external/printf/printf.c -o "$OUT/printf.o"
    gcc $CFLAGS -c ui/helper.c -o "$OUT/helper.o"
    gcc $CFLAGS -c frequencies.c -o "$OUT/frequencies.o"
    gcc $CFLAGS -c misc.c -o "$OUT/misc.o"
    gcc $CFLAGS -c tools/host_tests/stubs.c -o "$OUT/stubs.o"
    gcc $CFLAGS \
        -I external/CMSIS_5/CMSIS/Core/Include \
        -I external/CMSIS_5/Device/ARM/ARMCM0/Include \
        -c tools/host_tests/test_spectrum.c -o "$OUT/test_spectrum.o"

    gcc -fsanitize=address -g "$OUT/test_spectrum.o" "$OUT/font.o" \
        "$OUT/printf.o" "$OUT/helper.o" "$OUT/frequencies.o" "$OUT/misc.o" "$OUT/stubs.o" \
        -o "$OUT/test_spectrum"

    echo "=== $NAME ==="
    "$OUT/test_spectrum"
}

build_and_run de_ham "-DENABLE_DE_HAM_BANDS"
build_and_run de_extra "-DENABLE_DE_EXTRA_BANDS"
build_and_run de_ham_and_extra "-DENABLE_DE_HAM_BANDS -DENABLE_DE_EXTRA_BANDS"
