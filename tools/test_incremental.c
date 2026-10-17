#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/defs.h"
#include "../src/board.h"
#include "../src/bitboards.h"
#include "../src/io.h"
#include "../src/init.h"
#include "../src/makemove.h"
#include "../src/movegen.h"
#include "../src/nnue_loader.h"

static int verify_acc_matches_fresh(S_BOARD *pos, const char *ctx) {
    int inc_eval = nnue_eval(pos);
    int ply = pos->ply;
    int16_t inc_w[NNUE_ACC_SIZE], inc_b[NNUE_ACC_SIZE];
    uint8_t flip_w = pos->search->nnue_accumulators[ply].flip[WHITE];
    uint8_t flip_b = pos->search->nnue_accumulators[ply].flip[BLACK];
    memcpy(inc_w, pos->search->nnue_accumulators[ply].accumulation[WHITE], sizeof(inc_w));
    memcpy(inc_b, pos->search->nnue_accumulators[ply].accumulation[BLACK], sizeof(inc_b));

    nnue_refresh_accumulator(pos);
    int fresh_eval = nnue_eval(pos);

    int diff_w = memcmp(inc_w, pos->search->nnue_accumulators[ply].accumulation[WHITE], sizeof(inc_w));
    int diff_b = memcmp(inc_b, pos->search->nnue_accumulators[ply].accumulation[BLACK], sizeof(inc_b));
    int diff_fw = (flip_w != pos->search->nnue_accumulators[ply].flip[WHITE]);
    int diff_fb = (flip_b != pos->search->nnue_accumulators[ply].flip[BLACK]);

    if (diff_w != 0 || diff_b != 0 || diff_fw || diff_fb || inc_eval != fresh_eval) {
        printf("FAIL (%s): inc_eval=%d fresh_eval=%d diff_w=%d diff_b=%d diff_fw=%d diff_fb=%d\n",
               ctx, inc_eval, fresh_eval, diff_w, diff_b, diff_fw, diff_fb);
        return 0;
    }
    return 1;
}

static int test_position_moves(S_BOARD *pos, const char *fen, const char *move_strs[], int num_moves) {
    ParseFEN((char *)fen, pos);
    nnue_refresh_accumulator(pos);

    printf("Testing FEN: %s\n", fen);
    int initial_eval = nnue_eval(pos);
    printf("  Initial Eval: %d\n", initial_eval);

    for (int i = 0; i < num_moves; i++) {
        const char *mstr = move_strs[i];
        int move = ParseMove((char *)mstr, pos);
        if (move == NOMOVE) {
            printf("FAIL: Could not parse move '%s'\n", mstr);
            return 0;
        }

        makeMove(pos, move, &pos->stateTable[pos->hisPly + 1]);
        int inc_eval = nnue_eval(pos);
        if (!verify_acc_matches_fresh(pos, mstr)) {
            return 0;
        }
        printf("  Move %2d: %-6s -> Eval: %5d (inc==fresh OK)\n", i + 1, mstr, inc_eval);
    }

    for (int i = num_moves - 1; i >= 0; i--) {
        takeMove(pos);
        (void)nnue_eval(pos);
    }

    int back_eval = nnue_eval(pos);
    if (back_eval != initial_eval) {
        printf("FAIL after unmaking all moves: back_eval=%d initial_eval=%d\n", back_eval, initial_eval);
        return 0;
    }
    printf("  All %d moves unmade cleanly; returned to initial eval: %d\n\n", num_moves, back_eval);
    return 1;
}

int main(int argc, char **argv) {
    AllInit();

    S_BOARD pos[1];
    pos->search = alloc_search_thread();

    const char *net_path = (argc > 1) ? argv[1] : "src/weights/quantised.bin";
    if (!nnue_init(net_path)) {
        printf("FAIL: Could not load weights from '%s'\n", net_path);
        return 1;
    }

    int total_tests = 0;
    int passed_tests = 0;

    // Test 1: Standard Opening Game (quiet moves, captures, castling)
    {
        total_tests++;
        const char *moves[] = {
            "e2e4", "e7e5",
            "g1f3", "b8c6",
            "f1c4", "g8f6",
            "d2d3", "f8c5",
            "e1g1", "e8g8",
            "c2c3", "d7d6",
            "b2b4", "c5b6",
            "a2a4", "a7a6",
            "b1d2", "c6e7"
        };
        if (test_position_moves(pos, START_FEN, moves, 16)) passed_tests++;
    }

    // Test 2: Queenside Castling Game for BOTH colors (e1c1 and e8c8 cross d/e boundary!)
    {
        total_tests++;
        const char *moves[] = {
            "d2d4", "d7d5",
            "b1c3", "b8c6",
            "c1f4", "c8f5",
            "d1d2", "d8d7",
            "e1c1", "e8c8",   // Both White and Black castle queenside across d/e line!
            "c1b1", "c8b8"
        };
        if (test_position_moves(pos, START_FEN, moves, 12)) passed_tests++;
    }

    // Test 3: En Passant capture (White & Black)
    {
        total_tests++;
        const char *fen = "rnbqkbnr/pppp2pp/4p3/4Pp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3";
        const char *moves[] = { "e5f6", "g8f6" };
        if (test_position_moves(pos, fen, moves, 2)) passed_tests++;
    }

    // Test 4: En Passant capture by Black
    {
        total_tests++;
        const char *fen = "rnbqkbnr/pppp1ppp/8/8/3Pp3/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 3";
        const char *moves[] = { "e4d3", "c2d3" };
        if (test_position_moves(pos, fen, moves, 2)) passed_tests++;
    }

    // Test 5: Promotions (Q, R, B, N) and capture promotion
    {
        const char *proms[] = { "e7e8q", "e7e8r", "e7e8b", "e7e8n" };
        for (int p = 0; p < 4; p++) {
            total_tests++;
            const char *fen = "8/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
            const char *moves[] = { proms[p] };
            if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
        }
        total_tests++;
        const char *fen = "3r4/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
        const char *moves[] = { "e7d8q" };
        if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
    }

    // Test 6: King walks d1-e1-d1 and d8-e8-d8 across mirror boundary
    {
        total_tests++;
        const char *fen = "3k4/8/2n5/8/8/2N5/8/3K4 w - - 0 1";
        const char *moves[] = {
            "d1e1", "d8e8",
            "e1d1", "e8d8",
            "d1e2", "d8e7",
            "c3d5", "c6d4",
            "e2d3", "e7d6"
        };
        if (test_position_moves(pos, fen, moves, 10)) passed_tests++;
    }

    // Test 7: King captures across the d/e line for both colors
    {
        total_tests++;
        const char *fen = "3k4/4P3/8/8/8/8/4p3/3K4 w - - 0 1";
        const char *moves[] = {
            "d1e2",   // White king captures black pawn across d->e!
            "d8e7",   // Black king captures white pawn across d->e!
            "e2d2",
            "e7d7"
        };
        if (test_position_moves(pos, fen, moves, 4)) passed_tests++;
    }

    // Test 8: Multi-ply lazy catchup + deep stack moves after a king crossing (ancestor reuse)
    {
        total_tests++;
        printf("Testing Multi-ply lazy catchup with king crossing & ancestor reuse:\n");
        const char *fen = "r3k2r/ppp2ppp/2n5/8/8/2N5/PPP2PPP/R3K2R w KQkq - 0 1";
        ParseFEN((char *)fen, pos);
        nnue_refresh_accumulator(pos);

        // Make 4 plies without calling nnue_eval (includes White O-O-O crossing e1->c1)
        const char *lazy_moves1[] = { "e1c1", "e8g8", "c3d5", "c6e5" };
        for (int i = 0; i < 4; i++) {
            int move = ParseMove((char *)lazy_moves1[i], pos);
            makeMove(pos, move, &pos->stateTable[pos->hisPly + 1]);
        }
        // Evaluate at ply 4 (White crossed at ply 1, Black did not cross)
        int ok = verify_acc_matches_fresh(pos, "lazy_crossing_ply4");

        // Now play 4 more moves without crossing, evaluating only at ply 8 (reuses ply 4 ancestor!)
        const char *lazy_moves2[] = { "d5f4", "e5g6", "h2h3", "h7h6" };
        for (int i = 0; i < 4; i++) {
            int move = ParseMove((char *)lazy_moves2[i], pos);
            makeMove(pos, move, &pos->stateTable[pos->hisPly + 1]);
        }
        ok = ok && verify_acc_matches_fresh(pos, "ancestor_reuse_ply8");
        if (ok) {
            printf("  Multi-ply lazy crossing + ancestor reuse matched fresh exactly!\n\n");
            passed_tests++;
        }
    }

    // Test 9: 50 Random games (up to 80 plies each) with mixed lazy and eager evaluations
    {
        total_tests++;
        printf("Testing 50 random games (incremental + lazy vs fresh):\n");
        uint64_t rng = 0x123456789ABCDEF0ULL;
        int random_ok = 1;
        int total_plies = 0;
        for (int g = 0; g < 50 && random_ok; g++) {
            ParseFEN(START_FEN, pos);
            pos->hisPly = 0;
            initStacks(pos);
            pos->ply = 0;
            nnue_refresh_accumulator(pos);

            for (int ply = 0; ply < 60; ply++) {
                S_MOVELIST list[1];
                GenerateAllMoves(pos, list);
                int legals[MAXPOSMOVES];
                int nlegal = 0;
                for (int i = 0; i < list->count; i++) {
                    if (legal(pos, list->moves[i].move)) {
                        legals[nlegal++] = list->moves[i].move;
                    }
                }
                if (nlegal == 0) break;
                rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
                int pick = (int)((rng * 0x2545F4914F6CDD1DULL) % (uint64_t)nlegal);
                makeMove(pos, legals[pick], &pos->stateTable[pos->hisPly + 1]);
                total_plies++;

                // Evaluate every 1..3 plies to exercise both single-step and multi-step lazy updates
                if ((ply % 3) != 1) {
                    if (!verify_acc_matches_fresh(pos, "random_game")) {
                        random_ok = 0;
                        break;
                    }
                }
            }
        }
        if (random_ok) {
            printf("  50 random games (%d plies) passed with 0 mismatches!\n\n", total_plies);
            passed_tests++;
        }
    }

    free_search_thread(pos->search);

    printf("==================================================\n");
    printf("Results: %d / %d tests passed successfully!\n", passed_tests, total_tests);
    printf("==================================================\n");

    return (passed_tests == total_tests) ? 0 : 1;
}
