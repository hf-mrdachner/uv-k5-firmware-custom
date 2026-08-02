#!/bin/sh
# Build and run the host-side unit tests for app/ardf_df_simple.c.
#
# Unlike test_spectrum.c, this needs no hardware stubs at all: the module
# under test only reads/writes gEeprom and two plain ARDF globals, both
# defined directly in test_ardf_df_simple.c. Runs anywhere with gcc; in this
# repo's own dev environment that means inside the uvk5-hosttest Docker image
# (see tools/host_tests/Dockerfile), since no host compiler is assumed to be
# installed:
#
#   docker build -t uvk5-hosttest -f tools/host_tests/Dockerfile .
#   docker run --rm -v "$PWD":/app uvk5-hosttest /app/tools/host_tests/build_ardf_df_simple.sh
#
# or directly, if gcc is available on PATH:
#   ./tools/host_tests/build_ardf_df_simple.sh

set -e
cd "$(dirname "$0")/../.."   # repo root

OUT=/tmp/uvk5_host_tests
mkdir -p "$OUT"

gcc -I. -Wall -Wextra -fsanitize=address -g \
    tools/host_tests/test_ardf_df_simple.c \
    -o "$OUT/test_ardf_df_simple"

"$OUT/test_ardf_df_simple"
