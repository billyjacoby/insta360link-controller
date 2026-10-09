#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
xcrun clang -std=c11 -Wall -Wextra -Werror -O2 probe.c \
    -framework IOKit -framework CoreFoundation -o build/linkctl-probe
printf 'Built host/linkctl-probe/build/linkctl-probe\n'
