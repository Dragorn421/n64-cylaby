#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 Dragorn421
# SPDX-License-Identifier: CC0-1.0

set -e

if [ -z ${N64_GCCPREFIX+x} ]  # if N64_GCCPREFIX is not set
then
    if [ -z ${N64_INST+x} ]  # if N64_INST is not set
    then
        echo 'Neither N64_GCCPREFIX nor N64_INST is set, cannot compile libdragon.'
        exit 1
    else
        N64_GCCPREFIX=$N64_INST
    fi
fi

mkdir -p build/libdragon

export N64_GCCPREFIX
export N64_INST=$(realpath build/libdragon)

echo "N64_INST set to ${N64_INST}"

njobs=$(nproc)

set -x

make -C libdragon -j${njobs}
make -C libdragon tools -j${njobs}
make -C libdragon install
make -C libdragon tools-install
