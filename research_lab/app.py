"""
FastAPI Backend Application for Goob Chess Engine Research Laboratory.
Provides REST and WebSocket endpoints for position analysis, search diagnostics,
tournaments, SPSA tuning, and the experiment database.
"""

import os
import io
import json
import re
import subprocess
import asyncio
from typing import Dict, Any, Optional, List
from datetime import datetime

import chess
import chess.pgn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException, Query
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from .models import (
    EngineInfo, LegalMove, PositionState, AnalysisRequest, AnalysisResult,
    MatchConfig, MatchResult, SPSARunConfig, SPSARunSummary,
    ExperimentRecord, TraceReport
)
from .database import (
    init_db, list_engines, save_engine, get_engine, delete_engine,
    save_analysis, list_analyses, get_analysis, delete_analysis,
    save_experiment, list_experiments, get_experiment, delete_experiment
)
from .uci_client import (
    discover_engines, UCIEngineProcess, AsyncUCIEngine, parse_uci_info_line,
    extract_trace_metrics
)
from .tournament_runner import runner
from .spsa_manager import spsa_manager

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")

# Initialize database
init_db()

# Auto-register discovered engines
for eng in discover_engines():
    save_engine(eng.model_dump())

app = FastAPI(title="Goob Chess Engine Research Laboratory", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/favicon.ico", include_in_schema=False)
def favicon():
    return JSONResponse(status_code=204, content={})


# ---------------------------------------------------------------------------
# Engine Endpoints
# ---------------------------------------------------------------------------

class ImportEngineRequest(BaseModel):
    path: str
    name: Optional[str] = None


@app.get("/api/engines", response_model=List[EngineInfo])
def get_engines():
    """Lists all available engines and scans for newly built binaries."""
    for eng in discover_engines():
        save_engine(eng.model_dump())
    return [EngineInfo(**e) for e in list_engines()]


@app.post("/api/engines", response_model=EngineInfo)
def register_engine(engine: EngineInfo):
    if not os.path.exists(engine.path):
        raise HTTPException(status_code=400, detail="Engine binary does not exist at specified path")
    save_engine(engine.model_dump())
    return engine


@app.post("/api/engines/import", response_model=EngineInfo)
def import_custom_engine(req: ImportEngineRequest):
    bin_path = os.path.expanduser(req.path.strip())
    if not os.path.isabs(bin_path):
        bin_path = os.path.abspath(os.path.join(ROOT_DIR, bin_path))

    if not os.path.exists(bin_path):
        raise HTTPException(status_code=400, detail=f"File not found: {bin_path}")
    if not os.access(bin_path, os.X_OK):
        raise HTTPException(status_code=400, detail=f"File is not executable: {bin_path}")

    detected_name = ""
    options = {}
    try:
        proc = subprocess.run(
            [bin_path],
            input="uci\nquit\n",
            capture_output=True,
            text=True,
            timeout=5
        )
        for line in proc.stdout.splitlines():
            if line.startswith("id name "):
                detected_name = line[8:].strip()
            elif line.startswith("option name "):
                m = re.match(r"option name (.*?) type (.*?)(?: default (.*?))?(?: min (\d+))?(?: max (\d+))?$", line)
                if m:
                    options[m.group(1)] = {
                        "type": m.group(2),
                        "default": m.group(3),
                        "min": int(m.group(4)) if m.group(4) else None,
                        "max": int(m.group(5)) if m.group(5) else None,
                    }
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Engine failed UCI handshake: {str(e)}")

    engine_name = req.name.strip() if req.name and req.name.strip() else detected_name
    if not engine_name:
        engine_name = os.path.basename(bin_path)

    eng_id = re.sub(r'[^a-zA-Z0-9_\-]', '-', engine_name.lower())
    eng_info = EngineInfo(
        id=eng_id,
        name=engine_name,
        path=bin_path,
        build_type="custom",
        version=detected_name or "Custom",
        is_available=True,
        options=options
    )
    save_engine(eng_info.model_dump())
    return eng_info


@app.delete("/api/engines/{engine_id}")
def remove_engine(engine_id: str):
    success = delete_engine(engine_id)
    if not success:
        raise HTTPException(status_code=404, detail="Engine not found")
    return {"status": "deleted"}


# ---------------------------------------------------------------------------
# Position Validation & PGN Parsing
# ---------------------------------------------------------------------------

class FenValidationRequest(BaseModel):
    fen: str


class MakeMoveRequest(BaseModel):
    fen: str
    move: str  # uci or uci+promotion, e.g. "e2e4" or "e7e8q"


class PgnParseRequest(BaseModel):
    pgn: str


@app.post("/api/position/validate", response_model=PositionState)
def validate_fen(req: FenValidationRequest):
    fen_str = req.fen.strip()
    try:
        board = chess.Board(fen_str)
    except ValueError as e:
        return PositionState(
            fen=fen_str,
            is_valid=False,
            turn="w",
            fullmove=1,
            halfmove=0,
            is_check=False,
            is_checkmate=False,
            is_stalemate=False,
            is_insufficient_material=False,
            legal_moves=[]
        )

    legal_moves = []
    for m in board.legal_moves:
        legal_moves.append(LegalMove(
            uci=m.uci(),
            san=board.san(m),
            from_sq=chess.square_name(m.from_square),
            to_sq=chess.square_name(m.to_square),
            is_capture=board.is_capture(m),
            is_check=board.gives_check(m),
            is_promotion=m.promotion is not None
        ))

    return PositionState(
        fen=board.fen(),
        is_valid=True,
        turn="w" if board.turn == chess.WHITE else "b",
        fullmove=board.fullmove_number,
        halfmove=board.halfmove_clock,
        is_check=board.is_check(),
        is_checkmate=board.is_checkmate(),
        is_stalemate=board.is_stalemate(),
        is_insufficient_material=board.is_insufficient_material(),
        legal_moves=legal_moves
    )


@app.post("/api/position/make-move")
def make_move(req: MakeMoveRequest):
    try:
        b = chess.Board(req.fen)
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid FEN string")

    try:
        m = chess.Move.from_uci(req.move)
        if m not in b.legal_moves:
            # Check if pawn promotion defaulting to queen
            m_q = chess.Move.from_uci(req.move + "q")
            if m_q in b.legal_moves:
                m = m_q
            else:
                raise HTTPException(status_code=400, detail=f"Move {req.move} is not legal in this position")
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid move format: {req.move}")

    san = b.san(m)
    b.push(m)

    return {
        "fen": b.fen(),
        "san": san,
        "uci": m.uci(),
        "is_check": b.is_check(),
        "is_checkmate": b.is_checkmate(),
        "is_stalemate": b.is_stalemate(),
        "turn": "w" if b.turn == chess.WHITE else "b"
    }


@app.post("/api/position/parse-pgn")
def parse_pgn(req: PgnParseRequest):
    try:
        pgn_io = io.StringIO(req.pgn)
        game = chess.pgn.read_game(pgn_io)
        if not game:
            raise HTTPException(status_code=400, detail="Invalid PGN text")

        headers = dict(game.headers)
        positions = []
        board = game.board()

        positions.append({
            "ply": 0,
            "move_san": "Start",
            "move_uci": "",
            "fen": board.fen()
        })

        ply = 1
        for move in game.mainline_moves():
            san = board.san(move)
            uci = move.uci()
            board.push(move)
            positions.append({
                "ply": ply,
                "move_san": san,
                "move_uci": uci,
                "fen": board.fen()
            })
            ply += 1

        return {
            "headers": headers,
            "positions": positions
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Failed to parse PGN: {str(e)}")


# ---------------------------------------------------------------------------
# Synchronous Analysis & Trace
# ---------------------------------------------------------------------------

@app.post("/api/position/analyze", response_model=AnalysisResult)
def run_analysis(req: AnalysisRequest):
    eng_info = None
    if req.engine_id:
        eng_info = get_engine(req.engine_id)
    if not eng_info:
        # Default to native or first available
        engines = list_engines()
        if not engines:
            raise HTTPException(status_code=500, detail="No engines available")
        eng_info = next((e for e in engines if e["id"] == "goob-native"), engines[0])

    proc = UCIEngineProcess(eng_info["path"])
    try:
        result = proc.analyze(
            fen=req.fen,
            depth=req.depth or 10,
            movetime_ms=req.movetime_ms,
            multipv=req.multipv,
            hash_mb=req.hash_mb,
            threads=req.threads
        )
        return result
    finally:
        proc.quit()


@app.post("/api/position/trace", response_model=TraceReport)
def run_trace(req: AnalysisRequest):
    """Runs a trace analysis using the trace-enabled engine build."""
    trace_eng = None
    for eng in list_engines():
        if eng["build_type"] == "trace" and eng["is_available"]:
            trace_eng = eng
            break

    if not trace_eng:
        # Fallback to any available
        engines = list_engines()
        if not engines:
            raise HTTPException(status_code=500, detail="No engine available")
        trace_eng = engines[0]

    proc = UCIEngineProcess(trace_eng["path"])
    try:
        proc.start()
        proc.send("ucinewgame")
        proc.send(f"position fen {req.fen}")
        proc.send(f"go depth {req.depth or 8}")
        while True:
            line = proc.readline()
            if not line or line.startswith("bestmove"):
                break
        raw_trace = proc.query_trace()
        if not raw_trace:
            return TraceReport(trace_enabled=False, error="Engine did not return trace JSON")
        return extract_trace_metrics(raw_trace)
    finally:
        proc.quit()


@app.post("/api/position/trace-bench", response_model=TraceReport)
def run_trace_bench(depth: int = Query(6)):
    """Runs Goob's built-in benchmark suite to collect cumulative search trace statistics."""
    trace_eng = None
    for eng in list_engines():
        if eng["build_type"] == "trace" and eng["is_available"]:
            trace_eng = eng
            break

    if not trace_eng:
        raise HTTPException(status_code=500, detail="Trace engine not available")

    proc = UCIEngineProcess(trace_eng["path"])
    try:
        proc.start()
        proc.send(f"trace bench {depth}")
        while True:
            line = proc.readline()
            if not line or "CUTOFF DISTRIBUTION" in line:
                break
        proc.send("trace json")
        proc.send("isready")
        collected_lines = []
        while True:
            line = proc.readline()
            if not line or "readyok" in line:
                break
            collected_lines.append(line.strip())

        full_text = "\n".join(collected_lines).strip()
        start = full_text.find("{")
        end = full_text.rfind("}")
        if start != -1 and end != -1 and end > start:
            raw_trace = json.loads(full_text[start:end+1])
            return extract_trace_metrics(raw_trace)
        return TraceReport(trace_enabled=False, error="Engine did not return trace JSON")
    finally:
        proc.quit()


# ---------------------------------------------------------------------------
# WebSocket Live Search Stream
# ---------------------------------------------------------------------------

@app.websocket("/ws/analysis")
async def websocket_analysis(websocket: WebSocket):
    await websocket.accept()
    active_engine: Optional[AsyncUCIEngine] = None
    search_task: Optional[asyncio.Task] = None

    async def cleanup():
        nonlocal search_task, active_engine
        if search_task and not search_task.done():
            search_task.cancel()
            try:
                await search_task
            except asyncio.CancelledError:
                pass
            search_task = None
        if active_engine:
            await active_engine.quit()
            active_engine = None

    try:
        while True:
            data = await websocket.receive_text()
            msg = json.loads(data)
            action = msg.get("action")

            if action == "start":
                await cleanup()

                fen = msg.get("fen", chess.STARTING_FEN)
                engine_id = msg.get("engine_id")
                depth = msg.get("depth", 15)
                multipv = msg.get("multipv", 1)
                hash_mb = msg.get("hash_mb", 64)
                threads = msg.get("threads", 1)

                eng_info = get_engine(engine_id) if engine_id else None
                if not eng_info:
                    engines = list_engines()
                    eng_info = next((e for e in engines if e["id"] == "goob-native"), engines[0] if engines else None)

                if not eng_info:
                    await websocket.send_json({"type": "error", "message": "No engine available"})
                    continue

                active_engine = AsyncUCIEngine(eng_info["path"])
                try:
                    await active_engine.start()
                except Exception as e:
                    await websocket.send_json({"type": "error", "message": f"Failed to start engine: {str(e)}"})
                    active_engine = None
                    continue

                await active_engine.send(f"setoption name Hash value {hash_mb}")
                await active_engine.send(f"setoption name Threads value {threads}")
                await active_engine.send(f"setoption name MultiPV value {multipv}")
                await active_engine.send("isready")
                while True:
                    line = await active_engine.readline(timeout=3.0)
                    if not line or "readyok" in line:
                        break

                await active_engine.send("ucinewgame")
                await active_engine.send(f"position fen {fen}")

                go_cmd = "go"
                if depth:
                    go_cmd += f" depth {depth}"
                else:
                    go_cmd += " infinite"
                await active_engine.send(go_cmd)

                # Stream search lines asynchronously without thread blocking
                async def stream_output(engine: AsyncUCIEngine):
                    seen_depths = set()
                    while True:
                        line = await engine.readline(timeout=60.0)
                        if not line:
                            break
                        line_str = line.strip()
                        if line_str.startswith("bestmove"):
                            parts = line_str.split()
                            bm = parts[1] if len(parts) > 1 else ""
                            await websocket.send_json({"type": "bestmove", "bestmove": bm})
                            break

                        pv = parse_uci_info_line(line_str, fen)
                        if pv:
                            await websocket.send_json({"type": "pv", "data": pv.model_dump()})
                            if pv.multipv == 1 and pv.depth not in seen_depths:
                                seen_depths.add(pv.depth)
                                await websocket.send_json({
                                    "type": "iteration",
                                    "data": {
                                        "depth": pv.depth,
                                        "seldepth": pv.seldepth,
                                        "score_cp": pv.score_value if pv.score_type == "cp" else None,
                                        "score_mate": pv.score_value if pv.score_type == "mate" else None,
                                        "nodes": pv.nodes,
                                        "nps": pv.nps,
                                        "time_ms": pv.time_ms,
                                        "best_move_uci": pv.pv_uci[0] if pv.pv_uci else "",
                                        "best_move_san": pv.pv_san[0] if pv.pv_san else "",
                                        "pv": pv.pv_san
                                    }
                                })

                    # After bestmove, query trace if supported
                    try:
                        raw_trace = await engine.query_trace(timeout=0.6)
                        if raw_trace and raw_trace.get("trace_enabled", False):
                            tr = extract_trace_metrics(raw_trace)
                            await websocket.send_json({"type": "trace", "data": tr.model_dump()})
                    except Exception:
                        pass

                search_task = asyncio.create_task(stream_output(active_engine))

            elif action == "stop":
                if active_engine:
                    await active_engine.stop_search()
    except WebSocketDisconnect:
        pass
    finally:
        await cleanup()


# ---------------------------------------------------------------------------
# Saved Analyses CRUD
# ---------------------------------------------------------------------------

@app.get("/api/analyses")
def get_analyses_list(limit: int = Query(50)):
    return list_analyses(limit=limit)


@app.post("/api/analyses")
def create_analysis(data: Dict[str, Any]):
    new_id = save_analysis(data)
    return {"id": new_id, "status": "saved"}


@app.get("/api/analyses/{analysis_id}")
def get_single_analysis(analysis_id: int):
    res = get_analysis(analysis_id)
    if not res:
        raise HTTPException(status_code=404, detail="Analysis not found")
    return res


@app.delete("/api/analyses/{analysis_id}")
def delete_single_analysis(analysis_id: int):
    success = delete_analysis(analysis_id)
    if not success:
        raise HTTPException(status_code=404, detail="Analysis not found")
    return {"status": "deleted"}


# ---------------------------------------------------------------------------
# Tournaments & SPRT Matches
# ---------------------------------------------------------------------------

@app.post("/api/tournaments/start", response_model=MatchResult)
def start_tournament(config: MatchConfig):
    eng_new = get_engine(config.engine_new_id)
    eng_base = get_engine(config.engine_base_id)

    if not eng_new:
        raise HTTPException(status_code=400, detail=f"Engine {config.engine_new_id} not found")
    if not eng_base:
        raise HTTPException(status_code=400, detail=f"Engine {config.engine_base_id} not found")

    try:
        return runner.start_match(config, eng_new["path"], eng_base["path"])
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/tournaments", response_model=List[MatchResult])
def get_tournaments():
    matches = runner.list_all_matches()
    # Also fetch completed from db
    db_exps = list_experiments(exp_type="match") + list_experiments(exp_type="sprt")
    seen_ids = {m.id for m in matches}
    for exp in db_exps:
        if exp["id"] not in seen_ids and "results" in exp and exp["results"]:
            try:
                matches.append(MatchResult(**exp["results"]))
            except Exception:
                pass
    return matches


@app.get("/api/tournaments/{match_id}", response_model=MatchResult)
def get_tournament_status(match_id: str):
    st = runner.get_status(match_id)
    if not st:
        raise HTTPException(status_code=404, detail="Match not found")
    return st


@app.post("/api/tournaments/{match_id}/stop")
def stop_tournament(match_id: str):
    success = runner.stop_match(match_id)
    return {"status": "stopped" if success else "not running"}


@app.get("/api/tournaments/{match_id}/logs")
def get_tournament_logs(match_id: str):
    st = runner.get_status(match_id)
    if not st or not st.log_file or not os.path.exists(st.log_file):
        raise HTTPException(status_code=404, detail="Log file not found")
    with open(st.log_file, "r") as f:
        return {"logs": f.read(), "status": st.status}


@app.get("/api/tournaments/{match_id}/pgn")
def get_tournament_pgn(match_id: str):
    st = runner.get_status(match_id)
    if not st or not st.pgn_file or not os.path.exists(st.pgn_file):
        raise HTTPException(status_code=404, detail="PGN file not found")
    return FileResponse(st.pgn_file, media_type="text/plain", filename=f"{match_id}.pgn")


@app.websocket("/ws/tournament/{match_id}")
async def websocket_tournament_stream(websocket: WebSocket, match_id: str):
    await websocket.accept()
    queue: asyncio.Queue[str] = asyncio.Queue()

    def callback(line: str):
        queue.put_nowait(line)

    with runner.lock:
        runner.log_callbacks.setdefault(match_id, []).append(callback)

    try:
        # Replay any existing log content immediately so the user sees previous output
        st = runner.get_status(match_id)
        if st and st.log_file and os.path.exists(st.log_file):
            try:
                with open(st.log_file, "r") as f:
                    content = f.read()
                if content:
                    await websocket.send_json({"type": "history", "logs": content})
            except Exception:
                pass

        if st:
            await websocket.send_json({"type": "status", "data": st.model_dump()})

        while True:
            try:
                line = await asyncio.wait_for(queue.get(), timeout=0.5)
                await websocket.send_json({"type": "log", "line": line})
                while not queue.empty():
                    extra = queue.get_nowait()
                    await websocket.send_json({"type": "log", "line": extra})
            except asyncio.TimeoutError:
                pass

            st = runner.get_status(match_id)
            if st:
                await websocket.send_json({"type": "status", "data": st.model_dump()})
                if st.status in ("completed", "stopped", "failed") and queue.empty():
                    break
    except WebSocketDisconnect:
        pass
    finally:
        with runner.lock:
            if match_id in runner.log_callbacks and callback in runner.log_callbacks[match_id]:
                runner.log_callbacks[match_id].remove(callback)


# ---------------------------------------------------------------------------
# SPSA Parameter Tuning
# ---------------------------------------------------------------------------

@app.get("/api/spsa/runs")
def list_spsa_runs():
    return spsa_manager.list_runs()


@app.get("/api/spsa/runs/{name}", response_model=SPSARunSummary)
def get_spsa_run(name: str):
    summary = spsa_manager.get_run_summary(name)
    if not summary:
        raise HTTPException(status_code=404, detail="SPSA run not found")
    return summary


@app.get("/api/spsa/runs/{name}/options")
def get_spsa_cutechess_options(name: str):
    opts = spsa_manager.get_cutechess_options(name)
    return {"options": opts}


@app.post("/api/spsa/runs")
def start_spsa_run(config: SPSARunConfig):
    try:
        success = spsa_manager.start_run(config)
        if not success:
            raise HTTPException(status_code=400, detail="Run is already active")
        return {"status": "started", "name": config.name}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/api/spsa/runs/{name}/stop")
def stop_spsa_run(name: str):
    success = spsa_manager.stop_run(name)
    return {"status": "stopped" if success else "failed"}


@app.get("/api/spsa/runs/{name}/logs")
def get_spsa_logs(name: str, lines: int = Query(200)):
    logs = spsa_manager.get_logs(name, lines=lines)
    return {"logs": logs}


# ---------------------------------------------------------------------------
# Experiments Database
# ---------------------------------------------------------------------------

@app.get("/api/experiments", response_model=List[ExperimentRecord])
def get_experiments(exp_type: Optional[str] = None, status: Optional[str] = None, limit: int = 100):
    rows = list_experiments(exp_type=exp_type, status=status, limit=limit)
    return [ExperimentRecord(**r) for r in rows]


@app.get("/api/experiments/{exp_id}", response_model=ExperimentRecord)
def get_single_experiment(exp_id: str):
    r = get_experiment(exp_id)
    if not r:
        raise HTTPException(status_code=404, detail="Experiment not found")
    return ExperimentRecord(**r)


@app.delete("/api/experiments/{exp_id}")
def delete_single_experiment(exp_id: str):
    success = delete_experiment(exp_id)
    if not success:
        raise HTTPException(status_code=404, detail="Experiment not found")
    return {"status": "deleted"}


class ExperimentCompareRequest(BaseModel):
    exp_id_1: str
    exp_id_2: str


@app.post("/api/experiments/compare")
def compare_experiments(req: ExperimentCompareRequest):
    e1 = get_experiment(req.exp_id_1)
    e2 = get_experiment(req.exp_id_2)
    if not e1 or not e2:
        raise HTTPException(status_code=404, detail="One or both experiments not found")
    return {
        "exp1": ExperimentRecord(**e1),
        "exp2": ExperimentRecord(**e2)
    }


# ---------------------------------------------------------------------------
# Static Web Frontend
# ---------------------------------------------------------------------------

os.makedirs(STATIC_DIR, exist_ok=True)
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/")
def serve_index():
    index_file = os.path.join(STATIC_DIR, "index.html")
    if os.path.exists(index_file):
        return FileResponse(index_file)
    return JSONResponse({"status": "running", "message": "Goob Research Laboratory API ready"})
