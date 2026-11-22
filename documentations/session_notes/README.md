# Cross-Machine Session Notes

Shared handoff notes for sessions running across different machines (macOS Apple Silicon, Linux, and Windows).

Use this folder to record platform-specific benchmark results, build/correctness checks, observations, and open follow-ups so sessions on another machine have full context after pulling `origin/main`.

## Naming Convention

`YYYY-MM-DD_<platform>_<short-topic>.md` (for example, `2026-11-22_mac_b0dfc92_opt_bench.md`).

## Index

| Date | Machine / OS | Commit(s) | Summary | Note |
| :--- | :--- | :--- | :--- | :--- |
| 2026-11-22 | MacBook Pro (Apple M5, macOS arm64) | `028b5ba` -> `b0dfc92` | Node-identical verified (`perfttest` 33/33 PASS); **+2.23% to +2.49%** search NPS, **+22.5%** perft NPS; flagged NEON fused-kernel 2-chain bottleneck | [2026-11-22_mac_b0dfc92_opt_bench.md](./2026-11-22_mac_b0dfc92_opt_bench.md) |
