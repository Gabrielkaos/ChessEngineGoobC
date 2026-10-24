"""
Integration tests for FastAPI endpoints in Goob Chess Engine Research Laboratory.
"""

import pytest
from fastapi.testclient import TestClient
from research_lab.app import app

client = TestClient(app)


def test_get_index():
    response = client.get("/")
    assert response.status_code == 200
    assert "text/html" in response.headers.get("content-type", "") or "application/json" in response.headers.get("content-type", "")


def test_api_engines():
    response = client.get("/api/engines")
    assert response.status_code == 200
    engines = response.json()
    assert isinstance(engines, list)
    assert len(engines) >= 1
    # Check that at least one Goob native binary is listed
    names = [e["name"] for e in engines]
    assert any("GOOB" in n for n in names)


def test_position_validate_startpos():
    response = client.post("/api/position/validate", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    })
    assert response.status_code == 200
    data = response.json()
    assert data["is_valid"] is True
    assert data["turn"] == "w"
    assert data["fullmove"] == 1
    assert data["is_check"] is False
    assert len(data["legal_moves"]) == 20  # 16 pawn + 4 knight moves


def test_position_validate_invalid():
    response = client.post("/api/position/validate", json={
        "fen": "invalid_fen_string"
    })
    assert response.status_code == 200
    data = response.json()
    assert data["is_valid"] is False
    assert len(data["legal_moves"]) == 0


def test_position_make_move():
    # Valid move e2e4 from startpos
    response = client.post("/api/position/make-move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "move": "e2e4"
    })
    assert response.status_code == 200
    data = response.json()
    assert data["san"] == "e4"
    assert data["uci"] == "e2e4"
    assert "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq" in data["fen"]
    assert data["turn"] == "b"

    # Promotion defaulting to queen
    promo_resp = client.post("/api/position/make-move", json={
        "fen": "8/4P3/8/8/8/8/8/k6K w - - 0 1",
        "move": "e7e8"
    })
    assert promo_resp.status_code == 200
    promo_data = promo_resp.json()
    assert promo_data["san"] == "e8=Q"
    assert promo_data["uci"] == "e7e8q"

    # Illegal move
    illegal_resp = client.post("/api/position/make-move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "move": "e2e5"
    })
    assert illegal_resp.status_code == 400


def test_parse_pgn():
    sample_pgn = """[Event "Casual Game"]
[Site "Localhost"]
[Date "2026.10.24"]
[White "Goob"]
[Black "Baseline"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 1-0"""

    response = client.post("/api/position/parse-pgn", json={"pgn": sample_pgn})
    assert response.status_code == 200
    data = response.json()
    assert data["headers"]["White"] == "Goob"
    assert len(data["positions"]) == 9  # Start + 8 half-moves


def test_api_spsa_runs():
    response = client.get("/api/spsa/runs")
    assert response.status_code == 200
    runs = response.json()
    assert isinstance(runs, list)
    if "pruning5" in runs:
        resp_details = client.get("/api/spsa/runs/pruning5")
        assert resp_details.status_code == 200
        details = resp_details.json()
        assert details["name"] == "pruning5"
        assert len(details["params"]) > 0


def test_api_experiments_crud():
    # List
    response = client.get("/api/experiments")
    assert response.status_code == 200
    assert isinstance(response.json(), list)


def test_api_tournaments_list():
    response = client.get("/api/tournaments")
    assert response.status_code == 200
    assert isinstance(response.json(), list)


def test_api_position_trace():
    response = client.post("/api/position/trace", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "depth": 4
    })
    assert response.status_code == 200
    data = response.json()
    assert "trace_enabled" in data


def test_api_engine_import():
    # Valid import of local binary
    response = client.post("/api/engines/import", json={
        "path": "bin/GOOB-2.1-linux-x64",
        "name": "Custom Goob 2.1"
    })
    assert response.status_code == 200
    data = response.json()
    assert data["name"] == "Custom Goob 2.1"
    assert data["build_type"] == "custom"

    # Non-existent binary
    bad_resp = client.post("/api/engines/import", json={
        "path": "non_existent_engine_binary"
    })
    assert bad_resp.status_code == 400
