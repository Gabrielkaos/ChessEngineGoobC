#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2022-2026 Gabriel Montes
#
# Cross-platform build & test driver for macOS (Apple Silicon / Intel) using
# Docker (OrbStack / Docker Desktop) for Linux x86-64 and Windows x86-64 builds
# and execution via Rosetta, QEMU (SSE2/SSE4.2/AVX2/BMI2/AVX-512), and Wine.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE_NAME="${GOOB_CROSS_IMAGE:-goob-cross:latest}"
ENGINE_NAME="GOOB-2.2-BETA"
X86_TIERS=(universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4)

usage() {
    cat <<'EOF'
Usage: tools/cross/cross.sh <command> [args...]

Commands:
  setup
      Build the Docker image (goob-cross:latest, linux/amd64) containing GCC,
      MinGW-w64, QEMU (qemu-x86_64 with AVX-512), Wine, and Python 3.

  build [all|mac|linux|windows] [tier...]
      Build engine binaries. Default platform is 'all'.
      Default tiers:
        mac     : apple-silicon native (on Apple Silicon)
        linux   : universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4
        windows : universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4

  run <mac|linux|windows> <tier> [--cpu <qemu-cpu>] [--] [engine args...]
      Run a built binary directly (interactive or piped).
      Examples:
        tools/cross/cross.sh run mac apple-silicon compiler
        tools/cross/cross.sh run linux universal --cpu Haswell,-hle,-rtm compiler
        tools/cross/cross.sh run linux x86-64-v3 bench 8
        tools/cross/cross.sh run windows x86-64-v3 bench 8

  bench <mac|linux|windows> <tier> [depth=12] [--cpu <qemu-cpu>]
      Run tools/sprt/bench.py against the specified platform/tier binary.

  test [--depth N] [--only mac,linux,windows] [--skip-build]
      Run the full cross-platform build + verification matrix across macOS,
      Linux (all x86-64 ISA tiers + universal dispatch), and Windows (Wine).

  shell
      Open an interactive bash shell inside the cross-build container.
EOF
}

require_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "ERROR: docker CLI not found on PATH." >&2
        exit 1
    fi
    if ! docker info >/dev/null 2>&1; then
        echo "ERROR: Docker daemon is not running (start OrbStack or Docker Desktop)." >&2
        exit 1
    fi
}

ensure_image() {
    require_docker
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        echo "==> Docker image '$IMAGE_NAME' not found; building it now..."
        cmd_setup
    fi
}

docker_tty_flags() {
    if [ -t 0 ] && [ -t 1 ]; then
        echo "-it"
    else
        echo "-i"
    fi
}

docker_run() {
    local tty_flag
    tty_flag="$(docker_tty_flags)"
    # Mount the repo at /workspace. src/weights is mounted read-only as an extra
    # guard so nothing inside the container can ever modify NNUE weight files.
    local extra_mounts=()
    if [ -d "$REPO_ROOT/src/weights" ]; then
        extra_mounts+=(-v "$REPO_ROOT/src/weights:/workspace/src/weights:ro")
    fi
    docker run --rm $tty_flag \
        --platform linux/amd64 \
        -e XDG_RUNTIME_DIR=/tmp \
        -v "$REPO_ROOT:/workspace" \
        ${extra_mounts[@]+"${extra_mounts[@]}"} \
        -w /workspace \
        "$IMAGE_NAME" \
        "$@"
}

cmd_setup() {
    require_docker
    echo "==> Building Docker image $IMAGE_NAME (linux/amd64)..."
    docker build \
        --platform linux/amd64 \
        -t "$IMAGE_NAME" \
        -f "$SCRIPT_DIR/Dockerfile" \
        "$SCRIPT_DIR"
    echo "==> Done: $IMAGE_NAME is ready."
}

host_cpus() {
    sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4
}

cmd_build() {
    local platform="${1:-all}"
    shift || true
    local tiers=("$@")

    case "$platform" in
        mac|macos)
            echo "==> Building macOS binaries (native host)..."
            local jobs
            jobs="$(host_cpus)"
            if [ "${#tiers[@]}" -eq 0 ]; then
                make -C "$REPO_ROOT/src" TARGET=mac -j"$jobs" all
            else
                make -C "$REPO_ROOT/src" TARGET=mac -j"$jobs" "${tiers[@]}"
            fi
            ;;
        linux)
            ensure_image
            if [ "${#tiers[@]}" -eq 0 ]; then
                tiers=("${X86_TIERS[@]}")
            fi
            echo "==> Building Linux x86-64 binaries (${tiers[*]}) in Docker..."
            docker_run bash -c "make -C src TARGET=linux -j\$(nproc) ${tiers[*]}"
            ;;
        windows|win)
            ensure_image
            if [ "${#tiers[@]}" -eq 0 ]; then
                tiers=("${X86_TIERS[@]}")
            fi
            echo "==> Building Windows x86-64 binaries (${tiers[*]}) in Docker..."
            docker_run bash -c "make -C src TARGET=windows -j\$(nproc) ${tiers[*]}"
            ;;
        all)
            if [ "$(uname -s)" = "Darwin" ]; then
                cmd_build mac
            fi
            cmd_build linux
            cmd_build windows
            ;;
        *)
            echo "ERROR: Unknown build platform '$platform' (use all|mac|linux|windows)." >&2
            exit 1
            ;;
    esac
}

cmd_run() {
    if [ "$#" -lt 2 ]; then
        echo "ERROR: 'run' requires <mac|linux|windows> <tier> [args...]" >&2
        exit 1
    fi
    local platform="$1"
    local tier="$2"
    shift 2

    local qemu_cpu=""
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --cpu)
                qemu_cpu="$2"
                shift 2
                ;;
            --)
                shift
                break
                ;;
            *)
                break
                ;;
        esac
    done

    case "$platform" in
        mac|macos)
            local bin="$REPO_ROOT/src/bin/mac/${ENGINE_NAME}-${tier}"
            if [ ! -x "$bin" ]; then
                echo "ERROR: Binary not found: $bin (run: tools/cross/cross.sh build mac)" >&2
                exit 1
            fi
            exec "$bin" "$@"
            ;;
        linux)
            ensure_image
            local rel_bin="src/bin/linux/${ENGINE_NAME}-${tier}"
            if [ ! -f "$REPO_ROOT/$rel_bin" ]; then
                echo "ERROR: Binary not found: $REPO_ROOT/$rel_bin (run: tools/cross/cross.sh build linux)" >&2
                exit 1
            fi
            if [ "$tier" = "x86-64-v4" ] && [ -z "$qemu_cpu" ]; then
                echo "NOTE: x86-64-v4 (AVX-512) cannot execute under Rosetta or QEMU 10 TCG; running anyway (only 'compiler' succeeds)." >&2
            fi
            if [ -n "$qemu_cpu" ]; then
                docker_run qemu-x86_64 -cpu "$qemu_cpu" "./$rel_bin" "$@"
            else
                docker_run "./$rel_bin" "$@"
            fi
            ;;
        windows|win)
            ensure_image
            local rel_bin="src/bin/windows/${ENGINE_NAME}-${tier}.exe"
            if [ ! -f "$REPO_ROOT/$rel_bin" ]; then
                echo "ERROR: Binary not found: $REPO_ROOT/$rel_bin (run: tools/cross/cross.sh build windows)" >&2
                exit 1
            fi
            if [ "$tier" = "x86-64-v4" ] && [ -z "$qemu_cpu" ]; then
                echo "NOTE: x86-64-v4.exe (AVX-512) cannot execute under Rosetta/Wine or QEMU 10 TCG." >&2
            fi
            if [ -n "$qemu_cpu" ]; then
                docker_run qemu-x86_64 -cpu "$qemu_cpu" /usr/lib/wine/wine64 "./$rel_bin" "$@"
            else
                docker_run wine "./$rel_bin" "$@"
            fi
            ;;
        *)
            echo "ERROR: Unknown platform '$platform' (use mac|linux|windows)." >&2
            exit 1
            ;;
    esac
}

cmd_bench() {
    if [ "$#" -lt 2 ]; then
        echo "ERROR: 'bench' requires <mac|linux|windows> <tier> [depth=12] [--cpu <qemu-cpu>]" >&2
        exit 1
    fi
    local platform="$1"
    local tier="$2"
    shift 2
    local depth="12"
    local qemu_cpu=""
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --cpu)
                qemu_cpu="$2"
                shift 2
                ;;
            *)
                depth="$1"
                shift
                ;;
        esac
    done

    case "$platform" in
        mac|macos)
            python3 "$REPO_ROOT/tools/sprt/bench.py" "$REPO_ROOT/src/bin/mac/${ENGINE_NAME}-${tier}" "$depth"
            ;;
        linux|windows|win)
            ensure_image
            python3 "$SCRIPT_DIR/test_matrix.py" --bench-only "$platform" "$tier" --depth "$depth" ${qemu_cpu:+--cpu "$qemu_cpu"}
            ;;
        *)
            echo "ERROR: Unknown platform '$platform'." >&2
            exit 1
            ;;
    esac
}

cmd_test() {
    local skip_build=0
    local only="mac,linux,windows"
    local passthrough=()

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --skip-build)
                skip_build=1
                shift
                ;;
            --only)
                only="$2"
                passthrough+=("$1" "$2")
                shift 2
                ;;
            *)
                passthrough+=("$1")
                shift
                ;;
        esac
    done

    if [ "$skip_build" -eq 0 ]; then
        IFS=',' read -ra plats <<< "$only"
        for p in "${plats[@]}"; do
            case "$p" in
                mac|macos)
                    if [ "$(uname -s)" = "Darwin" ]; then
                        cmd_build mac
                    fi
                    ;;
                linux)   cmd_build linux ;;
                windows|win) cmd_build windows ;;
            esac
        done
    fi

    if [[ "$only" == *linux* ]] || [[ "$only" == *windows* ]] || [[ "$only" == *win* ]]; then
        ensure_image
    fi

    python3 "$SCRIPT_DIR/test_matrix.py" ${passthrough[@]+"${passthrough[@]}"}
}

cmd_shell() {
    ensure_image
    docker_run bash
}

main() {
    if [ "$#" -lt 1 ]; then
        usage
        exit 0
    fi
    local cmd="$1"
    shift
    case "$cmd" in
        setup) cmd_setup "$@" ;;
        build) cmd_build "$@" ;;
        run)   cmd_run "$@" ;;
        bench) cmd_bench "$@" ;;
        test)  cmd_test "$@" ;;
        shell) cmd_shell "$@" ;;
        -h|--help|help) usage ;;
        *)
            echo "ERROR: Unknown command '$cmd'." >&2
            usage >&2
            exit 1
            ;;
    esac
}

main "$@"
