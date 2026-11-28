# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2022-2026 Gabriel Montes
#
# Cross-platform build & test container for GOOB Chess Engine (linux/amd64).
# Provides:
#   - GCC (native x86-64 Linux builds: universal, x86-64, v2, v3, v4)
#   - MinGW-w64 (Windows x86-64 PE .exe cross-builds)
#   - QEMU user-mode (qemu-x86_64 with full AVX2/BMI2/AVX-512 emulation)
#   - Wine (wine64 for executing Windows .exe binaries)
#   - Python 3 + file (for automated cross-platform verification)

FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV WINEARCH=win64
ENV WINEPREFIX=/opt/wineprefix
ENV WINEDEBUG=-all
ENV XDG_RUNTIME_DIR=/tmp

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-mingw-w64-x86-64 \
        qemu-user \
        wine \
        wine64 \
        python3 \
        file \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Pre-initialize a headless 64-bit Wine prefix so runtime invocations are fast and quiet.
RUN mkdir -p /opt/wineprefix \
    && wineboot --init \
    && wineserver -w \
    && chmod -R a+rwX /opt/wineprefix

WORKDIR /workspace
