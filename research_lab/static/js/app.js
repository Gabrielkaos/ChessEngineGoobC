/**
 * Main Application Controller for Goob Chess Engine Research Laboratory.
 */

let board = null;
let analysisWs = null;
let activeAnalysisLines = {};
let activeIterations = [];
let currentTrace = null;
let currentEngines = [];
let tournamentWs = null;
let currentPgnPositions = [];
let currentPgnIndex = 0;

document.addEventListener("DOMContentLoaded", () => {
    initTabs();
    initBoard();
    loadEngines();
    loadSavedAnalyses();
    loadTournaments();
    loadSpsaRuns();
    loadExperiments();

    // Event listeners
    document.getElementById("btn-analyze-start").addEventListener("click", toggleAnalysis);
    document.getElementById("select-engine-analysis").addEventListener("change", () => {
        updateEngineStatusBadge();
        if (isSearching) {
            stopLiveAnalysis();
            if (document.getElementById("chk-auto-analyze").checked) {
                setTimeout(startLiveAnalysis, 150);
            }
        }
    });
    document.getElementById("btn-set-fen").addEventListener("click", () => {
        const fen = document.getElementById("input-fen").value.trim();
        if (fen) loadBoardPosition(fen);
    });
    document.getElementById("btn-reset-board").addEventListener("click", () => {
        loadBoardPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    });
    document.getElementById("btn-flip-board").addEventListener("click", () => {
        if (board) board.flip();
    });
    document.getElementById("btn-save-analysis").addEventListener("click", saveCurrentAnalysis);
    document.getElementById("btn-run-trace").addEventListener("click", runTraceDiagnostic);
    document.getElementById("btn-run-trace-bench").addEventListener("click", runTraceBenchDiagnostic);
    document.getElementById("btn-load-pgn").addEventListener("click", parsePgnInput);
    document.getElementById("btn-open-match-modal").addEventListener("click", () => {
        document.getElementById("match-modal").classList.add("active");
    });
    document.getElementById("btn-close-match-modal").addEventListener("click", () => {
        document.getElementById("match-modal").classList.remove("active");
    });
    document.getElementById("btn-start-match-submit").addEventListener("click", submitStartMatch);

    // Import Engine Modal
    document.getElementById("btn-open-import-modal").addEventListener("click", () => {
        document.getElementById("import-engine-modal").classList.add("active");
    });
    document.getElementById("btn-close-import-modal").addEventListener("click", () => {
        document.getElementById("import-engine-modal").classList.remove("active");
    });
    document.getElementById("btn-submit-import-engine").addEventListener("click", submitImportEngine);

    initTcPresets();
    document.getElementById("btn-open-spsa-modal").addEventListener("click", () => {
        document.getElementById("spsa-modal").classList.add("active");
    });
    document.getElementById("btn-close-spsa-modal").addEventListener("click", () => {
        document.getElementById("spsa-modal").classList.remove("active");
    });
    document.getElementById("btn-start-spsa-submit").addEventListener("click", submitStartSpsa);
    document.getElementById("spsa-run-select").addEventListener("change", (e) => {
        loadSpsaRunDetails(e.target.value);
    });
    document.getElementById("btn-copy-spsa-opts").addEventListener("click", copySpsaCutechessOpts);
    document.getElementById("btn-compare-exps").addEventListener("click", openExperimentCompareModal);
    document.getElementById("btn-close-compare-modal").addEventListener("click", () => {
        document.getElementById("compare-modal").classList.remove("active");
    });
});

// ---------------------------------------------------------------------------
// Tabs Navigation
// ---------------------------------------------------------------------------
function initTabs() {
    const tabs = document.querySelectorAll(".tab-btn");
    tabs.forEach(tab => {
        tab.addEventListener("click", () => {
            tabs.forEach(t => t.classList.remove("active"));
            document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));

            tab.classList.add("active");
            const targetId = tab.dataset.tab;
            const targetEl = document.getElementById(targetId);
            if (targetEl) targetEl.classList.add("active");

            // Re-render charts on tab switch if needed
            if (targetId === "tab-tree" && currentTrace) {
                drawCutoffDistribution("canvas-cutoffs", currentTrace.cutoffs);
            }
        });
    });
}

// ---------------------------------------------------------------------------
// Board Management
// ---------------------------------------------------------------------------
function initBoard() {
    board = new ChessBoard("chessboard", (move, res) => {
        // Update FEN input display
        document.getElementById("input-fen").value = res.fen;

        // Append to move history chips
        if (!currentPgnPositions || currentPgnPositions.length === 0) {
            currentPgnPositions = [{ ply: 0, move_san: "Start", move_uci: "", fen: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" }];
        }
        currentPgnPositions.push({
            ply: currentPgnPositions.length,
            move_san: res.san,
            move_uci: res.uci,
            fen: res.fen
        });
        currentPgnIndex = currentPgnPositions.length - 1;
        renderMoveHistory(currentPgnPositions);

        // Clear previous lines for the new position
        activeAnalysisLines = {};
        activeIterations = [];
        updateTelemetryDisplay(null);
        renderPvTable([]);
        drawScoreTrajectory("canvas-score-trajectory", []);

        if (document.getElementById("chk-auto-analyze").checked) {
            startLiveAnalysis();
        }
    });

    currentPgnPositions = [{ ply: 0, move_san: "Start", move_uci: "", fen: board.fen }];
    renderMoveHistory(currentPgnPositions);
}

function loadBoardPosition(fen) {
    if (board) {
        board.loadFen(fen);
        document.getElementById("input-fen").value = fen;
        activeAnalysisLines = {};
        activeIterations = [];
        updateTelemetryDisplay(null);
        renderPvTable([]);
        drawScoreTrajectory("canvas-score-trajectory", []);
    }
}

// ---------------------------------------------------------------------------
// Engines Discovery
// ---------------------------------------------------------------------------
async function loadEngines() {
    try {
        const resp = await fetch("/api/engines");
        currentEngines = await resp.json();

        const selAnalysis = document.getElementById("select-engine-analysis");
        const selNew = document.getElementById("match-engine-new");
        const selBase = document.getElementById("match-engine-base");

        selAnalysis.innerHTML = "";
        selNew.innerHTML = "";
        selBase.innerHTML = "";

        // Analysis dropdown has all engines
        currentEngines.forEach(eng => {
            const opt = document.createElement("option");
            opt.value = eng.id;
            opt.textContent = `${eng.name} (${eng.build_type})`;
            selAnalysis.appendChild(opt);
        });

        // Tournament pairings exclude slow diagnostic/tuning builds (trace and tune)
        const tourneyEngines = currentEngines.filter(
            eng => eng.build_type !== "tune" && eng.build_type !== "trace"
        );

        tourneyEngines.forEach(eng => {
            const opt1 = document.createElement("option");
            opt1.value = eng.id;
            opt1.textContent = `${eng.name} [${eng.build_type}]`;
            const opt2 = opt1.cloneNode(true);
            selNew.appendChild(opt1);
            selBase.appendChild(opt2);
        });

        // Pre-select reasonable defaults
        if (tourneyEngines.length > 1) {
            selBase.selectedIndex = 1;
        }

        // Default Analysis engine to Goob Native if available
        const goobNativeIdx = Array.from(selAnalysis.options).findIndex(
            opt => opt.value === "goob-native" || opt.text.toLowerCase().includes("native")
        );
        if (goobNativeIdx >= 0) {
            selAnalysis.selectedIndex = goobNativeIdx;
        }

        updateEngineStatusBadge();
    } catch (e) {
        console.error("Failed to load engines:", e);
    }
}

// ---------------------------------------------------------------------------
// Analysis & WebSocket Live Streaming
// ---------------------------------------------------------------------------
let isSearching = false;
let restAbortController = null;

function updateEngineStatusBadge() {
    const badge = document.getElementById("engine-badge-text");
    const statusDot = document.querySelector(".engine-status-badge .status-dot");
    const selAnalysis = document.getElementById("select-engine-analysis");
    if (!badge || !selAnalysis) return;

    const selectedOption = selAnalysis.options[selAnalysis.selectedIndex];
    const engineName = selectedOption ? selectedOption.text.split(" (")[0] : (currentEngines[0]?.name || "Engine");

    if (isSearching) {
        badge.textContent = `${engineName} (Searching...)`;
        if (statusDot) {
            statusDot.style.backgroundColor = "var(--accent)";
            statusDot.style.boxShadow = "0 0 8px var(--accent)";
        }
    } else {
        badge.textContent = `${engineName} (Ready)`;
        if (statusDot) {
            statusDot.style.backgroundColor = "var(--success)";
            statusDot.style.boxShadow = "0 0 8px var(--success)";
        }
    }
}

function setSearchingState(searching) {
    isSearching = searching;
    updateEngineStatusBadge();
    const btn = document.getElementById("btn-analyze-start");
    if (!btn) return;
    if (searching) {
        btn.innerHTML = `<span class="status-dot" style="background-color: #ef4444; box-shadow: 0 0 8px #ef4444;"></span> Stop Search`;
        btn.className = "btn btn-danger";
    } else {
        btn.innerHTML = `▶ Start Search`;
        btn.className = "btn btn-primary";
    }
}

function toggleAnalysis() {
    if (isSearching) {
        stopLiveAnalysis();
    } else {
        startLiveAnalysis();
    }
}

function startLiveAnalysis() {
    const fen = document.getElementById("input-fen").value.trim() || board.fen;
    const engineId = document.getElementById("select-engine-analysis").value;
    const depth = parseInt(document.getElementById("input-depth").value, 10) || 12;
    const multipv = parseInt(document.getElementById("input-multipv").value, 10) || 1;
    const hashMb = parseInt(document.getElementById("input-hash").value, 10) || 64;
    const threads = parseInt(document.getElementById("input-threads").value, 10) || 1;

    activeAnalysisLines = {};
    activeIterations = [];
    setSearchingState(true);

    const startPayload = {
        action: "start",
        fen: fen,
        engine_id: engineId,
        depth: depth,
        multipv: multipv,
        hash_mb: hashMb,
        threads: threads
    };

    if (analysisWs && analysisWs.readyState === WebSocket.OPEN) {
        analysisWs.send(JSON.stringify(startPayload));
        return;
    }

    if (analysisWs) {
        try { analysisWs.close(); } catch (e) {}
        analysisWs = null;
    }

    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const wsUrl = `${protocol}//${window.location.host}/ws/analysis`;
    analysisWs = new WebSocket(wsUrl);

    analysisWs.onopen = () => {
        analysisWs.send(JSON.stringify(startPayload));
    };

    analysisWs.onmessage = (event) => {
        const msg = JSON.parse(event.data);
        if (msg.type === "pv") {
            const line = msg.data;
            activeAnalysisLines[line.multipv] = line;
            updateTelemetryDisplay(activeAnalysisLines[1]);
            renderPvTable(Object.values(activeAnalysisLines));
        } else if (msg.type === "iteration") {
            activeIterations.push(msg.data);
            drawScoreTrajectory("canvas-score-trajectory", activeIterations);
            renderIterationsTable(activeIterations);
        } else if (msg.type === "trace") {
            currentTrace = msg.data;
            renderTraceTelemetry(currentTrace);
        } else if (msg.type === "bestmove") {
            document.getElementById("metric-bestmove").innerText = msg.bestmove || "None";
            setSearchingState(false);
        } else if (msg.type === "error") {
            console.error("Engine error:", msg.message);
            setSearchingState(false);
        }
    };

    analysisWs.onerror = (e) => {
        console.warn("WebSocket error, falling back to REST analysis:", e);
        fallbackRestAnalysis(fen, engineId, depth, multipv, hashMb, threads);
    };

    analysisWs.onclose = () => {
        setSearchingState(false);
    };
}

function stopLiveAnalysis() {
    setSearchingState(false);
    if (analysisWs && analysisWs.readyState === WebSocket.OPEN) {
        try {
            analysisWs.send(JSON.stringify({ action: "stop" }));
        } catch (e) {}
    }
    if (restAbortController) {
        try {
            restAbortController.abort();
        } catch (e) {}
        restAbortController = null;
    }
}

async function fallbackRestAnalysis(fen, engineId, depth, multipv, hashMb, threads) {
    if (restAbortController) {
        try { restAbortController.abort(); } catch (e) {}
    }
    restAbortController = new AbortController();
    setSearchingState(true);

    try {
        const resp = await fetch("/api/position/analyze", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            signal: restAbortController.signal,
            body: JSON.stringify({
                fen: fen,
                engine_id: engineId,
                depth: depth,
                multipv: multipv,
                hash_mb: hashMb,
                threads: threads
            })
        });
        const res = await resp.json();
        if (res.lines) {
            res.lines.forEach(l => {
                activeAnalysisLines[l.multipv] = l;
            });
            updateTelemetryDisplay(activeAnalysisLines[1]);
            renderPvTable(Object.values(activeAnalysisLines));
        }
        if (res.iterations) {
            activeIterations = res.iterations;
            drawScoreTrajectory("canvas-score-trajectory", activeIterations);
            renderIterationsTable(activeIterations);
        }
        if (res.trace) {
            currentTrace = res.trace;
            renderTraceTelemetry(currentTrace);
        }
        document.getElementById("metric-bestmove").innerText = res.best_move_san || res.best_move_uci || "None";
    } catch (err) {
        if (err.name !== "AbortError") {
            console.error("REST analysis failed:", err);
        }
    } finally {
        setSearchingState(false);
        restAbortController = null;
    }
}

function updateTelemetryDisplay(pv) {
    if (!pv) {
        document.getElementById("metric-depth").innerText = "—";
        document.getElementById("metric-score").innerText = "0.00";
        document.getElementById("metric-nodes").innerText = "—";
        document.getElementById("metric-nps").innerText = "—";
        document.getElementById("metric-time").innerText = "—";
        document.getElementById("metric-bestmove").innerText = "—";
        document.getElementById("eval-bar-fill").style.width = "50%";
        return;
    }

    document.getElementById("metric-depth").innerText = `${pv.depth} / ${pv.seldepth}`;
    document.getElementById("metric-score").innerText = pv.score_formatted;
    document.getElementById("metric-nodes").innerText = pv.nodes.toLocaleString();
    document.getElementById("metric-nps").innerText = `${(pv.nps / 1000).toFixed(0)} kN/s`;
    document.getElementById("metric-time").innerText = `${pv.time_ms} ms`;
    document.getElementById("metric-bestmove").innerText = (pv.pv_san && pv.pv_san[0]) ? pv.pv_san[0] : (pv.pv_uci[0] || "—");

    // Eval bar update (-1000 to +1000 centipawns mapped to 0% - 100%)
    if (pv.score_type === "cp") {
        const cp = Math.max(-1000, Math.min(1000, pv.score_value));
        const pct = 50 + (cp / 1000) * 50;
        document.getElementById("eval-bar-fill").style.width = `${pct}%`;
    } else if (pv.score_type === "mate") {
        document.getElementById("eval-bar-fill").style.width = pv.score_value > 0 ? "100%" : "0%";
    }
}

function renderPvTable(lines) {
    const tbody = document.getElementById("pv-table-body");
    tbody.innerHTML = "";
    lines.sort((a, b) => a.multipv - b.multipv);

    lines.forEach(l => {
        const tr = document.createElement("tr");
        const pvStr = l.pv_san.slice(0, 10).join(" ");
        tr.innerHTML = `
            <td><strong>#${l.multipv}</strong></td>
            <td><span class="pill ${l.score_value >= 0 ? "pill-success" : "pill-danger"}">${l.score_formatted}</span></td>
            <td>${l.depth}/${l.seldepth}</td>
            <td>${l.nodes.toLocaleString()}</td>
            <td class="pv-moves" title="${l.pv_uci.join(' ')}">${pvStr}</td>
        `;
        tbody.appendChild(tr);
    });
}

function renderIterationsTable(iterations) {
    const tbody = document.getElementById("iterations-table-body");
    if (!tbody) return;
    tbody.innerHTML = "";

    iterations.slice().reverse().forEach(it => {
        const tr = document.createElement("tr");
        const scoreStr = it.score_cp !== null ? `${(it.score_cp / 100.0).toFixed(2)}` : `#${it.score_mate}`;
        tr.innerHTML = `
            <td>${it.depth}</td>
            <td>${it.seldepth}</td>
            <td>${scoreStr}</td>
            <td><strong>${it.best_move_san || it.best_move_uci}</strong></td>
            <td>${it.nodes.toLocaleString()}</td>
            <td>${it.time_ms} ms</td>
        `;
        tbody.appendChild(tr);
    });
}

// ---------------------------------------------------------------------------
// Heuristic Trace & Diagnostics (Feature B)
// ---------------------------------------------------------------------------
async function runTraceDiagnostic() {
    const fen = document.getElementById("input-fen").value.trim() || board.fen;
    const depth = parseInt(document.getElementById("input-depth").value, 10) || 8;
    const btnPos = document.getElementById("btn-run-trace");
    const btnBench = document.getElementById("btn-run-trace-bench");
    const loadingEl = document.getElementById("trace-loading");
    const titleEl = document.getElementById("trace-loading-title");
    const subtitleEl = document.getElementById("trace-loading-subtitle");

    btnPos.disabled = true;
    btnBench.disabled = true;
    btnPos.innerHTML = `<span class="spinner" style="width:13px;height:13px;border-width:2px;display:inline-block;vertical-align:middle;margin-right:5px;"></span> Profiling...`;

    if (loadingEl) {
        titleEl.innerText = `Profiling current board position (Depth ${depth})...`;
        subtitleEl.innerText = `Analyzing cutoffs and pruning rates with GOOB 2.2-BETA-native-trace • 0.0s elapsed`;
        loadingEl.style.display = "flex";
    }

    document.querySelector('[data-tab="tab-tree"]').click();

    const startTime = performance.now();
    const timer = setInterval(() => {
        if (subtitleEl) {
            const elapsed = ((performance.now() - startTime) / 1000).toFixed(1);
            subtitleEl.innerText = `Analyzing cutoffs and pruning rates with GOOB 2.2-BETA-native-trace • ${elapsed}s elapsed`;
        }
    }, 100);

    try {
        const resp = await fetch("/api/position/trace", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ fen, depth })
        });
        currentTrace = await resp.json();
        const badge = document.getElementById("trace-target-badge");
        if (badge) badge.innerText = `Target: Current Board Position (Depth ${depth})`;
        renderTraceTelemetry(currentTrace);
    } catch (e) {
        alert("Trace failed: " + e.message);
    } finally {
        clearInterval(timer);
        if (loadingEl) loadingEl.style.display = "none";
        btnPos.disabled = false;
        btnBench.disabled = false;
        btnPos.innerText = "🔍 Profile Current Position";
    }
}

async function runTraceBenchDiagnostic() {
    const btnBench = document.getElementById("btn-run-trace-bench");
    const btnPos = document.getElementById("btn-run-trace");
    const loadingEl = document.getElementById("trace-loading");
    const titleEl = document.getElementById("trace-loading-title");
    const subtitleEl = document.getElementById("trace-loading-subtitle");

    btnBench.disabled = true;
    btnPos.disabled = true;
    btnBench.innerHTML = `<span class="spinner" style="width:13px;height:13px;border-width:2px;display:inline-block;vertical-align:middle;margin-right:5px;"></span> Running Suite...`;

    if (loadingEl) {
        titleEl.innerText = "Benchmarking 30 standard positions...";
        subtitleEl.innerText = "Searching openings, middlegames, and endgames with GOOB 2.2-BETA-native-trace • 0.0s elapsed";
        loadingEl.style.display = "flex";
    }

    document.querySelector('[data-tab="tab-tree"]').click();

    const startTime = performance.now();
    const timer = setInterval(() => {
        if (subtitleEl) {
            const elapsed = ((performance.now() - startTime) / 1000).toFixed(1);
            subtitleEl.innerText = `Searching openings, middlegames, and endgames with GOOB 2.2-BETA-native-trace • ${elapsed}s elapsed`;
        }
    }, 100);

    try {
        const resp = await fetch("/api/position/trace-bench?depth=6", {
            method: "POST"
        });
        currentTrace = await resp.json();
        const badge = document.getElementById("trace-target-badge");
        if (badge) badge.innerText = "Target: 30-Position Benchmark Suite (Cumulative)";
        renderTraceTelemetry(currentTrace);
    } catch (e) {
        alert("Benchmark trace failed: " + e.message);
    } finally {
        clearInterval(timer);
        if (loadingEl) loadingEl.style.display = "none";
        btnBench.disabled = false;
        btnPos.disabled = false;
        btnBench.innerText = "📊 Profile Bench Suite (30 FENs)";
    }
}

function renderTraceTelemetry(trace) {
    if (!trace || !trace.trace_enabled) {
        document.getElementById("trace-warning").style.display = "block";
        return;
    }
    document.getElementById("trace-warning").style.display = "none";

    // Draw Cutoff Distribution Chart
    drawCutoffDistribution("canvas-cutoffs", trace.cutoffs);

    // Node distribution
    document.getElementById("trace-ab-nodes").innerText = trace.ab_nodes.toLocaleString();
    document.getElementById("trace-qs-nodes").innerText = trace.qs_nodes.toLocaleString();
    document.getElementById("trace-evals").innerText = trace.eval_calls.toLocaleString();
    document.getElementById("trace-tt-hits").innerText = trace.tt_hits.toLocaleString();

    // Heuristics table
    const tbody = document.getElementById("heuristics-table-body");
    tbody.innerHTML = "";

    (trace.diagnostic_items || []).forEach(item => {
        const tr = document.createElement("tr");
        let pillClass = "pill-success";
        if (item.status === "low") pillClass = "pill-warning";
        if (item.status === "high") pillClass = "pill-danger";

        tr.innerHTML = `
            <td><strong>${item.name}</strong></td>
            <td>${item.considered !== null ? item.considered.toLocaleString() : "—"}</td>
            <td>${item.triggered !== null ? item.triggered.toLocaleString() : "—"}</td>
            <td><span class="pill ${pillClass}">${item.rate !== null ? item.rate.toFixed(1) + "%" : "—"}</span></td>
            <td><small style="color: #94a3b8">${item.constants || ""}</small></td>
            <td><small>${item.advice || ""}</small></td>
        `;
        tbody.appendChild(tr);
    });
}

// ---------------------------------------------------------------------------
// PGN Parser & Game Position Explorer
// ---------------------------------------------------------------------------
async function parsePgnInput() {
    const pgnText = document.getElementById("input-pgn").value.trim();
    if (!pgnText) return;

    try {
        const resp = await fetch("/api/position/parse-pgn", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ pgn: pgnText })
        });
        const data = await resp.json();
        currentPgnPositions = data.positions || [];
        currentPgnIndex = 0;

        renderMoveHistory(currentPgnPositions);
        if (currentPgnPositions.length > 0) {
            loadBoardPosition(currentPgnPositions[0].fen);
        }
    } catch (e) {
        alert("Failed to parse PGN: " + e.message);
    }
}

function renderMoveHistory(positions) {
    const container = document.getElementById("move-history-container");
    container.innerHTML = "";

    positions.forEach((pos, idx) => {
        const span = document.createElement("span");
        span.className = `move-token ${idx === currentPgnIndex ? "active" : ""}`;
        span.innerText = `${idx}. ${pos.move_san}`;
        span.addEventListener("click", () => {
            currentPgnIndex = idx;
            document.querySelectorAll(".move-token").forEach((t, i) => {
                t.classList.toggle("active", i === idx);
            });
            loadBoardPosition(pos.fen);
        });
        container.appendChild(span);
    });
}

// ---------------------------------------------------------------------------
// Saved Analyses (Feature A basic persistence)
// ---------------------------------------------------------------------------
async function saveCurrentAnalysis() {
    const primaryLine = activeAnalysisLines[1];
    const data = {
        fen: board.fen,
        title: `Analysis d${primaryLine ? primaryLine.depth : 0} (${board.fen.slice(0, 20)}...)`,
        engine_id: document.getElementById("select-engine-analysis").value,
        depth: primaryLine ? primaryLine.depth : 0,
        best_move_uci: primaryLine && primaryLine.pv_uci ? primaryLine.pv_uci[0] : "",
        best_move_san: primaryLine && primaryLine.pv_san ? primaryLine.pv_san[0] : "",
        score_type: primaryLine ? primaryLine.score_type : "cp",
        score_value: primaryLine ? primaryLine.score_value : 0,
        score_formatted: primaryLine ? primaryLine.score_formatted : "+0.00",
        lines: Object.values(activeAnalysisLines),
        iterations: activeIterations,
        trace: currentTrace
    };

    try {
        await fetch("/api/analyses", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(data)
        });
        loadSavedAnalyses();
        alert("Analysis saved to database successfully.");
    } catch (e) {
        alert("Failed to save analysis: " + e.message);
    }
}

async function loadSavedAnalyses() {
    try {
        const resp = await fetch("/api/analyses");
        const list = await resp.json();
        const tbody = document.getElementById("saved-analyses-body");
        if (!tbody) return;
        tbody.innerHTML = "";

        list.forEach(item => {
            const tr = document.createElement("tr");
            tr.innerHTML = `
                <td>${item.title || "Untitled"}</td>
                <td><small style="font-family: monospace;">${item.fen.slice(0, 28)}...</small></td>
                <td><span class="pill pill-info">${item.score_formatted}</span></td>
                <td>d${item.depth}</td>
                <td>${item.created_at ? item.created_at.slice(0, 16) : ""}</td>
                <td>
                    <button class="btn" onclick="restoreAnalysis(${item.id})">Load</button>
                    <button class="btn btn-danger" onclick="deleteAnalysisItem(${item.id})">×</button>
                </td>
            `;
            tbody.appendChild(tr);
        });
    } catch (e) {
        console.error("Failed to load analyses:", e);
    }
}

window.restoreAnalysis = async (id) => {
    const resp = await fetch(`/api/analyses/${id}`);
    const data = await resp.json();
    loadBoardPosition(data.fen);
    if (data.lines) renderPvTable(data.lines);
    if (data.iterations) {
        activeIterations = data.iterations;
        drawScoreTrajectory("canvas-score-trajectory", activeIterations);
        renderIterationsTable(activeIterations);
    }
    if (data.trace) {
        currentTrace = data.trace;
        renderTraceTelemetry(currentTrace);
    }
};

window.deleteAnalysisItem = async (id) => {
    if (!confirm("Delete this saved analysis?")) return;
    await fetch(`/api/analyses/${id}`, { method: "DELETE" });
    loadSavedAnalyses();
};

// ---------------------------------------------------------------------------
// Tournament & Matches (Feature C)
// ---------------------------------------------------------------------------
async function submitStartMatch() {
    const config = {
        name: document.getElementById("match-name").value.trim() || "match",
        engine_new_id: document.getElementById("match-engine-new").value,
        engine_base_id: document.getElementById("match-engine-base").value,
        tc: document.getElementById("match-tc").value.trim() || "6+0.06",
        rounds: parseInt(document.getElementById("match-rounds").value, 10) || 50,
        concurrency: parseInt(document.getElementById("match-concurrency").value, 10) || 4,
        hash_mb: parseInt(document.getElementById("match-hash").value, 10) || 32,
        threads: 1,
        book: "tools/book.epd",
        is_sprt: document.getElementById("match-is-sprt").checked,
        elo0: parseFloat(document.getElementById("match-elo0").value) || 0.0,
        elo1: parseFloat(document.getElementById("match-elo1").value) || 5.0,
        alpha: 0.05,
        beta: 0.05,
        rating_interval: 10
    };

    try {
        const resp = await fetch("/api/tournaments/start", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(config)
        });
        const match = await resp.json();
        document.getElementById("match-modal").classList.remove("active");
        attachTournamentStream(match.id);
        loadTournaments();
    } catch (e) {
        alert("Failed to start match: " + e.message);
    }
}

function initTcPresets() {
    const tcInput = document.getElementById("match-tc");
    const buttons = document.querySelectorAll(".tc-preset-btn");

    buttons.forEach(btn => {
        btn.addEventListener("click", () => {
            const tc = btn.dataset.tc;
            if (tcInput) tcInput.value = tc;
            buttons.forEach(b => b.classList.remove("active"));
            btn.classList.add("active");
        });
    });

    if (tcInput) {
        tcInput.addEventListener("input", () => {
            const val = tcInput.value.trim();
            buttons.forEach(b => {
                b.classList.toggle("active", b.dataset.tc === val);
            });
        });
    }

    const sprtCheckbox = document.getElementById("match-is-sprt");
    if (sprtCheckbox) {
        sprtCheckbox.addEventListener("change", () => {
            const elo0 = document.getElementById("match-elo0");
            const elo1 = document.getElementById("match-elo1");
            if (elo0) elo0.disabled = !sprtCheckbox.checked;
            if (elo1) elo1.disabled = !sprtCheckbox.checked;
        });
    }
}

async function submitImportEngine() {
    const pathInput = document.getElementById("import-engine-path");
    const nameInput = document.getElementById("import-engine-name");
    const path = pathInput.value.trim();
    const name = nameInput.value.trim() || undefined;

    if (!path) {
        alert("Please enter the binary path of the engine.");
        return;
    }

    const btn = document.getElementById("btn-submit-import-engine");
    btn.disabled = true;
    btn.innerText = "Validating...";

    try {
        const resp = await fetch("/api/engines/import", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ path, name })
        });
        if (!resp.ok) {
            const err = await resp.json();
            throw new Error(err.detail || "Failed to import engine");
        }
        const engine = await resp.json();
        alert(`Successfully imported: ${engine.name} (${engine.version})!`);
        document.getElementById("import-engine-modal").classList.remove("active");
        pathInput.value = "";
        nameInput.value = "";
        await loadEngines();
    } catch (e) {
        alert("Import Error: " + e.message);
    } finally {
        btn.disabled = false;
        btn.innerText = "Import & Validate";
    }
}

let currentAttachedMatchId = null;

async function attachTournamentStream(matchId) {
    currentAttachedMatchId = matchId;
    const terminal = document.getElementById("tournament-terminal");
    terminal.innerText = `Connecting to match ${matchId}...\n`;

    // 1. Preload any existing logs via REST right away
    try {
        const logResp = await fetch(`/api/tournaments/${matchId}/logs`);
        if (logResp.ok) {
            const data = await logResp.json();
            if (data.logs) {
                terminal.innerText = data.logs;
                terminal.scrollTop = terminal.scrollHeight;
            }
        }
    } catch (e) {
        console.warn("Could not preload match logs:", e);
    }

    // 2. Preload current match status
    try {
        const stResp = await fetch(`/api/tournaments/${matchId}`);
        if (stResp.ok) {
            const st = await stResp.json();
            updateTournamentStatusUI(st);
        }
    } catch (e) {}

    // 3. Connect WebSocket for live streaming
    if (tournamentWs) {
        try { tournamentWs.close(); } catch (e) {}
        tournamentWs = null;
    }

    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    tournamentWs = new WebSocket(`${protocol}//${window.location.host}/ws/tournament/${matchId}`);

    tournamentWs.onmessage = (event) => {
        const msg = JSON.parse(event.data);
        if (msg.type === "history") {
            terminal.innerText = msg.logs;
            terminal.scrollTop = terminal.scrollHeight;
        } else if (msg.type === "log") {
            if (terminal.innerText.startsWith("Connecting to match")) {
                terminal.innerText = "";
            }
            terminal.innerText += msg.line;
            terminal.scrollTop = terminal.scrollHeight;
        } else if (msg.type === "status") {
            updateTournamentStatusUI(msg.data);
            if (msg.data.status !== "running") {
                loadTournaments();
            }
        }
    };

    tournamentWs.onerror = (e) => {
        console.warn("Tournament WS error:", e);
    };

    tournamentWs.onclose = () => {
        loadTournaments();
    };
}

function updateTournamentStatusUI(st) {
    if (!st) return;
    document.getElementById("tourney-wins").innerText = st.wins;
    document.getElementById("tourney-losses").innerText = st.losses;
    document.getElementById("tourney-draws").innerText = st.draws;
    document.getElementById("tourney-score-pct").innerText = `${st.score_pct}%`;
    document.getElementById("tourney-elo").innerText = st.elo_diff !== null ? `${st.elo_diff >= 0 ? "+" : ""}${st.elo_diff.toFixed(1)} ± ${st.elo_error ? st.elo_error.toFixed(1) : "—"}` : "—";

    let sprtInfo = "";
    if (st.sprt_llr !== null && st.sprt_llr !== undefined) {
        const dec = st.sprt_status ? ` • ${st.sprt_status.toUpperCase()}` : "";
        sprtInfo = ` • LLR ${st.sprt_llr.toFixed(2)} [${st.sprt_lbound}, ${st.sprt_ubound}]${dec}`;
    }
    const gamesBadge = document.getElementById("tourney-games");
    gamesBadge.innerText = `${st.name || "Match"}: ${st.games_played} games (${st.status.toUpperCase()})${sprtInfo}`;
    if (st.status === "completed") {
        gamesBadge.className = "pill pill-success";
    } else if (st.status === "failed") {
        gamesBadge.className = "pill pill-danger";
    } else if (st.status === "running") {
        gamesBadge.className = "pill pill-info";
    }
}

async function loadTournaments() {
    try {
        const resp = await fetch("/api/tournaments");
        const matches = await resp.json();
        const tbody = document.getElementById("tournaments-table-body");
        if (!tbody) return;
        tbody.innerHTML = "";

        matches.forEach(m => {
            const tr = document.createElement("tr");
            const eloStr = m.elo_diff !== null ? `${m.elo_diff >= 0 ? "+" : ""}${m.elo_diff.toFixed(1)} ± ${m.elo_error ? m.elo_error.toFixed(1) : ""}` : "—";
            tr.innerHTML = `
                <td><strong>${m.name}</strong></td>
                <td>${m.engine_new_name} vs ${m.engine_base_name}</td>
                <td>${m.wins} - ${m.losses} - ${m.draws} (${m.games_played})</td>
                <td>${m.score_pct}%</td>
                <td><span class="pill ${m.elo_diff && m.elo_diff >= 0 ? "pill-success" : "pill-danger"}">${eloStr}</span></td>
                <td><span class="pill pill-info">${m.status}</span></td>
                <td>
                    <button class="btn" onclick="attachTournamentStream('${m.id}')">View</button>
                    ${m.pgn_file ? `<a href="/api/tournaments/${m.id}/pgn" class="btn" download>PGN</a>` : ""}
                </td>
            `;
            tbody.appendChild(tr);
        });
    } catch (e) {
        console.error("Failed to load tournaments:", e);
    }
}

// ---------------------------------------------------------------------------
// SPSA Tuning (Feature D)
// ---------------------------------------------------------------------------
async function loadSpsaRuns() {
    try {
        const resp = await fetch("/api/spsa/runs");
        const runs = await resp.json();
        const sel = document.getElementById("spsa-run-select");
        sel.innerHTML = "";

        runs.forEach(r => {
            const opt = document.createElement("option");
            opt.value = r;
            opt.textContent = r;
            sel.appendChild(opt);
        });

        if (runs.length > 0) {
            loadSpsaRunDetails(runs[runs.length - 1]);
        }
    } catch (e) {
        console.error("Failed to list SPSA runs:", e);
    }
}

async function loadSpsaRunDetails(name) {
    if (!name) return;
    try {
        const resp = await fetch(`/api/spsa/runs/${name}`);
        const data = await resp.json();

        document.getElementById("spsa-stat-iters").innerText = `${data.iterations_completed} / ${data.iterations_total}`;
        document.getElementById("spsa-stat-games").innerText = `${data.games_played.toLocaleString()} games`;
        document.getElementById("spsa-stat-time").innerText = `${data.elapsed_hours}h`;
        document.getElementById("spsa-stat-status").innerText = data.status;

        // Render parameter table
        const tbody = document.getElementById("spsa-params-table-body");
        tbody.innerHTML = "";

        data.params.forEach(p => {
            const tr = document.createElement("tr");
            const changePill = p.change === 0 ? "pill-info" : (p.change > 0 ? "pill-success" : "pill-danger");
            const changeSign = p.change > 0 ? `+${p.change}` : `${p.change}`;

            tr.innerHTML = `
                <td><strong>${p.name}</strong></td>
                <td>${p.default}</td>
                <td><strong>${p.tuned}</strong></td>
                <td><span class="pill ${changePill}">${changeSign}</span></td>
                <td>[${p.min_val}, ${p.max_val}]</td>
            `;
            tbody.appendChild(tr);
        });

        // Draw convergence trajectory if history is present
        if (data.recent_history && data.recent_history.length > 1) {
            drawSPSATrajectory("canvas-spsa-trajectory", data.recent_history);
        }
    } catch (e) {
        console.error("Failed to load SPSA details:", e);
    }
}

async function copySpsaCutechessOpts() {
    const sel = document.getElementById("spsa-run-select").value;
    if (!sel) return;
    const resp = await fetch(`/api/spsa/runs/${sel}/options`);
    const data = await resp.json();
    navigator.clipboard.writeText(data.options);
    alert("Copied cutechess-cli parameters to clipboard!");
}

async function submitStartSpsa() {
    const config = {
        name: document.getElementById("spsa-run-name").value.trim(),
        iterations: parseInt(document.getElementById("spsa-run-iters").value, 10) || 5000,
        pairs: 2,
        tc: document.getElementById("spsa-run-tc").value.trim() || "5+0.05",
        workers: parseInt(document.getElementById("spsa-run-workers").value, 10) || 4,
        hash_mb: 16,
        book: "tools/book.epd"
    };

    if (!config.name) {
        alert("Please specify a run name");
        return;
    }

    try {
        await fetch("/api/spsa/runs", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(config)
        });
        document.getElementById("spsa-modal").classList.remove("active");
        loadSpsaRuns();
        alert(`SPSA run '${config.name}' launched in background!`);
    } catch (e) {
        alert("Failed to start SPSA: " + e.message);
    }
}

// ---------------------------------------------------------------------------
// Experiments Registry (Feature E)
// ---------------------------------------------------------------------------
async function loadExperiments() {
    try {
        const resp = await fetch("/api/experiments");
        const list = await resp.json();
        const tbody = document.getElementById("experiments-table-body");
        if (!tbody) return;
        tbody.innerHTML = "";

        const sel1 = document.getElementById("compare-exp1");
        const sel2 = document.getElementById("compare-exp2");
        sel1.innerHTML = "";
        sel2.innerHTML = "";

        list.forEach(exp => {
            const tr = document.createElement("tr");
            tr.innerHTML = `
                <td><strong>${exp.title || exp.id}</strong></td>
                <td><span class="pill pill-info">${exp.exp_type}</span></td>
                <td>${exp.binary_name || "—"}</td>
                <td><span class="pill ${exp.status === "completed" ? "pill-success" : "pill-warning"}">${exp.status}</span></td>
                <td>${exp.created_at ? exp.created_at.slice(0, 16) : ""}</td>
                <td>
                    <button class="btn btn-danger" onclick="deleteExp('${exp.id}')">×</button>
                </td>
            `;
            tbody.appendChild(tr);

            const opt1 = document.createElement("option");
            opt1.value = exp.id;
            opt1.textContent = `${exp.title} (${exp.exp_type})`;
            sel1.appendChild(opt1);

            const opt2 = opt1.cloneNode(true);
            sel2.appendChild(opt2);
        });

        if (list.length > 1) {
            sel2.selectedIndex = 1;
        }
    } catch (e) {
        console.error("Failed to load experiments:", e);
    }
}

window.deleteExp = async (id) => {
    if (!confirm("Delete experiment record?")) return;
    await fetch(`/api/experiments/${id}`, { method: "DELETE" });
    loadExperiments();
};

function openExperimentCompareModal() {
    const exp1 = document.getElementById("compare-exp1").value;
    const exp2 = document.getElementById("compare-exp2").value;

    fetch("/api/experiments/compare", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ exp_id_1: exp1, exp_id_2: exp2 })
    }).then(r => r.json()).then(data => {
        document.getElementById("compare-card-1-title").innerText = data.exp1.title;
        document.getElementById("compare-card-1-content").innerText = JSON.stringify(data.exp1.results, null, 2);

        document.getElementById("compare-card-2-title").innerText = data.exp2.title;
        document.getElementById("compare-card-2-content").innerText = JSON.stringify(data.exp2.results, null, 2);

        document.getElementById("compare-modal").classList.add("active");
    });
}
