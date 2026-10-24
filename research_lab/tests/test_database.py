"""
Unit tests for the SQLite experiment and analysis database.
"""

import os
import tempfile
import pytest
from research_lab.database import (
    init_db, save_engine, list_engines, get_engine, delete_engine,
    save_analysis, list_analyses, get_analysis, delete_analysis,
    save_experiment, list_experiments, get_experiment, delete_experiment
)


@pytest.fixture
def temp_db():
    fd, path = tempfile.mkstemp(suffix=".db")
    os.close(fd)
    init_db(path)
    yield path
    if os.path.exists(path):
        os.remove(path)


def test_engines_crud(temp_db):
    eng = {
        "id": "test-goob",
        "name": "Test Goob 2.2",
        "path": "/bin/test",
        "build_type": "release",
        "version": "GOOB 2.2",
        "is_available": True,
        "options": {"Hash": {"default": 64}}
    }
    assert save_engine(eng, temp_db) is True
    retrieved = get_engine("test-goob", temp_db)
    assert retrieved is not None
    assert retrieved["name"] == "Test Goob 2.2"
    assert retrieved["options"]["Hash"]["default"] == 64

    all_engs = list_engines(temp_db)
    assert len(all_engs) == 1

    assert delete_engine("test-goob", temp_db) is True
    assert get_engine("test-goob", temp_db) is None


def test_analyses_crud(temp_db):
    analysis_data = {
        "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        "title": "Open Game Analysis",
        "engine_id": "test-goob",
        "depth": 14,
        "best_move_uci": "e7e5",
        "best_move_san": "e5",
        "score_type": "cp",
        "score_value": 30,
        "score_formatted": "+0.30",
        "lines": [{"multipv": 1, "score_formatted": "+0.30"}],
        "iterations": [{"depth": 14, "score_cp": 30}],
        "notes": "Solid position"
    }
    new_id = save_analysis(analysis_data, temp_db)
    assert new_id > 0

    item = get_analysis(new_id, temp_db)
    assert item is not None
    assert item["title"] == "Open Game Analysis"
    assert item["depth"] == 14
    assert len(item["lines"]) == 1

    items = list_analyses(limit=10, db_path=temp_db)
    assert len(items) == 1

    assert delete_analysis(new_id, temp_db) is True
    assert get_analysis(new_id, temp_db) is None


def test_experiments_crud(temp_db):
    exp = {
        "id": "exp-sprt-001",
        "title": "SPRT Goob vs Baseline",
        "exp_type": "sprt",
        "status": "completed",
        "binary_name": "GOOB-2.2-BETA-native",
        "settings": {"tc": "6+0.06", "rounds": 100},
        "results": {"wins": 45, "losses": 30, "draws": 25, "elo_diff": 52.3},
        "notes": "Passed H1"
    }
    assert save_experiment(exp, temp_db) is True
    retrieved = get_experiment("exp-sprt-001", temp_db)
    assert retrieved is not None
    assert retrieved["results"]["elo_diff"] == 52.3

    filtered = list_experiments(exp_type="sprt", db_path=temp_db)
    assert len(filtered) == 1

    empty_filter = list_experiments(exp_type="bench", db_path=temp_db)
    assert len(empty_filter) == 0

    assert delete_experiment("exp-sprt-001", temp_db) is True
    assert get_experiment("exp-sprt-001", temp_db) is None
