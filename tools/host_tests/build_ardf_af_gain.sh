#!/bin/sh
# Build and run the host-side unit tests for app/ardf_af_gain.c.
#
# This needs no hardware stubs at all: the module under test is pure
# arithmetic with zero globals or hardware dependencies. Runs anywhere with
# gcc; in this repo's own dev environment that means inside the
# uvk5-hosttest Docker image (see tools/host_tests/Dockerfile), since no host
# compiler is assumed to be installed:
#
#   docker build -t uvk5-hosttest -f tools/host_tests/Dockerfile .
#   docker run --rm -v "$PWD":/app uvk5-hosttest /app/tools/host_tests/build_ardf_af_gain.sh
#
# or directly, if gcc is available on PATH:
#   ./tools/host_tests/build_ardf_af_gain.sh

set -e
cd "$(dirname "$0")/../.."   # repo root

OUT=/tmp/uvk5_host_tests
mkdir -p "$OUT"

gcc -I. -Wall -Wextra -fsanitize=address -g \
    tools/host_tests/test_ardf_af_gain.c \
    -o "$OUT/test_ardf_af_gain"

"$OUT/test_ardf_af_gain"
