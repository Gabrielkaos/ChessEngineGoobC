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
