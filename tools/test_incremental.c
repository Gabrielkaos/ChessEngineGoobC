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
#include "../src/nnue_loader.h"

static int test_position_moves(S_BOARD *pos, const char *fen, const char *move_strs[], int num_moves) {
    ParseFEN(fen, pos);
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

        // Make move
        makeMove(pos, move, &pos->stateTable[pos->hisPly + 1]);

        // Evaluate incrementally
        int inc_eval = nnue_eval(pos);

        // Copy incremental accumulator state
        int ply = pos->ply;
        int16_t inc_w[NNUE_ACC_SIZE], inc_b[NNUE_ACC_SIZE];
        memcpy(inc_w, pos->search->nnue_accumulators[ply].accumulation[WHITE], sizeof(inc_w));
        memcpy(inc_b, pos->search->nnue_accumulators[ply].accumulation[BLACK], sizeof(inc_b));

        // Recompute fresh accumulator
        nnue_refresh_accumulator(pos);
        int fresh_eval = nnue_eval(pos);

        // Check accumulator contents
        int diff_w = memcmp(inc_w, pos->search->nnue_accumulators[ply].accumulation[WHITE], sizeof(inc_w));
        int diff_b = memcmp(inc_b, pos->search->nnue_accumulators[ply].accumulation[BLACK], sizeof(inc_b));

        if (diff_w != 0 || diff_b != 0 || inc_eval != fresh_eval) {
            printf("FAIL on move %d (%s): inc_eval=%d fresh_eval=%d diff_w=%d diff_b=%d\n",
                   i, mstr, inc_eval, fresh_eval, diff_w, diff_b);
            return 0;
        }

        printf("  Move %2d: %-6s -> Eval: %5d (inc==fresh OK)\n", i + 1, mstr, inc_eval);
    }

    // Now unmake all moves and check that we return to start state
    for (int i = num_moves - 1; i >= 0; i--) {
        takeMove(pos);
        int cur_eval = nnue_eval(pos);
        (void)cur_eval;
    }

    int back_eval = nnue_eval(pos);
    if (back_eval != initial_eval) {
        printf("FAIL after unmaking all moves: back_eval=%d initial_eval=%d\n", back_eval, initial_eval);
        return 0;
    }
    printf("  All %d moves unmade cleanly; returned to initial eval: %d\n\n", num_moves, back_eval);
    return 1;
}

int main(void) {
    AllInit();

    S_BOARD pos[1];
    pos->search = alloc_search_thread();
    pos->useNNUE = TRUE;

    if (!nnue_init("src/weights/quantised.bin")) {
        printf("FAIL: Could not load weights\n");
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
            "e1g1", "e8g8",   // Castling kingside for both!
            "c2c3", "d7d6",
            "b2b4", "c5b6",
            "a2a4", "a7a6",
            "b1d2", "c6e7"
        };
        if (test_position_moves(pos, START_FEN, moves, 16)) passed_tests++;
    }

    // Test 2: Queenside Castling Game
    {
        total_tests++;
        const char *moves[] = {
            "d2d4", "d7d5",
            "c2c4", "e7e6",
            "b1c3", "g8f6",
            "c1g5", "c7c6",
            "e2e3", "b8d7",
            "d1c2", "f8e7",
            "e1c1", "e8g8"    // White castles queenside, Black castles kingside!
        };
        if (test_position_moves(pos, START_FEN, moves, 14)) passed_tests++;
    }

    // Test 3: En Passant capture (White & Black)
    {
        total_tests++;
        // Position where White can play e5, Black plays f5, White plays exf6 e.p.
        const char *fen = "rnbqkbnr/pppp2pp/4p3/4Pp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3";
        const char *moves[] = {
            "e5f6",           // En passant capture!
            "g8f6"
        };
        if (test_position_moves(pos, fen, moves, 2)) passed_tests++;
    }

    // Test 4: En Passant capture by Black
    {
        total_tests++;
        const char *fen = "rnbqkbnr/pppp1ppp/8/8/3Pp3/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 3";
        const char *moves[] = {
            "e4d3",           // En passant capture by Black!
            "c2d3"
        };
        if (test_position_moves(pos, fen, moves, 2)) passed_tests++;
    }

    // Test 5: Promotion (all 4 promotion piece types: Q, R, B, N)
    {
        total_tests++;
        const char *fen = "8/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
        const char *moves[] = { "e7e8q" };
        if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
    }
    {
        total_tests++;
        const char *fen = "8/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
        const char *moves[] = { "e7e8r" };
        if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
    }
    {
        total_tests++;
        const char *fen = "8/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
        const char *moves[] = { "e7e8b" };
        if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
    }
    {
        total_tests++;
        const char *fen = "8/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
        const char *moves[] = { "e7e8n" };
        if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
    }

    // Test 6: Promotion with Capture
    {
        total_tests++;
        const char *fen = "3r4/4P3/8/8/8/8/4k3/4K3 w - - 0 1";
        const char *moves[] = { "e7d8q" };
        if (test_position_moves(pos, fen, moves, 1)) passed_tests++;
    }

    // Test 7: Multi-ply lazy evaluation test (skip eval for 5 plies, then eval)
    {
        total_tests++;
        printf("Testing Multi-ply lazy catchup (5 plies without eval):\n");
        ParseFEN(START_FEN, pos);
        nnue_refresh_accumulator(pos);

        const char *moves[] = { "e2e4", "e7e5", "g1f3", "b8c6", "f1b5" };
        for (int i = 0; i < 5; i++) {
            int move = ParseMove((char *)moves[i], pos);
            makeMove(pos, move, &pos->stateTable[pos->hisPly + 1]);
            // Notice: do NOT call nnue_eval here!
        }
        // Now call nnue_eval at ply 5:
        int lazy_eval = nnue_eval(pos);

        int16_t lazy_w[NNUE_ACC_SIZE], lazy_b[NNUE_ACC_SIZE];
        memcpy(lazy_w, pos->search->nnue_accumulators[pos->ply].accumulation[WHITE], sizeof(lazy_w));
        memcpy(lazy_b, pos->search->nnue_accumulators[pos->ply].accumulation[BLACK], sizeof(lazy_b));

        // Recompute fresh
        nnue_refresh_accumulator(pos);
        int fresh_eval = nnue_eval(pos);

        int dw = memcmp(lazy_w, pos->search->nnue_accumulators[pos->ply].accumulation[WHITE], sizeof(lazy_w));
        int db = memcmp(lazy_b, pos->search->nnue_accumulators[pos->ply].accumulation[BLACK], sizeof(lazy_b));

        if (dw == 0 && db == 0 && lazy_eval == fresh_eval) {
            printf("  Multi-ply lazy catchup matched fresh accumulator exactly! Eval: %d\n\n", lazy_eval);
            passed_tests++;
        } else {
            printf("  FAIL: Multi-ply lazy catchup failed! lazy=%d fresh=%d dw=%d db=%d\n\n",
                   lazy_eval, fresh_eval, dw, db);
        }
    }

    printf("==================================================\n");
    printf("Results: %d / %d tests passed successfully!\n", passed_tests, total_tests);
    printf("==================================================\n");

    return (passed_tests == total_tests) ? 0 : 1;
}
