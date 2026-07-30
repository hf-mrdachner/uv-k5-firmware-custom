#!/bin/sh
# Build and run the host-side integration tests for app/spectrum.c.
#
# These compile the REAL production source (font.c, ui/helper.c,
# helper/measurements.c, app/spectrum.c via #include) against a native host
# compiler -- no ARM cross-toolchain, no device. Only genuinely
# hardware-touching leaf functions are stubbed (see stubs.c). Runs anywhere
# with gcc; in this repo's own dev environment that means inside the
# uvk5-hosttest Docker image (archlinux + base-devel + gcc), since no host
# compiler is assumed to be installed:
#
#   docker build -t uvk5-hosttest -f tools/host_tests/Dockerfile .
#   docker run --rm -v "$PWD":/app uvk5-hosttest /app/tools/host_tests/build.sh
#
# or directly, if gcc is available on PATH:
#   ./tools/host_tests/build.sh

set -e
cd "$(dirname "$0")/../.."   # repo root

OUT=/tmp/uvk5_host_tests
mkdir -p "$OUT"

CFLAGS="-I. -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -Wall -Wextra"

gcc $CFLAGS -c font.c -o "$OUT/font.o"
gcc $CFLAGS -c helper/measurements.c -o "$OUT/measurements.o"
gcc $CFLAGS -c external/printf/printf.c -o "$OUT/printf.o"
gcc $CFLAGS -c ui/helper.c -o "$OUT/helper.o"
gcc $CFLAGS -c frequencies.c -o "$OUT/frequencies.o"
gcc $CFLAGS -c misc.c -o "$OUT/misc.o"
gcc $CFLAGS -c tools/host_tests/stubs.c -o "$OUT/stubs.o"
gcc $CFLAGS \
    -I external/CMSIS_5/CMSIS/Core/Include \
    -I external/CMSIS_5/Device/ARM/ARMCM0/Include \
    -c tools/host_tests/test_spectrum.c -o "$OUT/test_spectrum.o"

gcc "$OUT/test_spectrum.o" "$OUT/font.o" "$OUT/measurements.o" \
    "$OUT/printf.o" "$OUT/helper.o" "$OUT/frequencies.o" "$OUT/misc.o" "$OUT/stubs.o" \
    -o "$OUT/test_spectrum"

"$OUT/test_spectrum"
