/**
 * ChessBoard Component for Goob Chess Engine Research Laboratory.
 * Renders an interactive SVG-based chessboard, displays legal moves,
 * and communicates with the backend for FEN and move state.
 */

const PIECE_SVGS = {
    // White pieces
    'P': '<svg viewBox="0 0 45 45"><path d="M22.5 9c-2.21 0-4 1.79-4 4 0 .89.29 1.71.78 2.38C17.33 16.5 16 18.59 16 21c0 2.03.94 3.84 2.41 5.03-3 1.06-7.41 5.55-7.41 13.47h23c0-7.92-4.41-12.41-7.41-13.47 1.47-1.19 2.41-3 2.41-5.03 0-2.41-1.33-4.5-3.28-5.62.49-.67.78-1.49.78-2.38 0-2.21-1.79-4-4-4z" fill="#fff" stroke="#000" stroke-width="1.5" stroke-linecap="round"/></svg>',
    'N': '<svg viewBox="0 0 45 45"><path d="M22 10c10.5 1 16.5 8 16 29H15c0-9 10-6.5 8-21" fill="#fff" stroke="#000" stroke-width="1.5"/><path d="M24 18c.38 2.91-5.55 7.37-8 9-3 2-2.82 4.34-5 4-1.042-.94 1.41-3.04 0-3-1 0-.932 1.27-2 2.5-1 1-2.99 1.15-4 .5-.68-.42.6-1.57.5-2.5-.2-2-5.11-4.94-5-7.5.11-2.56 5.8-5.32 8-5.5 2-.18 6.5-1.5 8.5-3.5 1.5-1.5 3-4 3-4s6.5 1 7 8z" fill="#fff" stroke="#000" stroke-width="1.5"/></svg>',
    'B': '<svg viewBox="0 0 45 45"><g fill="none" stroke="#000" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><g fill="#fff"><path d="M9 36c3.39-.97 10.11.43 13.5-2 3.39 2.43 10.11 1.03 13.5 2 0 0 1.65.54 3 2-.68.97-1.65.99-3 .5-3.39-.97-10.11.46-13.5-1-3.39 1.46-10.11.03-13.5 1-1.354.49-2.323.47-3-.5 1.354-1.94 3-2 3-2zM15 32c2.5 2.5 12.5 2.5 15 0 .5-1.5 0-2 0-2 0-2.5-2.5-4-2.5-4 5.5-1.5 6-11.5-5-15.5-11 4-10.5 14-5 15.5 0 0-2.5 1.5-2.5 4 0 0-.5.5 0 2zM25 8a2.5 2.5 0 1 1-5 0 2.5 2.5 0 1 1 5 0z"/></g><path d="M17.5 26h10M15 30h15m-7.5-14.5v5M20 18h5"/></g></svg>',
    'R': '<svg viewBox="0 0 45 45"><g fill="#fff" stroke="#000" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M9 39h27v-3H9v3zM12 36v-4h21v4H12zM11 14V9h4v2h5V9h5v2h5V9h4v5" stroke-linecap="butt"/><path d="M34 14l-3 3H14l-3-3"/><path d="M31 17v12.5H14V17"/><path d="M31 29.5l1.5 2.5h-20l1.5-2.5"/></g></svg>',
    'Q': '<svg viewBox="0 0 45 45"><g fill="#fff" stroke="#000" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M8 12a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM24.5 7.5a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM41 12a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM16 8.5a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM33 8.5a2 2 0 1 1-4 0 2 2 0 1 1 4 0z"/><path d="M9 26c8.5-1.5 21-1.5 27 0l2-12-7 11-6.5-13.5-6.5 13.5-7-11 2 12zM9 26c0 2 1.5 2 2.5 4 1 1.5 1 1 .5 3.5-1.5 1-1.5 2.5-1.5 2.5-1.5 1.5.5 2.5.5 2.5 6.5 1 16.5 1 23 0 0 0 2-1 .5-2.5 0 0 0-1.5-1.5-2.5-.5-2.5-.5-2 .5-3.5 1-2 2.5-2 2.5-4-8.5-1.5-18.5-1.5-27 0z"/></g></svg>',
    'K': '<svg viewBox="0 0 45 45"><g fill="none" stroke="#000" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M22.5 11.63V6M20 8h5" stroke-linejoin="miter"/><path d="M22.5 25s4.5-7.5 3-10.5c0 0-1-2.5-3-2.5s-3 2.5-3 2.5c-1.5 3 3 10.5 3 10.5" fill="#fff"/><path d="M11.5 37c5.5 3.5 15.5 3.5 21 0v-7s9-4.5 6-10.5c-4-1-6 2.5-6 2.5s-3.5-3.5-7-2c-3.5-1.5-7 2-7 2s-2-3.5-6-2.5c-3 6 6 10.5 6 10.5v7z" fill="#fff"/><path d="M11.5 30c5.5-3 15.5-3 21 0m-21 3.5c5.5-3 15.5-3 21 0m-21 3.5c5.5-3 15.5-3 21 0"/></g></svg>',

    // Black pieces
    'p': '<svg viewBox="0 0 45 45"><path d="M22.5 9c-2.21 0-4 1.79-4 4 0 .89.29 1.71.78 2.38C17.33 16.5 16 18.59 16 21c0 2.03.94 3.84 2.41 5.03-3 1.06-7.41 5.55-7.41 13.47h23c0-7.92-4.41-12.41-7.41-13.47 1.47-1.19 2.41-3 2.41-5.03 0-2.41-1.33-4.5-3.28-5.62.49-.67.78-1.49.78-2.38 0-2.21-1.79-4-4-4z" fill="#333" stroke="#fff" stroke-width="1.5" stroke-linecap="round"/></svg>',
    'n': '<svg viewBox="0 0 45 45"><path d="M22 10c10.5 1 16.5 8 16 29H15c0-9 10-6.5 8-21" fill="#333" stroke="#fff" stroke-width="1.5"/><path d="M24 18c.38 2.91-5.55 7.37-8 9-3 2-2.82 4.34-5 4-1.042-.94 1.41-3.04 0-3-1 0-.932 1.27-2 2.5-1 1-2.99 1.15-4 .5-.68-.42.6-1.57.5-2.5-.2-2-5.11-4.94-5-7.5.11-2.56 5.8-5.32 8-5.5 2-.18 6.5-1.5 8.5-3.5 1.5-1.5 3-4 3-4s6.5 1 7 8z" fill="#333" stroke="#fff" stroke-width="1.5"/></svg>',
    'b': '<svg viewBox="0 0 45 45"><g fill="none" stroke="#fff" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><g fill="#333"><path d="M9 36c3.39-.97 10.11.43 13.5-2 3.39 2.43 10.11 1.03 13.5 2 0 0 1.65.54 3 2-.68.97-1.65.99-3 .5-3.39-.97-10.11.46-13.5-1-3.39 1.46-10.11.03-13.5 1-1.354.49-2.323.47-3-.5 1.354-1.94 3-2 3-2zM15 32c2.5 2.5 12.5 2.5 15 0 .5-1.5 0-2 0-2 0-2.5-2.5-4-2.5-4 5.5-1.5 6-11.5-5-15.5-11 4-10.5 14-5 15.5 0 0-2.5 1.5-2.5 4 0 0-.5.5 0 2zM25 8a2.5 2.5 0 1 1-5 0 2.5 2.5 0 1 1 5 0z"/></g><path d="M17.5 26h10M15 30h15m-7.5-14.5v5M20 18h5"/></g></svg>',
    'r': '<svg viewBox="0 0 45 45"><g fill="#333" stroke="#fff" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M9 39h27v-3H9v3zM12 36v-4h21v4H12zM11 14V9h4v2h5V9h5v2h5V9h4v5" stroke-linecap="butt"/><path d="M34 14l-3 3H14l-3-3"/><path d="M31 17v12.5H14V17"/><path d="M31 29.5l1.5 2.5h-20l1.5-2.5"/></g></svg>',
    'q': '<svg viewBox="0 0 45 45"><g fill="#333" stroke="#fff" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M8 12a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM24.5 7.5a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM41 12a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM16 8.5a2 2 0 1 1-4 0 2 2 0 1 1 4 0zM33 8.5a2 2 0 1 1-4 0 2 2 0 1 1 4 0z"/><path d="M9 26c8.5-1.5 21-1.5 27 0l2-12-7 11-6.5-13.5-6.5 13.5-7-11 2 12zM9 26c0 2 1.5 2 2.5 4 1 1.5 1 1 .5 3.5-1.5 1-1.5 2.5-1.5 2.5-1.5 1.5.5 2.5.5 2.5 6.5 1 16.5 1 23 0 0 0 2-1 .5-2.5 0 0 0-1.5-1.5-2.5-.5-2.5-.5-2 .5-3.5 1-2 2.5-2 2.5-4-8.5-1.5-18.5-1.5-27 0z"/></g></svg>',
    'k': '<svg viewBox="0 0 45 45"><g fill="none" stroke="#fff" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M22.5 11.63V6M20 8h5" stroke-linejoin="miter"/><path d="M22.5 25s4.5-7.5 3-10.5c0 0-1-2.5-3-2.5s-3 2.5-3 2.5c-1.5 3 3 10.5 3 10.5" fill="#333"/><path d="M11.5 37c5.5 3.5 15.5 3.5 21 0v-7s9-4.5 6-10.5c-4-1-6 2.5-6 2.5s-3.5-3.5-7-2c-3.5-1.5-7 2-7 2s-2-3.5-6-2.5c-3 6 6 10.5 6 10.5v7z" fill="#333"/><path d="M11.5 30c5.5-3 15.5-3 21 0m-21 3.5c5.5-3 15.5-3 21 0m-21 3.5c5.5-3 15.5-3 21 0"/></g></svg>'
};

class ChessBoard {
    constructor(containerId, onMoveCallback) {
        this.container = document.getElementById(containerId);
        this.onMove = onMoveCallback;
        this.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        this.isFlipped = false;
        this.selectedSquare = null;
        this.legalMoves = [];
        this.prevMove = null;
        this.boardState = null;

        this.initDOM();
        this.loadFen(this.fen);
    }

    initDOM() {
        this.container.innerHTML = "";
        this.container.classList.add("chessboard");

        this.squares = {};
        for (let r = 7; r >= 0; r--) {
            for (let f = 0; f < 8; f++) {
                const sq = String.fromCharCode(97 + f) + (r + 1);
                const el = document.createElement("div");
                el.className = `square ${(r + f) % 2 === 0 ? "dark" : "light"}`;
                el.dataset.sq = sq;

                el.addEventListener("click", () => this.handleSquareClick(sq));
                this.squares[sq] = el;
                this.container.appendChild(el);
            }
        }
    }

    async loadFen(fen) {
        this.fen = fen;
        this.selectedSquare = null;

        // Fetch position state & legal moves from backend
        try {
            const resp = await fetch("/api/position/validate", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ fen: this.fen })
            });
            this.boardState = await resp.json();
            this.legalMoves = this.boardState.legal_moves || [];
        } catch (e) {
            console.error("Failed to validate FEN:", e);
        }

        this.render();
    }

    flip() {
        this.isFlipped = !this.isFlipped;
        this.render();
    }

    render() {
        const rows = this.fen.split(" ")[0].split("/");
        const pieces = {};

        for (let r = 0; r < 8; r++) {
            const rowStr = rows[r];
            let f = 0;
            for (let i = 0; i < rowStr.length; i++) {
                const ch = rowStr[i];
                if (ch >= '1' && ch <= '8') {
                    f += parseInt(ch, 10);
                } else {
                    const sq = String.fromCharCode(97 + f) + (8 - r);
                    pieces[sq] = ch;
                    f++;
                }
            }
        }

        // Clear square contents
        this.container.innerHTML = "";
        const ranks = this.isFlipped ? [1, 2, 3, 4, 5, 6, 7, 8] : [8, 7, 6, 5, 4, 3, 2, 1];
        const files = this.isFlipped ? [7, 6, 5, 4, 3, 2, 1, 0] : [0, 1, 2, 3, 4, 5, 6, 7];

        for (const r of ranks) {
            for (const f of files) {
                const sq = String.fromCharCode(97 + f) + r;
                const el = document.createElement("div");
                const isLight = (r + f) % 2 === 0;
                el.className = `square ${isLight ? "dark" : "light"}`;
                el.dataset.sq = sq;

                if (this.selectedSquare === sq) {
                    el.classList.add("selected");
                }
                if (this.prevMove && (this.prevMove.from === sq || this.prevMove.to === sq)) {
                    el.classList.add("prev-move");
                }

                // Check highlighting
                if (this.boardState && this.boardState.is_check) {
                    const kingChar = this.boardState.turn === "w" ? "K" : "k";
                    if (pieces[sq] === kingChar) {
                        el.classList.add("in-check");
                    }
                }

                // Legal targets
                if (this.selectedSquare) {
                    const targets = this.legalMoves.filter(m => m.from_sq === this.selectedSquare);
                    const targetMove = targets.find(m => m.to_sq === sq);
                    if (targetMove) {
                        if (targetMove.is_capture || pieces[sq]) {
                            el.classList.add("legal-capture");
                        } else {
                            el.classList.add("legal-target");
                        }
                    }
                }

                // Piece rendering
                if (pieces[sq]) {
                    const p = pieces[sq];
                    const pieceEl = document.createElement("div");
                    pieceEl.className = "piece";
                    pieceEl.setAttribute("draggable", "true");
                    pieceEl.innerHTML = PIECE_SVGS[p] || "";
                    pieceEl.addEventListener("dragstart", (e) => {
                        e.dataTransfer.setData("text/plain", sq);
                        this.selectedSquare = sq;
                        this.render();
                    });
                    el.appendChild(pieceEl);
                }

                // Coordinate labels
                if (f === (this.isFlipped ? 7 : 0)) {
                    const rankLabel = document.createElement("span");
                    rankLabel.className = "board-coord rank";
                    rankLabel.innerText = r;
                    el.appendChild(rankLabel);
                }
                if (r === (this.isFlipped ? 8 : 1)) {
                    const fileLabel = document.createElement("span");
                    fileLabel.className = "board-coord file";
                    fileLabel.innerText = String.fromCharCode(97 + f);
                    el.appendChild(fileLabel);
                }

                el.addEventListener("dragover", (e) => e.preventDefault());
                el.addEventListener("drop", (e) => {
                    e.preventDefault();
                    const fromSq = e.dataTransfer.getData("text/plain");
                    if (fromSq && fromSq !== sq) {
                        const candidateMove = this.legalMoves.find(
                            m => m.from_sq === fromSq && m.to_sq === sq
                        );
                        if (candidateMove) {
                            this.selectedSquare = null;
                            this.applyMove(candidateMove);
                            return;
                        }
                    }
                    this.selectedSquare = null;
                    this.render();
                });

                el.addEventListener("click", () => this.handleSquareClick(sq));
                this.container.appendChild(el);
            }
        }
    }

    handleSquareClick(sq) {
        if (!this.selectedSquare) {
            // Select piece if it has legal moves
            const hasMoves = this.legalMoves.some(m => m.from_sq === sq);
            if (hasMoves) {
                this.selectedSquare = sq;
                this.render();
            }
        } else {
            // Move or change selection
            if (this.selectedSquare === sq) {
                this.selectedSquare = null;
                this.render();
                return;
            }

            const candidateMove = this.legalMoves.find(
                m => m.from_sq === this.selectedSquare && m.to_sq === sq
            );

            if (candidateMove) {
                this.selectedSquare = null;
                this.applyMove(candidateMove);
            } else {
                const hasMoves = this.legalMoves.some(m => m.from_sq === sq);
                this.selectedSquare = hasMoves ? sq : null;
                this.render();
            }
        }
    }

    async applyMove(candidateMove) {
        try {
            const resp = await fetch("/api/position/make-move", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    fen: this.fen,
                    move: candidateMove.uci
                })
            });
            if (!resp.ok) {
                this.render();
                return;
            }
            const res = await resp.json();
            this.prevMove = { from: candidateMove.from_sq, to: candidateMove.to_sq };
            await this.loadFen(res.fen);
            if (this.onMove) {
                this.onMove(candidateMove, res);
            }
        } catch (e) {
            console.error("Failed to apply move:", e);
            this.render();
        }
    }
}
