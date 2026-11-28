#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2022-2026 Gabriel Montes
"""
Cross-platform verification suite for GOOB Chess Engine.

Runs tests across:
  - macOS   (native host Apple Silicon: apple-silicon, native)
  - Linux   (Docker x86-64 via Rosetta + QEMU CPU models: SSE2, SSE4.2, AVX2/BMI2, AVX-512)
  - Windows (Docker x86-64 via Wine: universal.exe, x86-64.exe, x86-64-v2.exe, etc.)

Verifies per binary / ISA tier:
  1. Executable file format (Mach-O / ELF / PE32+)
  2. `compiler` output (expected CPU tier, SIMD kernel, and PEXT/Magic sliders)
  3. Built-in TT unit tests (`test` before `uci`)
  4. Move generation `perft 4` from startpos == 197281
  5. Static `eval` on startpos (must match bit-for-bit across all OSes and ISAs)
  6. Fixed-depth `bench <depth>` (must match total node count bit-for-bit across all OSes and ISAs)
  7. Multi-threaded UCI smoke test (`Threads 2`, `go depth 7`, legal `bestmove`, clean exit)
"""

import argparse
import os
import platform
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]
ENGINE_NAME = "GOOB-2.2-BETA"
IMAGE_NAME = os.environ.get("GOOB_CROSS_IMAGE", "goob-cross:latest")

STARTPOS_PERFT4 = 197281


@dataclass
class TestTarget:
    label: str
    os_name: str           # "mac" | "linux" | "windows"
    tier: str
    rel_bin: str
    expected_file_substr: str
    expected_arch: str
    expected_simd: str
    expected_pext: str     # "BMI2 PEXT" or "Magic Bitboards"
    qemu_cpu: Optional[str] = None
    use_wine: bool = False


def build_exec_cmd(target: TestTarget, extra_args: List[str]) -> List[str]:
    if target.os_name == "mac":
        return [str(REPO_ROOT / target.rel_bin), *extra_args]

    docker_cmd = [
        "docker", "run", "--rm", "-i",
        "--platform", "linux/amd64",
        "-e", "XDG_RUNTIME_DIR=/tmp",
        "-v", f"{REPO_ROOT}:/workspace",
        "-w", "/workspace",
    ]
    weights_dir = REPO_ROOT / "src" / "weights"
    if weights_dir.is_dir():
        docker_cmd += ["-v", f"{weights_dir}:/workspace/src/weights:ro"]
    docker_cmd.append(IMAGE_NAME)

    if target.use_wine:
        if target.qemu_cpu:
            docker_cmd += ["qemu-x86_64", "-cpu", target.qemu_cpu, "/usr/lib/wine/wine64", f"./{target.rel_bin}", *extra_args]
        else:
            docker_cmd += ["wine", f"./{target.rel_bin}", *extra_args]
    elif target.qemu_cpu:
        docker_cmd += ["qemu-x86_64", "-cpu", target.qemu_cpu, f"./{target.rel_bin}", *extra_args]
    else:
        docker_cmd += [f"./{target.rel_bin}", *extra_args]

    return docker_cmd


def run_cmd(cmd: List[str], stdin_text: Optional[str] = None, timeout: int = 180) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        input=stdin_text,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def check_file_format(target: TestTarget) -> str:
    bin_path = REPO_ROOT / target.rel_bin
    if not bin_path.is_file():
        raise AssertionError(f"missing binary: {bin_path}")
    res = run_cmd(["file", "-b", str(bin_path)], timeout=15)
    desc = res.stdout.strip()
    if target.expected_file_substr not in desc:
        raise AssertionError(f"unexpected file format: got {desc!r}, expected substring {target.expected_file_substr!r}")
    return desc


def check_compiler_info(target: TestTarget) -> dict:
    res = run_cmd(build_exec_cmd(target, ["compiler"]), timeout=60)
    if res.returncode != 0:
        raise AssertionError(f"`compiler` exited {res.returncode}: {res.stderr.strip()}")
    out = res.stdout.replace("\r\n", "\n")
    fields = {}
    for line in out.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            fields[k.strip()] = v.strip()

    arch = fields.get("Detected CPU Architecture", "")
    simd = fields.get("Active SIMD Kernel", "")
    slider = fields.get("Active Slider Attacks", "")

    if target.expected_arch not in arch:
        raise AssertionError(f"arch mismatch: got {arch!r}, expected {target.expected_arch!r}")
    if target.expected_simd not in simd:
        raise AssertionError(f"SIMD mismatch: got {simd!r}, expected {target.expected_simd!r}")
    if target.expected_pext not in slider:
        raise AssertionError(f"slider mismatch: got {slider!r}, expected {target.expected_pext!r}")
    return fields


def check_tt_tests(target: TestTarget) -> int:
    res = run_cmd(build_exec_cmd(target, []), stdin_text="test\nquit\n", timeout=60)
    if res.returncode != 0:
        raise AssertionError(f"`test` exited {res.returncode}: {res.stderr.strip()}")
    out = res.stdout.replace("\r\n", "\n")
    m = re.search(r"(\d+)\s+passed,\s+(\d+)\s+failed", out, re.IGNORECASE)
    if not m:
        # Check for PASS lines if summary format differs
        fails = re.findall(r"\bFAIL\b", out)
        passes = re.findall(r"\bPASS\b", out)
        if fails or not passes:
            raise AssertionError(f"unexpected `test` output:\n{out[-600:]}")
        return len(passes)
    passed, failed = int(m.group(1)), int(m.group(2))
    if failed != 0 or passed == 0:
        raise AssertionError(f"TT tests failed ({passed} passed, {failed} failed):\n{out[-600:]}")
    return passed


def check_uci_perft_eval_smp(target: TestTarget) -> tuple[int, int, str]:
    script = "\n".join([
        "uci",
        "isready",
        "position startpos",
        "eval",
        "perft 4",
        "setoption name Hash value 32",
        "setoption name Threads value 2",
        "isready",
        "position startpos",
        "go depth 7",
    ]) + "\n"

    # Wait for bestmove before sending quit so the engine does not exit early
    cmd = build_exec_cmd(target, [])
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    assert proc.stdin is not None and proc.stdout is not None

    def send(s: str) -> None:
        proc.stdin.write(s + "\n")
        proc.stdin.flush()

    def read_until(predicate, timeout_s: float = 120.0) -> List[str]:
        lines: List[str] = []
        t0 = time.monotonic()
        while True:
            if time.monotonic() - t0 > timeout_s:
                proc.kill()
                raise AssertionError(f"timed out waiting for engine output; last lines: {lines[-10:]}")
            line = proc.stdout.readline()
            if not line:
                err = proc.stderr.read() if proc.stderr else ""
                raise AssertionError(f"engine exited unexpectedly; stdout tail={lines[-10:]}, stderr={err[-400:]}")
            clean = line.rstrip("\r\n")
            lines.append(clean)
            if predicate(clean):
                return lines

    try:
        send("uci")
        read_until(lambda l: l == "uciok")
        send("isready")
        read_until(lambda l: l == "readyok")

        send("position startpos")
        send("eval")
        send("isready")
        eval_lines = read_until(lambda l: l == "readyok")
        eval_val = None
        for l in eval_lines:
            m = re.search(r"Eval\s*:\s*(-?\d+)", l)
            if m:
                eval_val = int(m.group(1))
                break
        if eval_val is None:
            raise AssertionError(f"could not parse `eval` output: {eval_lines}")

        send("perft 4")
        send("isready")
        perft_lines = read_until(lambda l: l == "readyok")
        perft_nodes = None
        for l in perft_lines:
            m = re.search(r"(?:Nodes|Leaf Nodes|Total Nodes|Test Complete)\D*(\d+)", l, re.IGNORECASE)
            if m:
                val = int(m.group(1))
                if val == STARTPOS_PERFT4:
                    perft_nodes = val
                    break
                perft_nodes = val
        if perft_nodes != STARTPOS_PERFT4:
            raise AssertionError(f"perft 4 mismatch: got {perft_nodes}, expected {STARTPOS_PERFT4}; lines={perft_lines[-10:]}")

        send("setoption name Hash value 32")
        send("setoption name Threads value 2")
        send("isready")
        read_until(lambda l: l == "readyok")
        send("ucinewgame")
        send("isready")
        read_until(lambda l: l == "readyok")
        send("position startpos")
        send("go depth 7")
        go_lines = read_until(lambda l: l.startswith("bestmove "))
        bestmove = go_lines[-1].split()[1]
        if bestmove in ("(none)", "0000", ""):
            raise AssertionError(f"invalid SMP bestmove: {go_lines[-1]!r}")

        send("quit")
        proc.wait(timeout=30)
        if proc.returncode != 0:
            err = proc.stderr.read() if proc.stderr else ""
            raise AssertionError(f"non-zero exit code {proc.returncode}: {err[-400:]}")
        return eval_val, perft_nodes, bestmove
    finally:
        if proc.poll() is None:
            proc.kill()


def check_bench(target: TestTarget, depth: int) -> tuple[int, int]:
    t0 = time.monotonic()
    res = run_cmd(build_exec_cmd(target, ["bench", str(depth)]), timeout=300)
    elapsed_ms = int((time.monotonic() - t0) * 1000)
    if res.returncode != 0:
        raise AssertionError(f"`bench {depth}` exited {res.returncode}: {res.stderr.strip()}")
    out = res.stdout.replace("\r\n", "\n")
    # Parse final summary line, e.g. "Total nodes: 123456 ..." or "123456 nodes 456789 nps"
    nodes = None
    nps = 0
    for line in reversed(out.splitlines()):
        m_nodes = re.search(r"(\d+)\s+nodes", line, re.IGNORECASE)
        if not m_nodes:
            m_nodes = re.search(r"nodes\s*[:=]\s*(\d+)", line, re.IGNORECASE)
        if m_nodes:
            nodes = int(m_nodes.group(1))
            m_nps = re.search(r"(\d+)\s+nps", line, re.IGNORECASE)
            if not m_nps:
                m_nps = re.search(r"nps\s*[:=]\s*(\d+)", line, re.IGNORECASE)
            if m_nps:
                nps = int(m_nps.group(1))
            break
    if nodes is None or nodes <= 0:
        raise AssertionError(f"could not parse bench nodes from output:\n{out[-600:]}")
    return nodes, (nps or (nodes * 1000 // max(1, elapsed_ms)))


def run_sprt_bench_only(os_name: str, tier: str, depth: int, qemu_cpu: Optional[str]) -> int:
    """Run tools/sprt/bench.py's 16-position suite against any Linux/Windows binary."""
    sys.path.insert(0, str(REPO_ROOT / "tools" / "sprt"))
    fens = [
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R1BQKB1R w KQ - 0 1",
        "8/8/1p1k4/p1p1p3/P1P1P1p1/1P1K2P1/8/8 w - - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
        "r2q1rk1/1b1nbppp/pp1ppn2/8/2PNP3/1PN1B3/P2QBPPP/R4RK1 w - - 0 1",
        "3rr1k1/pp3ppp/2n2q2/2b1p3/8/P1NB1Q2/1PP2PPP/R3R1K1 w - - 0 1",
        "r1b1k2r/ppppqppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 1",
        "2kr3r/ppp1qppp/2n1bn2/4p3/4P3/2N1BN2/PPPQ1PPP/2KR3R w - - 0 1",
        "8/5pk1/6p1/8/3K4/8/5PPP/8 w - - 0 1",
        "r1bqkb1r/pp3ppp/2n1pn2/2pp4/3P4/2P1PN2/PP1N1PPP/R1BQKB1R w KQkq - 0 1",
    ]
    is_win = os_name in ("windows", "win")
    rel_bin = f"src/bin/{'windows' if is_win else 'linux'}/{ENGINE_NAME}-{tier}{'.exe' if is_win else ''}"
    target = TestTarget(
        label=f"{os_name}/{tier}",
        os_name="windows" if is_win else "linux",
        tier=tier,
        rel_bin=rel_bin,
        expected_file_substr="",
        expected_arch="",
        expected_simd="",
        expected_pext="",
        qemu_cpu=qemu_cpu,
        use_wine=is_win,
    )
    cmd = build_exec_cmd(target, [])
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    assert p.stdin is not None and p.stdout is not None

    def send(s: str) -> None:
        p.stdin.write(s + "\n")
        p.stdin.flush()

    def readuntil(tok: str) -> List[str]:
        lines = []
        while True:
            l = p.stdout.readline()
            if not l:
                raise SystemExit("engine died")
            lines.append(l.strip())
            if l.strip().startswith(tok):
                return lines

    send("uci")
    readuntil("uciok")
    send("setoption name Hash value 64")
    totn = tott = 0
    for f in fens:
        send("ucinewgame")
        send("isready")
        readuntil("readyok")
        send(f"position fen {f}")
        send(f"go depth {depth}")
        lines = readuntil("bestmove")
        last = [l for l in lines if l.startswith("info depth ") and " nodes " in l]
        last_line = last[-1] if last else ""
        n = int(re.search(r" nodes (\d+)", last_line).group(1)) if last_line else 0
        t = int(re.search(r" time (\d+)", last_line).group(1)) if last_line else 0
        sc_m = re.search(r"score (cp|mate) (-?\d+)", last_line)
        sc = f"{sc_m.group(1)} {sc_m.group(2)}" if sc_m else "?"
        bm = lines[-1].split()[1]
        print(f"{bm:8s} {sc:10s} nodes={n:<10d} time={t:<6d} {f[:32]}")
        totn += n
        tott += t
    send("quit")
    p.wait(timeout=30)
    print(f"TOTAL nodes={totn} time={tott}ms nps={totn*1000//max(1,tott)}")
    return 0


def build_matrix(selected_os: set[str]) -> List[TestTarget]:
    targets: List[TestTarget] = []

    if "mac" in selected_os and platform.system() == "Darwin":
        targets += [
            TestTarget(
                label="mac / apple-silicon (native)",
                os_name="mac",
                tier="apple-silicon",
                rel_bin=f"src/bin/mac/{ENGINE_NAME}-apple-silicon",
                expected_file_substr="Mach-O 64-bit executable arm64",
                expected_arch="ARM64",
                expected_simd="NEON",
                expected_pext="Magic Bitboards",
            ),
            TestTarget(
                label="mac / native (host)",
                os_name="mac",
                tier="native",
                rel_bin=f"src/bin/mac/{ENGINE_NAME}-native",
                expected_file_substr="Mach-O 64-bit executable arm64",
                expected_arch="ARM64",
                expected_simd="NEON",
                expected_pext="Magic Bitboards",
            ),
        ]

    if "linux" in selected_os:
        targets += [
            # Universal runtime dispatch across all x86-64 tiers up to AVX2-BMI2
            TestTarget(
                label="linux / universal (Rosetta SSE4.2)",
                os_name="linux",
                tier="universal",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-universal",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="SSE4.2",
                expected_simd="SSE4.2",
                expected_pext="Magic Bitboards",
            ),
            TestTarget(
                label="linux / universal [QEMU qemu64 -> x86-64]",
                os_name="linux",
                tier="universal",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-universal",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="x86-64",
                expected_simd="Scalar / SSE2",
                expected_pext="Magic Bitboards",
                qemu_cpu="qemu64",
            ),
            TestTarget(
                label="linux / universal [QEMU Nehalem -> SSE4.2]",
                os_name="linux",
                tier="universal",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-universal",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="SSE4.2",
                expected_simd="SSE4.2",
                expected_pext="Magic Bitboards",
                qemu_cpu="Nehalem",
            ),
            TestTarget(
                label="linux / universal [QEMU Haswell,-bmi2 -> AVX2]",
                os_name="linux",
                tier="universal",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-universal",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="AVX2",
                expected_simd="AVX2",
                expected_pext="Magic Bitboards",
                qemu_cpu="Haswell,-bmi2,-hle,-rtm",
            ),
            TestTarget(
                label="linux / universal [QEMU Haswell -> AVX2-BMI2]",
                os_name="linux",
                tier="universal",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-universal",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="AVX2-BMI2",
                expected_simd="AVX2",
                expected_pext="BMI2 PEXT",
                qemu_cpu="Haswell,-hle,-rtm",
            ),
            # Dedicated Linux ISA binaries (Rosetta supports up through AVX2+BMI2)
            TestTarget(
                label="linux / x86-64 (Rosetta)",
                os_name="linux",
                tier="x86-64",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-x86-64",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="x86-64",
                expected_simd="Scalar / SSE2",
                expected_pext="Magic Bitboards",
            ),
            TestTarget(
                label="linux / x86-64-v2 (Rosetta)",
                os_name="linux",
                tier="x86-64-v2",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-x86-64-v2",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="SSE4.2",
                expected_simd="SSE4.2",
                expected_pext="Magic Bitboards",
            ),
            TestTarget(
                label="linux / x86-64-v3 (Rosetta AVX2-BMI2)",
                os_name="linux",
                tier="x86-64-v3",
                rel_bin=f"src/bin/linux/{ENGINE_NAME}-x86-64-v3",
                expected_file_substr="ELF 64-bit LSB",
                expected_arch="AVX2-BMI2",
                expected_simd="AVX2",
                expected_pext="BMI2 PEXT",
            ),
        ]

    if "windows" in selected_os:
        targets += [
            TestTarget(
                label="windows / universal.exe (Wine)",
                os_name="windows",
                tier="universal",
                rel_bin=f"src/bin/windows/{ENGINE_NAME}-universal.exe",
                expected_file_substr="PE32+ executable",
                expected_arch="SSE4.2",
                expected_simd="SSE4.2",
                expected_pext="Magic Bitboards",
                use_wine=True,
            ),
            TestTarget(
                label="windows / x86-64.exe (Wine)",
                os_name="windows",
                tier="x86-64",
                rel_bin=f"src/bin/windows/{ENGINE_NAME}-x86-64.exe",
                expected_file_substr="PE32+ executable",
                expected_arch="x86-64",
                expected_simd="Scalar / SSE2",
                expected_pext="Magic Bitboards",
                use_wine=True,
            ),
            TestTarget(
                label="windows / x86-64-v2.exe (Wine)",
                os_name="windows",
                tier="x86-64-v2",
                rel_bin=f"src/bin/windows/{ENGINE_NAME}-x86-64-v2.exe",
                expected_file_substr="PE32+ executable",
                expected_arch="SSE4.2",
                expected_simd="SSE4.2",
                expected_pext="Magic Bitboards",
                use_wine=True,
            ),
            TestTarget(
                label="windows / x86-64-v3.exe (Wine AVX2-BMI2)",
                os_name="windows",
                tier="x86-64-v3",
                rel_bin=f"src/bin/windows/{ENGINE_NAME}-x86-64-v3.exe",
                expected_file_substr="PE32+ executable",
                expected_arch="AVX2-BMI2",
                expected_simd="AVX2",
                expected_pext="BMI2 PEXT",
                use_wine=True,
            ),
        ]

    return targets


def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-platform GOOB verification matrix")
    parser.add_argument("--depth", type=int, default=8, help="Bench depth (default: 8)")
    parser.add_argument("--only", default="mac,linux,windows", help="Comma-separated platforms: mac,linux,windows")
    parser.add_argument("--bench-only", nargs=2, metavar=("PLATFORM", "TIER"), help="Run tools/sprt/bench.py loop on one target")
    parser.add_argument("--cpu", default=None, help="Optional QEMU CPU model for --bench-only")
    args = parser.parse_args()

    if args.bench_only:
        return run_sprt_bench_only(args.bench_only[0], args.bench_only[1], args.depth, args.cpu)

    selected = {p.strip().lower() for p in args.only.split(",") if p.strip()}
    if "win" in selected:
        selected.add("windows")
    if "macos" in selected:
        selected.add("mac")

    targets = build_matrix(selected)
    if not targets:
        print("No test targets selected.", file=sys.stderr)
        return 1

    print(f"=== GOOB Cross-Platform Test Matrix (bench depth={args.depth}, {len(targets)} configurations) ===\n")

    ref_eval: Optional[int] = None
    ref_nodes: Optional[int] = None
    ref_label: Optional[str] = None
    failures: List[tuple[str, str]] = []
    results = []

    for idx, t in enumerate(targets, 1):
        print(f"[{idx}/{len(targets)}] {t.label}")
        t0 = time.monotonic()
        try:
            check_file_format(t)
            info = check_compiler_info(t)
            tt_passed = check_tt_tests(t)
            eval_val, perft_nodes, smp_bm = check_uci_perft_eval_smp(t)
            bench_nodes, bench_nps = check_bench(t, args.depth)

            if ref_eval is None:
                ref_eval = eval_val
                ref_nodes = bench_nodes
                ref_label = t.label
            else:
                if eval_val != ref_eval:
                    raise AssertionError(f"eval drift vs {ref_label}: got {eval_val}, expected {ref_eval}")
                if bench_nodes != ref_nodes:
                    raise AssertionError(f"bench node drift vs {ref_label}: got {bench_nodes}, expected {ref_nodes}")

            dt = time.monotonic() - t0
            print(
                f"    PASS ({dt:.1f}s) | {info.get('Detected CPU Architecture')} / {info.get('Active SIMD Kernel')} | "
                f"TT={tt_passed} | perft4={perft_nodes} | eval={eval_val} | "
                f"bench{args.depth}={bench_nodes} ({bench_nps} nps) | SMP bm={smp_bm}"
            )
            results.append((t.label, "PASS", bench_nodes, eval_val, dt))
        except Exception as e:
            dt = time.monotonic() - t0
            print(f"    FAIL ({dt:.1f}s) | {e}")
            failures.append((t.label, str(e)))
            results.append((t.label, "FAIL", 0, 0, dt))

    # Verify build + binary format of AVX-512 (x86-64-v4) builds.
    # Neither Rosetta 2 nor QEMU 10 TCG emulates AVX-512 execution yet, so we
    # verify compilation, linking, and binary format (plus `compiler` for Linux).
    build_only_checks = []
    if "linux" in selected:
        build_only_checks.append(("linux / x86-64-v4 (AVX-512)", f"src/bin/linux/{ENGINE_NAME}-x86-64-v4", "ELF 64-bit LSB"))
    if "windows" in selected:
        build_only_checks.append(("windows / x86-64-v4.exe (AVX-512)", f"src/bin/windows/{ENGINE_NAME}-x86-64-v4.exe", "PE32+ executable"))

    for label, rel_bin, substr in build_only_checks:
        bin_path = REPO_ROOT / rel_bin
        if not bin_path.is_file():
            print(f"[build-check] {label} -> FAIL (missing {bin_path})")
            failures.append((label, f"missing binary: {bin_path}"))
            continue
        desc = run_cmd(["file", "-b", str(bin_path)], timeout=15).stdout.strip()
        ok = substr in desc
        status = "BUILD-OK (AVX-512 exec unsupported on Rosetta/QEMU TCG)" if ok else "FAIL"
        print(f"[build-check] {label} -> {status}")
        if not ok:
            failures.append((label, f"bad format: {desc}"))

    print("\n=== Summary ===")
    print(f"Passed: {len(targets) - len(failures)}/{len(targets)} | Reference eval={ref_eval} | Reference bench({args.depth}) nodes={ref_nodes}")
    if failures:
        print("\nFailures:")
        for label, err in failures:
            print(f"  - {label}: {err}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
