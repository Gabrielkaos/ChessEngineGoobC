/**
 * Canvas Charting Library for Goob Chess Engine Research Laboratory.
 * Zero-dependency, lightweight, responsive charts for search scores,
 * cutoff distributions, and SPSA trajectories.
 */

function drawScoreTrajectory(canvasId, iterations) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const width = canvas.width = canvas.parentElement.clientWidth;
    const height = canvas.height = canvas.parentElement.clientHeight || 200;

    ctx.clearRect(0, 0, width, height);

    if (!iterations || iterations.length < 2) {
        ctx.fillStyle = "#64748b";
        ctx.font = "12px sans-serif";
        ctx.textAlign = "center";
        ctx.fillText("Iterative deepening evaluation will appear as Goob searches...", width / 2, height / 2);
        return;
    }

    const padding = { top: 20, right: 30, bottom: 30, left: 50 };
    const chartW = width - padding.left - padding.right;
    const chartH = height - padding.top - padding.bottom;

    // Determine range of scores
    const scores = iterations.map(it => it.score_cp !== null ? it.score_cp / 100.0 : 0.0);
    const depths = iterations.map(it => it.depth);

    let minScore = Math.min(-1.0, ...scores);
    let maxScore = Math.max(1.0, ...scores);
    const scoreRange = Math.max(2.0, maxScore - minScore);

    // Draw grid
    ctx.strokeStyle = "#334155";
    ctx.lineWidth = 1;

    // Zero line
    const zeroY = padding.top + chartH * (1.0 - (0.0 - minScore) / scoreRange);
    ctx.beginPath();
    ctx.setLineDash([4, 4]);
    ctx.moveTo(padding.left, zeroY);
    ctx.lineTo(width - padding.right, zeroY);
    ctx.stroke();
    ctx.setLineDash([]);

    // Y Axis labels
    ctx.fillStyle = "#94a3b8";
    ctx.font = "10px monospace";
    ctx.textAlign = "right";
    ctx.fillText(`${maxScore.toFixed(1)}`, padding.left - 8, padding.top + 10);
    ctx.fillText("0.0", padding.left - 8, zeroY + 3);
    ctx.fillText(`${minScore.toFixed(1)}`, padding.left - 8, padding.top + chartH);

    // Plot line
    ctx.strokeStyle = "#3b82f6";
    ctx.lineWidth = 2.5;
    ctx.beginPath();

    const points = [];
    for (let i = 0; i < iterations.length; i++) {
        const x = padding.left + (i / (iterations.length - 1)) * chartW;
        const y = padding.top + chartH * (1.0 - (scores[i] - minScore) / scoreRange);
        points.push({ x, y, score: scores[i], depth: depths[i] });

        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();

    // Plot nodes
    for (const pt of points) {
        ctx.fillStyle = pt.score >= 0 ? "#10b981" : "#ef4444";
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, 4, 0, Math.PI * 2);
        ctx.fill();

        // Depth label
        ctx.fillStyle = "#cbd5e1";
        ctx.font = "10px monospace";
        ctx.textAlign = "center";
        ctx.fillText(`d${pt.depth}`, pt.x, padding.top + chartH + 18);
    }
}

function drawCutoffDistribution(canvasId, cutoffs) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const width = canvas.width = canvas.parentElement.clientWidth;
    const height = canvas.height = canvas.parentElement.clientHeight || 220;

    ctx.clearRect(0, 0, width, height);

    if (!cutoffs || !cutoffs.total) {
        ctx.fillStyle = "#64748b";
        ctx.font = "12px sans-serif";
        ctx.textAlign = "center";
        ctx.fillText("No cutoff trace recorded. Run Trace Analysis on a position.", width / 2, height / 2);
        return;
    }

    const total = cutoffs.total;
    const categories = [
        { label: "1st Move", count: cutoffs.first_move, color: "#10b981" },
        { label: "TT Move", count: cutoffs.tt_move, color: "#3b82f6" },
        { label: "Killer", count: cutoffs.killer, color: "#f59e0b" },
        { label: "Counter", count: cutoffs.counter, color: "#8b5cf6" },
        { label: "Followup", count: cutoffs.followup, color: "#ec4899" },
        { label: "Quiet", count: cutoffs.quiet, color: "#64748b" },
        { label: "Tactical", count: cutoffs.noisy, color: "#ef4444" }
    ];

    const padding = { top: 15, right: 60, bottom: 20, left: 80 };
    const chartW = width - padding.left - padding.right;
    const barHeight = Math.max(12, Math.floor((height - padding.top - padding.bottom) / categories.length) - 8);

    for (let i = 0; i < categories.length; i++) {
        const cat = categories[i];
        const pct = (cat.count / total) * 100.0;
        const y = padding.top + i * (barHeight + 8);
        const barW = (pct / 100.0) * chartW;

        // Label
        ctx.fillStyle = "#94a3b8";
        ctx.font = "11px sans-serif";
        ctx.textAlign = "right";
        ctx.fillText(cat.label, padding.left - 10, y + barHeight - 2);

        // Bar background
        ctx.fillStyle = "#1e293b";
        ctx.fillRect(padding.left, y, chartW, barHeight);

        // Bar fill
        ctx.fillStyle = cat.color;
        ctx.fillRect(padding.left, y, barW, barHeight);

        // Percentage text
        ctx.fillStyle = "#f8fafc";
        ctx.font = "10px monospace";
        ctx.textAlign = "left";
        ctx.fillText(`${pct.toFixed(1)}% (${cat.count})`, padding.left + barW + 8, y + barHeight - 2);
    }
}

function drawSPSATrajectory(canvasId, history, selectedParams) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const width = canvas.width = canvas.parentElement.clientWidth;
    const height = canvas.height = canvas.parentElement.clientHeight || 240;

    ctx.clearRect(0, 0, width, height);

    if (!history || history.length < 2) {
        ctx.fillStyle = "#64748b";
        ctx.font = "12px sans-serif";
        ctx.textAlign = "center";
        ctx.fillText("Tuning trajectory will plot here as iterations proceed...", width / 2, height / 2);
        return;
    }

    const padding = { top: 20, right: 30, bottom: 30, left: 60 };
    const chartW = width - padding.left - padding.right;
    const chartH = height - padding.top - padding.bottom;

    const colors = ["#3b82f6", "#10b981", "#f59e0b", "#8b5cf6", "#ec4899", "#06b6d4"];

    // Find min and max across all points for chosen parameters
    const keys = selectedParams || Object.keys(history[0]).filter(k => k !== "iter" && k !== "time" && k !== "game");
    const activeKeys = keys.slice(0, 4);

    let globalMin = Infinity;
    let globalMax = -Infinity;

    for (const row of history) {
        for (const k of activeKeys) {
            if (row[k] !== undefined) {
                const v = typeof row[k] === "number" ? row[k] : parseFloat(row[k]);
                if (!isNaN(v)) {
                    if (v < globalMin) globalMin = v;
                    if (v > globalMax) globalMax = v;
                }
            }
        }
    }

    if (globalMin === Infinity) return;
    const valRange = Math.max(1, globalMax - globalMin);

    // Grid lines
    ctx.strokeStyle = "#334155";
    ctx.lineWidth = 1;
    ctx.strokeRect(padding.left, padding.top, chartW, chartH);

    // Y Axis labels
    ctx.fillStyle = "#94a3b8";
    ctx.font = "10px monospace";
    ctx.textAlign = "right";
    ctx.fillText(`${globalMax.toFixed(0)}`, padding.left - 8, padding.top + 10);
    ctx.fillText(`${globalMin.toFixed(0)}`, padding.left - 8, padding.top + chartH);

    // Plot each parameter trajectory
    activeKeys.forEach((key, kIdx) => {
        const color = colors[kIdx % colors.length];
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();

        for (let i = 0; i < history.length; i++) {
            const v = history[i][key];
            if (v === undefined) continue;
            const num = typeof v === "number" ? v : parseFloat(v);
            const x = padding.left + (i / (history.length - 1)) * chartW;
            const y = padding.top + chartH * (1.0 - (num - globalMin) / valRange);

            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // Legend at top
        ctx.fillStyle = color;
        ctx.font = "11px sans-serif";
        ctx.textAlign = "left";
        ctx.fillText(key, padding.left + kIdx * 110, padding.top - 6);
    });
}
