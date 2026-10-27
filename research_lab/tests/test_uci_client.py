"""
Unit tests for UCI engine client, parser, and lifecycle.
"""

import os
import pytest
from research_lab.uci_client import (
    parse_uci_info_line, extract_trace_metrics, UCIEngineProcess,
    discover_engines
)

STARTPOS_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NATIVE_BIN = os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native")


def test_parse_uci_info_cp():
    line = "info depth 6 seldepth 8 multipv 1 score cp 25 time 15 nodes 1943 pv e2e4 e7e5 g1f3 b8c6"
    pv = parse_uci_info_line(line, STARTPOS_FEN)
    assert pv is not None
    assert pv.depth == 6
    assert pv.seldepth == 8
    assert pv.multipv == 1
    assert pv.score_type == "cp"
    assert pv.score_value == 25
    assert pv.score_formatted == "+0.25"
    assert pv.nodes == 1943
    assert pv.time_ms == 15
    assert pv.pv_uci == ["e2e4", "e7e5", "g1f3", "b8c6"]
    # Check SAN translation
    assert pv.pv_san == ["e4", "e5", "Nf3", "Nc6"]
    # WDL should be present
    assert pv.wdl is not None
    assert pv.wdl["win"] > 0
    assert pv.wdl["draw"] > 0
    assert pv.wdl["loss"] > 0


def test_parse_uci_info_mate():
    line = "info depth 5 seldepth 7 multipv 1 score mate 3 time 2 nodes 500 pv f7f8q"
    pv = parse_uci_info_line(line, STARTPOS_FEN)
    assert pv is not None
    assert pv.score_type == "mate"
    assert pv.score_value == 3
    assert pv.score_formatted == "#+3"
    assert pv.wdl == {"win": 1.0, "draw": 0.0, "loss": 0.0}


def test_extract_trace_metrics():
    mock_json = {
        "trace_enabled": True,
        "mode": "current",
        "searches_count": 1,
        "ab_nodes": 1000,
        "qs_nodes": 500,
        "total_nodes": 1500,
        "eval_calls": 1200,
        "tt_hits": 200,
        "tt_cutoffs": 50,
        "heuristics": {
            "beta_pruning": {"considered": 500, "triggered": 150, "rate": 0.30},
            "nmp": {"attempted": 200, "direct_cutoffs": 120, "rate": 0.60},
            "razoring": {"attempted": 300, "cutoffs": 15, "rate": 0.05}
        },
        "cutoffs": {
            "total": 300,
            "first_move": 200,
            "tt_move": 50,
            "killer": 20,
            "counter": 10,
            "followup": 5,
            "quiet": 10,
            "noisy": 5
        }
    }
    report = extract_trace_metrics(mock_json)
    assert report.trace_enabled is True
    assert report.total_nodes == 1500
    assert report.cutoffs.first_move == 200
    assert len(report.diagnostic_items) >= 3

    # Check that normal rate status is marked normal
    rfp_item = next(it for it in report.diagnostic_items if "Beta Pruning" in it.name)
    assert rfp_item.status == "normal"


def test_uci_engine_lifecycle():
    if not os.path.exists(NATIVE_BIN) or not os.access(NATIVE_BIN, os.X_OK):
        pytest.skip("Goob native binary not found")

    proc = UCIEngineProcess(NATIVE_BIN)
    try:
        proc.start()
        assert proc.is_running is True

        res = proc.analyze(STARTPOS_FEN, depth=4, multipv=1)
        assert res.depth >= 4
        assert res.best_move_uci != ""
        assert len(res.lines) >= 1
        assert len(res.iterations) >= 1
    finally:
        proc.quit()
        assert proc.is_running is False


def test_engine_perft_endgame_tt_and_eval():
    if not os.path.exists(NATIVE_BIN) or not os.access(NATIVE_BIN, os.X_OK):
        pytest.skip("Goob native binary not found")

    proc = UCIEngineProcess(NATIVE_BIN)
    try:
        proc.start()

        # 1. Built-in C TT bucket replacement & 65-search generation wrap-around unit test
        proc.send("test")
        proc.send("isready")
        tt_out = []
        while True:
            line = proc.readline()
            if not line or "readyok" in line:
                break
            tt_out.append(line.strip())
        assert any("All TT tests PASSED" in l for l in tt_out)

        # 2. 5 tricky perft FENs (EP pins, EP check, castling, promotion out of check, underpromotion)
        tricky_perfts = [
            ("3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888),
            ("8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467),
            ("r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206),
            ("2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001),
            ("8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683),
        ]
        for fen, depth, expected_nodes in tricky_perfts:
            proc.send(f"position fen {fen}")
            proc.send(f"uperft {depth}")
            proc.send("isready")
            nodes = None
            while True:
                line = proc.readline()
                if not line or "readyok" in line:
                    break
                if line.startswith("Nodes:"):
                    nodes = int(line.split(":")[1].strip())
            assert nodes == expected_nodes, f"Perft mismatch on {fen}: got {nodes}, expected {expected_nodes}"

        # 3. UCILoop eval state preservation across MirrorBoard / HMirrorBoard
        test_fen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"
        proc.send(f"position fen {test_fen}")
        evals = []
        for _ in range(2):
            proc.send("eval")
            proc.send("isready")
            while True:
                line = proc.readline()
                if not line or "readyok" in line:
                    break
                if line.startswith("Eval:"):
                    evals.append(int(line.split(":")[1].strip()))
        assert len(evals) == 2 and evals[0] == evals[1] and evals[0] != 0

        # 4. K+RP vs K win vs corner fortress draw, and 65 consecutive searches
        win_res = proc.analyze("2k5/P7/1K6/8/8/8/8/8 w - - 0 1", depth=6)
        assert win_res.best_move_uci == "a7a8q"
        assert win_res.score_value > 500

        draw_res = proc.analyze("k7/P7/1K6/8/8/8/8/8 w - - 0 1", depth=6)
        assert abs(draw_res.score_value) <= 5

        for _ in range(65):
            proc.send("position fen 2k5/P7/1K6/8/8/8/8/8 w - - 0 1")
            proc.send("go depth 3")
            while True:
                line = proc.readline()
                if not line or line.startswith("bestmove"):
                    break
        after_65 = proc.analyze("2k5/P7/1K6/8/8/8/8/8 w - - 0 1", depth=6)
        assert after_65.best_move_uci == "a7a8q"
        assert after_65.score_value > 500
    finally:
        proc.quit()
