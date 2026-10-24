"""
Unit tests for Cutechess-cli tournament log parsing and match orchestration.
"""

import re
import pytest
from research_lab.models import MatchConfig
from research_lab.tournament_runner import runner


def test_cutechess_output_parsing():
    score_pattern = re.compile(r"Score of .*?:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)\s+\[([\d\.]+)\]\s+(\d+)")
    elo_pattern = re.compile(r"Elo difference:\s+([+-]?\d+\.?\d*)\s+\+/-\s+(\d+\.?\d*)")
    sprt_pattern = re.compile(r"SPRT:\s+llr\s+([+-]?\d+\.?\d*)\s+\(([\d\.]+)%\),\s+lbound\s+([+-]?\d+\.?\d*),\s+ubound\s+([+-]?\d+\.?\d*)")
    sprt_dec_pattern = re.compile(r"SPRT:\s+([a-zA-Z0-9\s]+(?:accepted|rejected|H0|H1)[a-zA-Z0-9\s]*)")

    line1 = "Score of NEW vs BASE: 28 - 14 - 38 [0.588] 80"
    m1 = score_pattern.search(line1)
    assert m1 is not None
    assert int(m1.group(1)) == 28
    assert int(m1.group(2)) == 14
    assert int(m1.group(3)) == 38
    assert float(m1.group(4)) == 0.588
    assert int(m1.group(5)) == 80

    line2 = "Elo difference: 61.4 +/- 45.2, LOS: 98.2 %, DrawRatio: 47.5 %"
    m2 = elo_pattern.search(line2)
    assert m2 is not None
    assert float(m2.group(1)) == 61.4
    assert float(m2.group(2)) == 45.2

    line3 = "SPRT: llr 1.45 (49.3%), lbound -2.94, ubound 2.94"
    m3 = sprt_pattern.search(line3)
    assert m3 is not None
    assert float(m3.group(1)) == 1.45
    assert float(m3.group(3)) == -2.94
    assert float(m3.group(4)) == 2.94

    line4 = "SPRT: accepted H1"
    m4 = sprt_dec_pattern.search(line4)
    assert m4 is not None
    assert "accepted H1" in m4.group(1)


def test_match_config_defaults():
    cfg = MatchConfig(
        engine_new_id="goob-native",
        engine_base_id="goob-2.1"
    )
    assert cfg.tc == "6+0.06"
    assert cfg.rounds == 50
    assert cfg.concurrency == 4
    assert cfg.is_sprt is True
    assert cfg.elo0 == 0.0
    assert cfg.elo1 == 5.0
