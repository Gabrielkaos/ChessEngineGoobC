#!/usr/bin/env python3
"""
Goob Chess Engine Research Laboratory - Launcher
Starts the FastAPI backend and serves the research dashboard.

Usage:
    python3 run_lab.py [--port 8080] [--host 127.0.0.1]
"""

import os
import sys
import socket
import argparse
import subprocess

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
VENV_PYTHON = os.path.join(ROOT_DIR, "tools", ".venv", "bin", "python")

# Ensure research_lab is in python path
if ROOT_DIR not in sys.path:
    sys.path.insert(0, ROOT_DIR)


def is_port_in_use(host: str, port: int) -> bool:
    """Checks whether a TCP port is currently bound/in-use."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((host, port))
            return False
        except OSError:
            return True


def find_available_port(host: str, start_port: int, max_attempts: int = 50) -> int:
    """Finds the next free port starting from start_port."""
    for p in range(start_port, start_port + max_attempts):
        if not is_port_in_use(host, p):
            return p
    return start_port


def ensure_environment():
    """Ensures we run with the repository virtualenv containing uvicorn, fastapi, websockets & chess."""
    if os.path.exists(VENV_PYTHON) and sys.executable != VENV_PYTHON:
        os.execv(VENV_PYTHON, [VENV_PYTHON] + sys.argv)

    try:
        import fastapi
        import uvicorn
        import chess
        import websockets
    except ImportError:
        pip_bin = os.path.join(ROOT_DIR, "tools", ".venv", "bin", "pip")
        if os.path.exists(pip_bin):
            subprocess.run([pip_bin, "install", "websockets", "fastapi", "uvicorn", "python-chess"], check=False)
        try:
            import websockets
            import fastapi
            import uvicorn
            import chess
        except ImportError:
            sys.exit(
                "Error: Required Python packages not found.\n"
                "Please run: tools/.venv/bin/pip install websockets fastapi uvicorn python-chess"
            )


def verify_binaries():
    """Checks whether Goob binaries exist; builds them if missing."""
    native_bin = os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native")
    trace_bin = os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native-trace")

    if not os.path.exists(native_bin):
        print("[LAB] Building release native binary (make native)...")
        subprocess.run(["make", "-C", os.path.join(ROOT_DIR, "src"), "native"], check=True)

    if not os.path.exists(trace_bin):
        print("[LAB] Building trace diagnostic binary (make trace)...")
        subprocess.run(["make", "-C", os.path.join(ROOT_DIR, "src"), "trace"], check=True)


def main():
    parser = argparse.ArgumentParser(description="Goob Chess Engine Research Laboratory")
    parser.add_argument("--port", type=int, default=8080, help="Preferred HTTP server port (default: 8080)")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host address (default: 127.0.0.1)")
    parser.add_argument("--open-browser", action="store_true", help="Attempt to automatically launch default browser")
    args = parser.parse_args()

    ensure_environment()
    verify_binaries()

    port = args.port
    if is_port_in_use(args.host, port):
        free_port = find_available_port(args.host, port + 1)
        print(f"[LAB] Note: Port {port} is already in use by another process.")
        print(f"[LAB] Automatically switched to available port: {free_port}\n")
        port = free_port

    url = f"http://{args.host}:{port}"
    print("=" * 72)
    print("      GOOB CHESS ENGINE RESEARCH & EXPERIMENTATION PLATFORM")
    print("=" * 72)
    print(f"  • Web Dashboard:  {url}")
    print(f"  • API Docs:       {url}/docs")
    print(f"  • SQLite DB:      {os.path.join(ROOT_DIR, 'research_lab', 'data', 'lab.db')}")
    print("=" * 72)
    print("Press Ctrl+C to stop the laboratory.\n")

    if args.open_browser:
        try:
            subprocess.Popen(["xdg-open", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass

    import uvicorn
    uvicorn.run("research_lab.app:app", host=args.host, port=port, log_level="info")


if __name__ == "__main__":
    main()
