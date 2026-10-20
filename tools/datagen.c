/*
 * Native C Self-Play Dataset Generator for GOOB NNUE Training (Phase 2)
 *
 * Linked directly against GOOB engine objects, bypassing UCI/Python overhead.
 * Generates games using node-limited searches and outputs packed 68-byte
 * records matching fen_utils.py / nnue_dataset.py with WDL game outcomes.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "defs.h"
#include "board.h"
#include "bitboards.h"
#include "init.h"
#include "makemove.h"
#include "movegen.h"
#include "pvtable.h"
#include "search.h"
#include "nnue_loader.h"
#include "recog.h"
#include "trace.h"
#include "uci.h"
#include "io.h"
#include "misc.h"
#include "syzygy.h"

#define MAX_GAME_BUFFER 1024
#define DEFAULT_NODES 5000
#define DEFAULT_RESIGN_EVAL 1500
#define DEFAULT_RESIGN_PLIES 4
#define DEFAULT_DRAW_EVAL 10
#define DEFAULT_DRAW_PLIES 8
#define DEFAULT_DRAW_MOVE 40
#define DEFAULT_MAX_PLY 400
#define DEFAULT_HASH_MB 16

#pragma pack(push, 1)
typedef struct {
    uint8_t  board[64];  /* 0=empty, 1..6=wP..wK, 7..12=bP..bK */
    uint8_t  stm;        /* 0=white, 1=black */
    int16_t  eval;       /* white-relative centipawn eval, clipped to [-3000, 3000] */
    uint8_t  flags;      /* bit 0 = mate flag, bits 1..2 = game result (1=W, 2=D, 3=B) */
} BinaryRecord;
#pragma pack(pop)

static_assert(sizeof(BinaryRecord) == 68, "BinaryRecord must be exactly 68 bytes");

/* Xoshiro256** PRNG for reproducible, high-throughput move/opening randomization */
static uint64_t g_rng[4];

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t next_rand(void) {
    const uint64_t result = rotl(g_rng[1] * 5, 7) * 9;
    const uint64_t t = g_rng[1] << 17;
    g_rng[2] ^= g_rng[0];
    g_rng[3] ^= g_rng[1];
    g_rng[1] ^= g_rng[2];
    g_rng[0] ^= g_rng[3];
    g_rng[2] ^= t;
    g_rng[3] = rotl(g_rng[3], 45);
    return result;
}

static void seed_rand(uint64_t seed) {
    for (int i = 0; i < 4; i++) {
        uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        g_rng[i] = z ^ (z >> 31);
    }
}

typedef struct {
    char **fens;
    int count;
    int capacity;
} Book;

static void book_load(Book *book, const char *path) {
    book->fens = NULL;
    book->count = 0;
    book->capacity = 0;

    if (!path || !path[0]) return;
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("Note: opening book '%s' not found, defaulting to startpos.\n", path);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n' || p[len - 1] == ' ')) {
            p[--len] = '\0';
        }
        if (len == 0 || p[0] == '#') continue;

        if (book->count >= book->capacity) {
            book->capacity = book->capacity ? book->capacity * 2 : 16384;
            book->fens = (char **)realloc(book->fens, book->capacity * sizeof(char *));
        }
        book->fens[book->count++] = strdup(p);
    }
    fclose(f);
    printf("Loaded %d openings from '%s'.\n", book->count, path);
}

static void book_free(Book *book) {
    if (!book->fens) return;
    for (int i = 0; i < book->count; i++) free(book->fens[i]);
    free(book->fens);
    book->fens = NULL;
    book->count = 0;
}

static const char *book_pick(const Book *book) {
    if (book && book->count > 0) {
        return book->fens[next_rand() % book->count];
    }
    return START_FEN;
}

/* Map GOOB board piece constant to packed 68-byte record piece code */
static inline uint8_t piece_to_record_code(int pce) {
    if (pce == EMPTY) return 0;
    if (COLOR_OF(pce) == WHITE) {
        return (uint8_t)(TYPE_OF(pce));       /* 1..6: wP, wN, wB, wR, wQ, wK */
    } else {
        return (uint8_t)(6 + TYPE_OF(pce));   /* 7..12: bP, bN, bB, bR, bQ, bK */
    }
}

/* Recursive mkdir helper to create destination directory structure */
static int mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = strrchr(tmp, '/');
    if (!p) return 0;
    *p = '\0';
    for (char *s = tmp + 1; *s; s++) {
        if (*s == '/') {
            *s = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *s = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* Verify path safety against NNUE data protection rules */
static void verify_path_safety(const char *path) {
    if (!path || !path[0]) {
        fprintf(stderr, "FATAL: Output file path cannot be empty.\n");
        exit(1);
    }
    if (strstr(path, "data_selfplay/") || strstr(path, "data_selfplay") ||
        strstr(path, "checkpoints/") || strstr(path, "checkpoints") ||
        strstr(path, "quantised.bin") || strstr(path, "nnue.pt") ||
        strstr(path, "tools/nnue_project/data/") || strstr(path, "data/train.bin") ||
        strstr(path, "data/val.bin")) {
        fprintf(stderr, "FATAL: Output path '%s' violates NNUE protection rules.\n"
                        "Never overwrite existing training sets, checkpoints, or weights.\n", path);
        exit(1);
    }
}

static int is_threefold_repetition(const S_BOARD *pos) {
    U64 key = pos->st->posKey;
    int count = 1;
    int limit = pos->st->fiftyMove;
    StateInfo *st = pos->st->previous;
    for (int i = 1; i <= limit && st; i++) {
        if (st->posKey == key) {
            count++;
            if (count >= 3) return 1;
        }
        st = st->previous;
    }
    return 0;
}

static void print_usage(const char *prog) {
    printf("GOOB NNUE Native Self-Play Data Generator\n\n");
    printf("Usage:\n");
    printf("  %s --out <file> [options]\n\n", prog);
    printf("Required:\n");
    printf("  --out <path>         Destination binary file (refuses to overwrite existing file)\n\n");
    printf("Options:\n");
    printf("  --nodes <N>          Nodes per move search limit (default: %d)\n", DEFAULT_NODES);
    printf("  --positions <N>      Target number of quiet positions to generate (default: 0 = unlimited)\n");
    printf("  --games <N>          Target number of games to play (default: 0 = unlimited)\n");
    printf("  --seed <N>           Random number generator seed (default: 1)\n");
    printf("  --book <file>        Opening book in EPD/FEN format (default: tools/book.epd)\n");
    printf("  --random-plies <N>   Random legal moves at start (-1 = auto: 4 for book, 8 for startpos)\n");
    printf("  --resign-eval <N>    Resignation eval threshold in cp (default: %d)\n", DEFAULT_RESIGN_EVAL);
    printf("  --resign-plies <N>   Consecutive plies exceeding resign-eval to adjudicate (default: %d)\n", DEFAULT_RESIGN_PLIES);
    printf("  --draw-eval <N>      Draw adjudication eval threshold in cp (default: %d)\n", DEFAULT_DRAW_EVAL);
    printf("  --draw-plies <N>     Consecutive plies under draw-eval to adjudicate (default: %d)\n", DEFAULT_DRAW_PLIES);
    printf("  --draw-move <N>      Move number after which draw adjudication triggers (default: %d)\n", DEFAULT_DRAW_MOVE);
    printf("  --max-ply <N>        Maximum plies per game before draw adjudication (default: %d)\n", DEFAULT_MAX_PLY);
    printf("  --hash <N>           Transposition table hash size in MB (default: %d)\n", DEFAULT_HASH_MB);
    printf("  --help, -h           Show this help message\n");
}

int main(int argc, char **argv) {
    const char *out_path = NULL;
    int nodes_per_move = DEFAULT_NODES;
    uint64_t target_positions = 0;
    uint64_t target_games = 0;
    uint64_t seed = 1;
    const char *book_path = "tools/book.epd";
    int random_plies = -1;
    int resign_eval = DEFAULT_RESIGN_EVAL;
    int resign_plies = DEFAULT_RESIGN_PLIES;
    int draw_eval = DEFAULT_DRAW_EVAL;
    int draw_plies = DEFAULT_DRAW_PLIES;
    int draw_move = DEFAULT_DRAW_MOVE;
    int max_ply = DEFAULT_MAX_PLY;
    int hash_mb = DEFAULT_HASH_MB;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(argv[i], "--nodes") && i + 1 < argc) {
            nodes_per_move = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--positions") && i + 1 < argc) {
            target_positions = strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--games") && i + 1 < argc) {
            target_games = strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--book") && i + 1 < argc) {
            book_path = argv[++i];
        } else if (!strcmp(argv[i], "--random-plies") && i + 1 < argc) {
            random_plies = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--resign-eval") && i + 1 < argc) {
            resign_eval = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--resign-plies") && i + 1 < argc) {
            resign_plies = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--draw-eval") && i + 1 < argc) {
            draw_eval = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--draw-plies") && i + 1 < argc) {
            draw_plies = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--draw-move") && i + 1 < argc) {
            draw_move = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--max-ply") && i + 1 < argc) {
            max_ply = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--hash") && i + 1 < argc) {
            hash_mb = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s (see --help)\n", argv[i]);
            return 1;
        }
    }

    if (!out_path) {
        fprintf(stderr, "Error: --out <path> is required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    verify_path_safety(out_path);
    mkdir_p(out_path);

    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out_fd < 0) {
        if (errno == EEXIST) {
            fprintf(stderr, "FATAL: Output file '%s' already exists.\n"
                            "NNUE protection refuses to overwrite existing data.\n", out_path);
        } else {
            fprintf(stderr, "FATAL: Could not create output file '%s': %s\n", out_path, strerror(errno));
        }
        return 1;
    }
    FILE *out_fp = fdopen(out_fd, "wb");
    if (!out_fp) {
        fprintf(stderr, "FATAL: fdopen failed for '%s'\n", out_path);
        close(out_fd);
        return 1;
    }

    seed_rand(seed);

    Book book;
    book_load(&book, book_path);

    if (random_plies < 0) {
        random_plies = (book.count > 0) ? 4 : 8;
    }

    printf("================================================================================\n");
    printf("GOOB Datagen Configuration:\n");
    printf("  Output File      : %s\n", out_path);
    printf("  Nodes / Move     : %d\n", nodes_per_move);
    printf("  Target Positions : %" PRIu64 "%s\n", target_positions, target_positions ? "" : " (unlimited)");
    printf("  Target Games     : %" PRIu64 "%s\n", target_games, target_games ? "" : " (unlimited)");
    printf("  RNG Seed         : %" PRIu64 "\n", seed);
    printf("  Opening Book     : %s (%d openings)\n", book_path, book.count);
    printf("  Random Plies     : %d\n", random_plies);
    printf("  Resign Eval      : >= %d cp for %d consecutive plies\n", resign_eval, resign_plies);
    printf("  Draw Eval        : <= %d cp for %d consecutive plies after move %d\n", draw_eval, draw_plies, draw_move);
    printf("  Max Game Ply     : %d\n", max_ply);
    printf("  Hash Size        : %d MB\n", hash_mb);
    printf("================================================================================\n");
    fflush(stdout);

    /* Engine initialization */
    init_cpu_features();
    AllInit();

    S_BOARD pos[1];
    S_SEARCHINFO info[1];
    memset(pos, 0, sizeof(S_BOARD));
    memset(info, 0, sizeof(S_SEARCHINFO));
    pos->search = alloc_search_thread();
    info->quit = FALSE;
    info->threadNum = 1;

    pvTable->pTable = NULL;
    InitPvTable(pvTable, hash_mb, 0);

    size_t sharedSize = (sizeof(S_SHARED_TABLES) + 63) & ~(size_t)63;
    pos->shared = (S_SHARED_TABLES *)goob_aligned_alloc(64, sharedSize);
    if (!pos->shared) {
        fprintf(stderr, "FATAL: Shared table allocation failed\n");
        fclose(out_fp);
        return 1;
    }

    initStacks(pos);
    resetContinuationTable(pos);
    InitUciDefaults(pos, info);

    if (!nnue_init(NULL)) {
        fprintf(stderr, "FATAL: Embedded NNUE network failed to load\n");
        fclose(out_fp);
        return 1;
    }

    uint64_t total_positions = 0;
    uint64_t games_played = 0;
    int wins_white = 0, draws = 0, wins_black = 0;
    int t_start = getTimeMs();

    BinaryRecord game_buffer[MAX_GAME_BUFFER];

    while (TRUE) {
        if (target_positions > 0 && total_positions >= target_positions) break;
        if (target_games > 0 && games_played >= target_games) break;

        /* Reset board & state for new game */
        clearPvTable(pvTable);
        resetContinuationTable(pos);
        pos->gamePlyOffset = 0;
        pos->hisPly = 0;
        pos->ply = 0;

        const char *opening_fen = book_pick(&book);
        if (ParseFEN((char *)opening_fen, pos) != 0) {
            ParseFEN(START_FEN, pos);
        }
        nnue_refresh_accumulator(pos);

        /* Play initial random legal plies for diversity */
        bool opening_ok = true;
        for (int r = 0; r < random_plies; r++) {
            S_MOVELIST ml[1];
            GenerateAllMoves(pos, ml);
            int legal_moves[MAXPOSMOVES];
            int legal_count = 0;
            for (int m = 0; m < ml->count; m++) {
                if (legal(pos, ml->moves[m].move)) {
                    legal_moves[legal_count++] = ml->moves[m].move;
                }
            }
            if (legal_count == 0) {
                opening_ok = false;
                break;
            }
            int chosen = legal_moves[next_rand() % legal_count];
            if (pos->hisPly + 1 >= MAXGAMESMOVES) compactStateHistory(pos);
            makeMove(pos, chosen, &pos->stateTable[pos->hisPly + 1]);
            pos->ply = 0;
        }

        if (!opening_ok) continue;

        int game_records = 0;
        int consec_white_win = 0;
        int consec_black_win = 0;
        int consec_draw = 0;
        int result_code = 0; /* 1 = White win, 2 = Draw, 3 = Black win */
        const char *adjudication_reason = "none";

        while (pos->hisPly < max_ply && result_code == 0) {
            /* Check natural game-ending rules before searching */
            if (pos->st->fiftyMove >= 100) {
                result_code = 2;
                adjudication_reason = "fifty-move rule";
                break;
            }
            if (is_threefold_repetition(pos)) {
                result_code = 2;
                adjudication_reason = "threefold repetition";
                break;
            }
            if (drawByMaterial(pos) || drawKBPRP(pos)) {
                result_code = 2;
                adjudication_reason = "insufficient material";
                break;
            }

            /* Generate legal moves to test for checkmate/stalemate */
            S_MOVELIST ml[1];
            GenerateAllMoves(pos, ml);
            int has_legal = 0;
            for (int m = 0; m < ml->count; m++) {
                if (legal(pos, ml->moves[m].move)) {
                    has_legal = 1;
                    break;
                }
            }
            if (!has_legal) {
                if (pos->st->checkersBB != 0) {
                    /* Side to move is in check with no legal moves: checkmate! */
                    result_code = (pos->side == WHITE) ? 3 : 1;
                    adjudication_reason = "checkmate";
                } else {
                    /* Stalemate */
                    result_code = 2;
                    adjudication_reason = "stalemate";
                }
                break;
            }

            /* Perform node-limited search */
            info->starttime = getTimeMs();
            info->nodeSet = TRUE;
            info->nodeLimit = (U64)nodes_per_move;
            info->depth = MAXDEPTH - 1;
            info->depthSet = FALSE;
            info->timeSet = FALSE;
            info->softTimeSet = FALSE;
            info->UciInfinite = FALSE;
            info->ponder = FALSE;
            info->stopped = FALSE;
            info->depthOneComplete = FALSE;
            info->analyzeMode = EngineOptions->analysisMode;
            info->mateLimit = -1;
            info->EloNodelimit = 0;
            info->silent = 1;

            SearchPosition(pos, info, pvTable);

            int bestMove = info->bestMove;
            int score = info->bestScore;

            if (bestMove == NOMOVE || !legal(pos, bestMove)) {
                for (int m = 0; m < ml->count; m++) {
                    if (legal(pos, ml->moves[m].move)) {
                        bestMove = ml->moves[m].move;
                        break;
                    }
                }
            }

            if (bestMove == NOMOVE) {
                result_code = 2;
                adjudication_reason = "no legal move";
                break;
            }

            /* Score converted to White's point of view */
            int white_score = (pos->side == WHITE) ? score : -score;

            /* Check quietness:
             * 1. Side to move not in check
             * 2. Best move not a capture / promotion / en-passant
             * 3. Best move does not give check
             */
            int in_check = (pos->st->checkersBB != 0);
            int tactical = moveIsTactical(pos, bestMove);
            int gives_check = 0;

            if (!in_check && !tactical) {
                StateInfo tmp_st;
                makeMove(pos, bestMove, &tmp_st);
                gives_check = (pos->st->checkersBB != 0);
                takeMove(pos);
            }

            if (!in_check && !tactical && !gives_check && game_records < MAX_GAME_BUFFER) {
                BinaryRecord *rec = &game_buffer[game_records++];
                for (int sq = 0; sq < 64; sq++) {
                    rec->board[sq] = piece_to_record_code(pos->pieces[sq]);
                }
                rec->stm = (uint8_t)pos->side;

                int is_mate = (abs(score) >= ISMATE);
                int eval_cp = is_mate ? ((white_score > 0) ? 3000 : -3000) : white_score;
                if (eval_cp > 3000) eval_cp = 3000;
                else if (eval_cp < -3000) eval_cp = -3000;

                rec->eval = (int16_t)eval_cp;
                rec->flags = (uint8_t)(is_mate & 1);
            }

            /* Play the move */
            if (pos->hisPly + 1 >= MAXGAMESMOVES) compactStateHistory(pos);
            makeMove(pos, bestMove, &pos->stateTable[pos->hisPly + 1]);
            pos->ply = 0;

            /* Adjudication checks */
            if (white_score >= resign_eval) {
                consec_white_win++;
                consec_black_win = 0;
            } else if (white_score <= -resign_eval) {
                consec_black_win++;
                consec_white_win = 0;
            } else {
                consec_white_win = 0;
                consec_black_win = 0;
            }

            if (consec_white_win >= resign_plies) {
                result_code = 1;
                adjudication_reason = "resignation (White winning)";
                break;
            }
            if (consec_black_win >= resign_plies) {
                result_code = 3;
                adjudication_reason = "resignation (Black winning)";
                break;
            }

            /* Draw adjudication after draw_move full moves */
            if (pos->hisPly >= draw_move * 2) {
                if (abs(white_score) <= draw_eval) {
                    consec_draw++;
                    if (consec_draw >= draw_plies) {
                        result_code = 2;
                        adjudication_reason = "eval draw adjudication";
                        break;
                    }
                } else {
                    consec_draw = 0;
                }
            }
        }

        if (result_code == 0 && pos->hisPly >= max_ply) {
            result_code = 2;
            adjudication_reason = "max ply reached";
        }

        if (result_code != 0) {
            /* Stamp game outcome into record flags (bits 1..2) */
            for (int r = 0; r < game_records; r++) {
                game_buffer[r].flags |= (uint8_t)(result_code << 1);
            }
            if (game_records > 0) {
                size_t written = fwrite(game_buffer, sizeof(BinaryRecord), game_records, out_fp);
                if (written != (size_t)game_records) {
                    fprintf(stderr, "FATAL: Disk write error: %s\n", strerror(errno));
                    break;
                }
                fflush(out_fp);
                total_positions += game_records;
            }

            games_played++;
            if (result_code == 1) wins_white++;
            else if (result_code == 2) draws++;
            else if (result_code == 3) wins_black++;

            int elapsed = getTimeMs() - t_start;
            if (elapsed <= 0) elapsed = 1;
            double pos_per_sec = (double)total_positions * 1000.0 / (double)elapsed;
            double draw_pct = 100.0 * (double)draws / (double)games_played;

            const char *res_str = (result_code == 1) ? "1-0" : (result_code == 2) ? "1/2-1/2" : "0-1";
            printf("Game %5" PRIu64 " | Pos: %8" PRIu64 " (+%2d) | %-7s (%s) | W/D/L: %d/%d/%d (%.1f%% D) | %.1f pos/s\n",
                   games_played, total_positions, game_records, res_str, adjudication_reason,
                   wins_white, draws, wins_black, draw_pct, pos_per_sec);
            fflush(stdout);
        }
    }

    int elapsed_total = getTimeMs() - t_start;
    if (elapsed_total <= 0) elapsed_total = 1;
    double final_rate = (double)total_positions * 1000.0 / (double)elapsed_total;
    double final_draw_pct = games_played ? (100.0 * (double)draws / (double)games_played) : 0.0;

    printf("\n================================================================================\n");
    printf("Generation Complete:\n");
    printf("  Games Played     : %" PRIu64 "\n", games_played);
    printf("  Positions Written: %" PRIu64 "\n", total_positions);
    printf("  Avg Pos / Game   : %.1f\n", games_played ? ((double)total_positions / (double)games_played) : 0.0);
    printf("  Score (W/D/L)    : %d / %d / %d (%.1f%% draws)\n", wins_white, draws, wins_black, final_draw_pct);
    printf("  Total Time       : %.2f s (%.1f pos/s)\n", (double)elapsed_total / 1000.0, final_rate);
    printf("  Output File      : %s\n", out_path);
    printf("================================================================================\n");

    fclose(out_fp);
    book_free(&book);

    if (pvTable->pTable) goob_aligned_free(pvTable->pTable);
    FreeThreadPool();
    if (pos->shared) goob_aligned_free(pos->shared);
    if (pos->search) free_search_thread(pos->search);
    TBFree();

    return 0;
}
