"""
Data models for the Goob Chess Engine Research Laboratory.
"""

from typing import List, Optional, Dict, Any
from pydantic import BaseModel, Field


class EngineInfo(BaseModel):
    id: str
    name: str
    path: str
    build_type: str = "release"  # release, trace, tune, legacy, custom
    version: str = "Unknown"
    is_available: bool = True
    options: Dict[str, Any] = Field(default_factory=dict)


class LegalMove(BaseModel):
    uci: str
    san: str
    from_sq: str
    to_sq: str
    is_capture: bool
    is_check: bool
    is_promotion: bool


class PositionState(BaseModel):
    fen: str
    is_valid: bool
    turn: str
    fullmove: int
    halfmove: int
    is_check: bool
    is_checkmate: bool
    is_stalemate: bool
    is_insufficient_material: bool
    legal_moves: List[LegalMove] = Field(default_factory=list)


class PVLine(BaseModel):
    multipv: int = 1
    depth: int = 0
    seldepth: int = 0
    score_type: str = "cp"  # cp or mate
    score_value: int = 0
    score_formatted: str = "+0.00"
    wdl: Optional[Dict[str, float]] = None  # win, draw, loss probabilities
    nodes: int = 0
    nps: int = 0
    time_ms: int = 0
    pv_uci: List[str] = Field(default_factory=list)
    pv_san: List[str] = Field(default_factory=list)


class SearchIteration(BaseModel):
    depth: int
    seldepth: int
    score_cp: Optional[int] = None
    score_mate: Optional[int] = None
    nodes: int
    nps: int
    time_ms: int
    best_move_uci: str
    best_move_san: Optional[str] = None
    pv: List[str] = Field(default_factory=list)


class AnalysisRequest(BaseModel):
    fen: str = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    engine_id: Optional[str] = None
    depth: Optional[int] = None
    movetime_ms: Optional[int] = None
    nodes: Optional[int] = None
    multipv: int = 1
    hash_mb: int = 64
    threads: int = 1


class CutoffDistribution(BaseModel):
    total: int = 0
    first_move: int = 0
    tt_move: int = 0
    killer: int = 0
    counter: int = 0
    followup: int = 0
    quiet: int = 0
    noisy: int = 0


class HeuristicMetric(BaseModel):
    name: str
    considered: Optional[int] = None
    triggered: Optional[int] = None
    rate: Optional[float] = None
    status: str = "normal"  # normal, low, high, info
    advice: Optional[str] = None
    constants: Optional[str] = None


class TraceReport(BaseModel):
    trace_enabled: bool = False
    error: Optional[str] = None
    mode: str = "current"
    searches_count: int = 0
    ab_nodes: int = 0
    qs_nodes: int = 0
    total_nodes: int = 0
    eval_calls: int = 0
    nnue_refresh_mg: int = 0
    nnue_refresh_eg: int = 0
    tt_hits: int = 0
    tt_cutoffs: int = 0
    tt_research_cutoffs: int = 0
    syzygy_probes: int = 0
    syzygy_hits: int = 0
    cutoffs: CutoffDistribution = Field(default_factory=CutoffDistribution)
    heuristics: Dict[str, Any] = Field(default_factory=dict)
    diagnostic_items: List[HeuristicMetric] = Field(default_factory=list)


class AnalysisResult(BaseModel):
    id: Optional[int] = None
    fen: str
    title: str = "Analysis"
    engine_id: str
    engine_name: str
    depth: int = 0
    best_move_uci: str = ""
    best_move_san: str = ""
    score_type: str = "cp"
    score_value: int = 0
    score_formatted: str = "+0.00"
    lines: List[PVLine] = Field(default_factory=list)
    iterations: List[SearchIteration] = Field(default_factory=list)
    trace: Optional[TraceReport] = None
    created_at: Optional[str] = None


class MatchConfig(BaseModel):
    name: str = "match_run"
    engine_new_id: str
    engine_base_id: str
    tc: str = "6+0.06"
    rounds: int = 50
    concurrency: int = 4
    hash_mb: int = 32
    threads: int = 1
    book: str = "tools/book.epd"
    is_sprt: bool = True
    elo0: float = 0.0
    elo1: float = 5.0
    alpha: float = 0.05
    beta: float = 0.05
    rating_interval: int = 10


class MatchResult(BaseModel):
    id: str
    name: str
    status: str = "pending"  # pending, running, completed, stopped, failed
    engine_new_name: str
    engine_base_name: str
    tc: str
    concurrency: int
    games_played: int = 0
    wins: int = 0
    losses: int = 0
    draws: int = 0
    score_pct: float = 50.0
    elo_diff: Optional[float] = None
    elo_error: Optional[float] = None
    sprt_status: Optional[str] = None  # continuing, accepted_h1, accepted_h0
    sprt_llr: Optional[float] = None
    sprt_lbound: Optional[float] = None
    sprt_ubound: Optional[float] = None
    log_file: Optional[str] = None
    pgn_file: Optional[str] = None
    created_at: Optional[str] = None
    finished_at: Optional[str] = None


class SPSAParameter(BaseModel):
    name: str
    default: int
    tuned: int
    theta: float
    change: int
    min_val: int
    max_val: int
    c_end: Optional[float] = None
    r_end: Optional[float] = None


class SPSARunConfig(BaseModel):
    name: str
    engine_path: Optional[str] = None
    iterations: int = 5000
    pairs: int = 2
    tc: str = "5+0.05"
    workers: int = 4
    hash_mb: int = 16
    book: str = "tools/book.epd"
    include: Optional[str] = None
    exclude: Optional[str] = None


class SPSARunSummary(BaseModel):
    name: str
    status: str = "idle"  # idle, running, completed, stopped
    iterations_completed: int = 0
    iterations_total: int = 0
    games_played: int = 0
    tc: str = "5+0.05"
    elapsed_hours: float = 0.0
    params: List[SPSAParameter] = Field(default_factory=list)
    recent_history: List[Dict[str, Any]] = Field(default_factory=list)


class ExperimentRecord(BaseModel):
    id: str
    title: str
    exp_type: str  # match, sprt, spsa, trace, bench, analysis
    status: str = "completed"
    created_at: str
    finished_at: Optional[str] = None
    git_commit: Optional[str] = None
    binary_name: Optional[str] = None
    settings: Dict[str, Any] = Field(default_factory=dict)
    results: Dict[str, Any] = Field(default_factory=dict)
    log_path: Optional[str] = None
    pgn_path: Optional[str] = None
    notes: Optional[str] = None
