"""
SQLite persistence layer for the Goob Chess Engine Research Laboratory.
"""

import os
import sqlite3
import json
from typing import List, Optional, Dict, Any
from datetime import datetime

DB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
DB_PATH = os.path.join(DB_DIR, "lab.db")


def get_connection(db_path: str = DB_PATH) -> sqlite3.Connection:
    os.makedirs(os.path.dirname(db_path), exist_ok=True)
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    return conn


def init_db(db_path: str = DB_PATH):
    """Creates the SQLite tables if they do not exist."""
    conn = get_connection(db_path)
    cursor = conn.cursor()

    cursor.execute("""
    CREATE TABLE IF NOT EXISTS engines (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        path TEXT NOT NULL,
        build_type TEXT NOT NULL,
        version TEXT,
        is_available INTEGER DEFAULT 1,
        options_json TEXT,
        registered_at TEXT
    )
    """)

    cursor.execute("""
    CREATE TABLE IF NOT EXISTS analyses (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        fen TEXT NOT NULL,
        title TEXT,
        engine_id TEXT,
        engine_name TEXT,
        depth INTEGER,
        best_move_uci TEXT,
        best_move_san TEXT,
        score_type TEXT,
        score_value INTEGER,
        score_formatted TEXT,
        lines_json TEXT,
        iterations_json TEXT,
        trace_json TEXT,
        notes TEXT,
        created_at TEXT
    )
    """)

    cursor.execute("""
    CREATE TABLE IF NOT EXISTS experiments (
        id TEXT PRIMARY KEY,
        title TEXT NOT NULL,
        exp_type TEXT NOT NULL,
        status TEXT NOT NULL,
        created_at TEXT NOT NULL,
        finished_at TEXT,
        git_commit TEXT,
        binary_name TEXT,
        settings_json TEXT,
        results_json TEXT,
        log_path TEXT,
        pgn_path TEXT,
        notes TEXT
    )
    """)

    conn.commit()
    conn.close()


# ---------------------------------------------------------------------------
# Engines CRUD
# ---------------------------------------------------------------------------

def save_engine(engine_data: Dict[str, Any], db_path: str = DB_PATH) -> bool:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("""
    INSERT INTO engines (id, name, path, build_type, version, is_available, options_json, registered_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
        name = excluded.name,
        path = excluded.path,
        build_type = excluded.build_type,
        version = excluded.version,
        is_available = excluded.is_available,
        options_json = excluded.options_json
    """, (
        engine_data["id"],
        engine_data["name"],
        engine_data["path"],
        engine_data.get("build_type", "release"),
        engine_data.get("version", "Unknown"),
        1 if engine_data.get("is_available", True) else 0,
        json.dumps(engine_data.get("options", {})),
        engine_data.get("registered_at", datetime.now().isoformat())
    ))
    conn.commit()
    conn.close()
    return True


def list_engines(db_path: str = DB_PATH) -> List[Dict[str, Any]]:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM engines ORDER BY name ASC")
    rows = cursor.fetchall()
    conn.close()

    result = []
    for r in rows:
        d = dict(r)
        d["is_available"] = bool(d["is_available"])
        d["options"] = json.loads(d["options_json"]) if d["options_json"] else {}
        d.pop("options_json", None)
        result.append(d)
    return result


def get_engine(engine_id: str, db_path: str = DB_PATH) -> Optional[Dict[str, Any]]:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM engines WHERE id = ?", (engine_id,))
    row = cursor.fetchone()
    conn.close()
    if not row:
        return None
    d = dict(row)
    d["is_available"] = bool(d["is_available"])
    d["options"] = json.loads(d["options_json"]) if d["options_json"] else {}
    d.pop("options_json", None)
    return d


def delete_engine(engine_id: str, db_path: str = DB_PATH) -> bool:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("DELETE FROM engines WHERE id = ?", (engine_id,))
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected


# ---------------------------------------------------------------------------
# Analyses CRUD
# ---------------------------------------------------------------------------

def save_analysis(analysis_data: Dict[str, Any], db_path: str = DB_PATH) -> int:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    created_at = analysis_data.get("created_at") or datetime.now().isoformat()
    cursor.execute("""
    INSERT INTO analyses (
        fen, title, engine_id, engine_name, depth, best_move_uci, best_move_san,
        score_type, score_value, score_formatted, lines_json, iterations_json,
        trace_json, notes, created_at
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        analysis_data.get("fen", ""),
        analysis_data.get("title", "Saved Analysis"),
        analysis_data.get("engine_id", ""),
        analysis_data.get("engine_name", ""),
        analysis_data.get("depth", 0),
        analysis_data.get("best_move_uci", ""),
        analysis_data.get("best_move_san", ""),
        analysis_data.get("score_type", "cp"),
        analysis_data.get("score_value", 0),
        analysis_data.get("score_formatted", "+0.00"),
        json.dumps(analysis_data.get("lines", [])),
        json.dumps(analysis_data.get("iterations", [])),
        json.dumps(analysis_data.get("trace", {})) if analysis_data.get("trace") else None,
        analysis_data.get("notes", ""),
        created_at
    ))
    new_id = cursor.lastrowid
    conn.commit()
    conn.close()
    return new_id


def list_analyses(limit: int = 50, db_path: str = DB_PATH) -> List[Dict[str, Any]]:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM analyses ORDER BY id DESC LIMIT ?", (limit,))
    rows = cursor.fetchall()
    conn.close()

    result = []
    for r in rows:
        d = dict(r)
        d["lines"] = json.loads(d["lines_json"]) if d["lines_json"] else []
        d["iterations"] = json.loads(d["iterations_json"]) if d["iterations_json"] else []
        d["trace"] = json.loads(d["trace_json"]) if d["trace_json"] else None
        d.pop("lines_json", None)
        d.pop("iterations_json", None)
        d.pop("trace_json", None)
        result.append(d)
    return result


def get_analysis(analysis_id: int, db_path: str = DB_PATH) -> Optional[Dict[str, Any]]:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM analyses WHERE id = ?", (analysis_id,))
    row = cursor.fetchone()
    conn.close()
    if not row:
        return None
    d = dict(row)
    d["lines"] = json.loads(d["lines_json"]) if d["lines_json"] else []
    d["iterations"] = json.loads(d["iterations_json"]) if d["iterations_json"] else []
    d["trace"] = json.loads(d["trace_json"]) if d["trace_json"] else None
    d.pop("lines_json", None)
    d.pop("iterations_json", None)
    d.pop("trace_json", None)
    return d


def delete_analysis(analysis_id: int, db_path: str = DB_PATH) -> bool:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("DELETE FROM analyses WHERE id = ?", (analysis_id,))
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected


# ---------------------------------------------------------------------------
# Experiments CRUD
# ---------------------------------------------------------------------------

def save_experiment(exp_data: Dict[str, Any], db_path: str = DB_PATH) -> bool:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("""
    INSERT INTO experiments (
        id, title, exp_type, status, created_at, finished_at,
        git_commit, binary_name, settings_json, results_json,
        log_path, pgn_path, notes
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
        title = excluded.title,
        exp_type = excluded.exp_type,
        status = excluded.status,
        finished_at = excluded.finished_at,
        settings_json = excluded.settings_json,
        results_json = excluded.results_json,
        log_path = excluded.log_path,
        pgn_path = excluded.pgn_path,
        notes = excluded.notes
    """, (
        exp_data["id"],
        exp_data.get("title", exp_data["id"]),
        exp_data.get("exp_type", "match"),
        exp_data.get("status", "completed"),
        exp_data.get("created_at", datetime.now().isoformat()),
        exp_data.get("finished_at"),
        exp_data.get("git_commit"),
        exp_data.get("binary_name"),
        json.dumps(exp_data.get("settings", {})),
        json.dumps(exp_data.get("results", {})),
        exp_data.get("log_path"),
        exp_data.get("pgn_path"),
        exp_data.get("notes")
    ))
    conn.commit()
    conn.close()
    return True


def update_experiment_status(exp_id: str, status: str, finished_at: Optional[str],
                             results: Dict[str, Any], db_path: str = DB_PATH) -> bool:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute(
        "UPDATE experiments SET status = ?, finished_at = ?, results_json = ? WHERE id = ?",
        (status, finished_at, json.dumps(results), exp_id)
    )
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected


def list_experiments(exp_type: Optional[str] = None, status: Optional[str] = None,
                     limit: int = 100, db_path: str = DB_PATH) -> List[Dict[str, Any]]:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    query = "SELECT * FROM experiments WHERE 1=1"
    params = []
    if exp_type:
        query += " AND exp_type = ?"
        params.append(exp_type)
    if status:
        query += " AND status = ?"
        params.append(status)
    query += " ORDER BY created_at DESC LIMIT ?"
    params.append(limit)

    cursor.execute(query, tuple(params))
    rows = cursor.fetchall()
    conn.close()

    result = []
    for r in rows:
        d = dict(r)
        d["settings"] = json.loads(d["settings_json"]) if d["settings_json"] else {}
        d["results"] = json.loads(d["results_json"]) if d["results_json"] else {}
        d.pop("settings_json", None)
        d.pop("results_json", None)
        result.append(d)
    return result


def get_experiment(exp_id: str, db_path: str = DB_PATH) -> Optional[Dict[str, Any]]:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM experiments WHERE id = ?", (exp_id,))
    row = cursor.fetchone()
    conn.close()
    if not row:
        return None
    d = dict(row)
    d["settings"] = json.loads(d["settings_json"]) if d["settings_json"] else {}
    d["results"] = json.loads(d["results_json"]) if d["results_json"] else {}
    d.pop("settings_json", None)
    d.pop("results_json", None)
    return d


def delete_experiment(exp_id: str, db_path: str = DB_PATH) -> bool:
    conn = get_connection(db_path)
    cursor = conn.cursor()
    cursor.execute("DELETE FROM experiments WHERE id = ?", (exp_id,))
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected
