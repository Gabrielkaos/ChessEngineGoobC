"""
Unit tests for SPSA tuning manager, parameter state inspection, and history parsing.
"""

import os
import pytest
from research_lab.spsa_manager import spsa_manager


def test_list_spsa_runs():
    runs = spsa_manager.list_runs()
    assert isinstance(runs, list)
    # The repository already contains runs like pruning5, pruning1, etc.
    assert "pruning5" in runs or len(runs) >= 1


def test_get_spsa_run_summary():
    runs = spsa_manager.list_runs()
    if not runs:
        pytest.skip("No SPSA runs found in tools/spsa/runs")

    summary = spsa_manager.get_run_summary("pruning5")
    if not summary:
        summary = spsa_manager.get_run_summary(runs[0])

    assert summary is not None
    assert summary.name in runs
    assert summary.iterations_total > 0
    assert len(summary.params) > 0

    # Verify parameter structure
    p0 = summary.params[0]
    assert p0.name != ""
    assert p0.min_val <= p0.max_val
    assert isinstance(p0.default, int)
    assert isinstance(p0.tuned, int)


def test_get_cutechess_options():
    runs = spsa_manager.list_runs()
    if "pruning5" in runs:
        opts = spsa_manager.get_cutechess_options("pruning5")
        assert "option." in opts
        assert "DeltaMarginQ=" in opts
