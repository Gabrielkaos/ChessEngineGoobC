#include "perft.h"
#include "stdio.h"
#include "string.h"
#include "inttypes.h"
#include "bitboards.h"
#include "movegen.h"
#include "misc.h"
#include "makemove.h"
#include "io.h"
#include "attacks.h"
#include "validate.h"

U64 countNps(U64 nodes, int time) {
	if (time == 0) return 0;

	if (time > 20000) return nodes / (time / 1000);
	else return (nodes * 1000) / time;
}

U64 leafNodes;
void Perft(int depth,S_BOARD *pos){

    ASSERT(checkBoard(pos));

    if(depth==0){
        leafNodes++;
        return;
    }

    S_MOVELIST list[1];

    GenerateAllMoves(pos,list);

    StateInfo st;
    int moveNum;
    for(moveNum=0;moveNum<list->count;++moveNum){
        int move = list->moves[moveNum].move;
        if(!legal(pos, move)){
            continue;
        }

        makeMove(pos, move, &st);
        Perft(depth-1,pos);
        takeMove(pos);
    }

    return;
}
void PerftTest(int depth,S_BOARD *pos){

    ASSERT(checkBoard(pos));
    if(depth==0){
        printf("Depth 0\nNodes 1\n");
        return;
    }

    printf("\nStarting Perft Test to depth: %d\n",depth);

    leafNodes=0;
    int start=getTimeMs();
    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);
    U64 cumnodes,oldnodes;

    int move;
    int moveNum;
    int realNumMoves = 0;
    StateInfo st;
    for(moveNum=0;moveNum<list->count;++moveNum){
        move=list->moves[moveNum].move;
        if(!legal(pos, move)){
            continue;
        }

        cumnodes=leafNodes;
        makeMove(pos, move, &st);
        Perft(depth-1,pos);

        takeMove(pos);
        oldnodes=leafNodes-cumnodes;

        printf(" %3d : %s : %"PRIu64"\n",++realNumMoves,PrMove(move),oldnodes);
    }

    int end=getTimeMs();
    printf("Nodes Searched: %"PRIu64", Time: %dms\n",leafNodes,end-start);
    printf("Perft Speed: %"PRIu64" nps\n",countNps(leafNodes,end-start));

    return;
}

void BenchTest(int depth,S_BOARD *pos){

    ASSERT(checkBoard(pos));

    if(depth==0){
        printf("Nodes: 1\n");
        return;
    }

    leafNodes=0;
    Perft(depth,pos);
    printf("Nodes: %"PRIu64"\n",leafNodes);

    return;
}

//////////////////////////////////////////////////////////////////////
// Perft regression suite: known positions/depths/expected leaf counts,
// covering the standard "Perft Results" set plus Martin Sedlak's
// TalkChess edge-case positions (illegal ep, ep-capture-checks,
// castling through/out-of check, discovered check, promotion checks,
// self-stalemate). These specifically target move-generation and
// legality bugs, so this runs through Perft()/makeMove()/takeMove()/
// GenerateAllMoves() -- the actual search path, hash/psqt/NNUE updates
// included -- not the stripped-down Bench()/makeMoves() fast path.
//////////////////////////////////////////////////////////////////////
typedef struct {
    const char *fen;
    int depth;
    U64 expectedNodes;
    const char *label;
} S_PERFTCASE;

static const S_PERFTCASE perftSuite[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",              6, 119060324ULL, "Startpos"},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",  5, 193690690ULL, "Kiwipete"},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",                            7, 178633661ULL, "Position 3"},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",     6, 706045033ULL, "Position 4"},
    {"1k6/1b6/8/8/7R/8/8/4K2R b K - 0 1",                                    5, 1063513ULL,   "Position 5"},

    {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1",                                    6, 1134888ULL,   "Illegal ep move #1"},
    {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1",                                   6, 1015133ULL,   "Illegal ep move #2"},
    {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",                                  6, 1440467ULL,   "EP capture checks opponent"},
    {"5k2/8/8/8/8/8/8/4K2R w K - 0 1",                                       6, 661072ULL,    "Short castling gives check"},
    {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1",                                       6, 803711ULL,    "Long castling gives check"},
    {"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1",                            4, 1274206ULL,   "Castle rights"},
    {"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1",                             4, 1720476ULL,   "Castling prevented"},
    {"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",                                    6, 3821001ULL,   "Promote out of check"},
    {"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1",                                  5, 1004658ULL,   "Discovered check"},
    {"4k3/1P6/8/8/8/8/K7/8 w - - 0 1",                                       6, 217342ULL,    "Promote to give check"},
    {"8/P1k5/K7/8/8/8/8/8 w - - 0 1",                                        6, 92683ULL,     "Under promote to give check"},
    {"K1k5/8/P7/8/8/8/8/8 w - - 0 1",                                        6, 2217ULL,      "Self stalemate"},
    {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1",                                       7, 567584ULL,    "Stalemate & checkmate"},
    {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1",                                    4, 23527ULL,     "Double check (black)"},

    // ---- color-reversed counterparts to the edge cases above ----
    {"8/5bk1/8/2Pp4/8/1K6/8/8 w - d6 0 1",                                    6, 824064ULL,    "Illegal ep avoid (white)"},
    {"8/8/1k6/8/2pP4/8/5BK1/8 b - d3 0 1",                                   6, 824064ULL,    "Illegal ep avoid (black)"},
    {"8/5k2/8/2Pp4/2B5/1K6/8/8 w - d6 0 1",                                  6, 1440467ULL,   "EP capture checks opp (white)"},
    {"4k2r/8/8/8/8/8/8/5K2 b k - 0 1",                                       6, 661072ULL,    "Short castle check (black)"},
    {"r3k3/8/8/8/8/8/8/3K4 b q - 0 1",                                       6, 803711ULL,    "Long castle check (black)"},
    {"r3k2r/7b/8/8/8/8/1B4BQ/R3K2R b KQkq - 0 1",                            4, 1274206ULL,   "Castle rights (black)"},
    {"r3k2r/8/5Q2/8/8/3q4/8/R3K2R w KQkq - 0 1",                             4, 1720476ULL,   "Castling prevented (white)"},
    {"3K4/8/8/8/8/8/4p3/2k2R2 b - - 0 1",                                    6, 3821001ULL,   "Promote out of check (black)"},
    {"5K2/8/1Q6/2N5/8/1p2k3/8/8 w - - 0 1",                                  5, 1004658ULL,   "Discovered check (white)"},
    {"8/k7/8/8/8/8/1p6/4K3 b - - 0 1",                                       6, 217342ULL,    "Promote to give check (black)"},
    {"8/8/8/8/8/k7/p1K5/8 b - - 0 1",                                        6, 92683ULL,     "Underpromote to check (black)"},
    {"8/8/8/8/8/p7/8/k1K5 b - - 0 1",                                        6, 2217ULL,      "Self stalemate (black)"},
    {"8/8/8/8/1k6/8/K1p5/8 b - - 0 1",                                       7, 567584ULL,    "Stalemate & checkmate (black)"},
    {"8/5k2/8/5N2/5Q2/2K5/8/8 w - - 0 1",                                    4, 23527ULL,     "Double check (white)"},
};

#define PERFTSUITE_SIZE ((int)(sizeof(perftSuite)/sizeof(perftSuite[0])))

void PerftSuiteTest(S_BOARD *pos){

    int passed=0, failed=0;
    U64 totalNodes=0;
    int totalTime=0;

    printf("\nRunning perft suite: %d positions\n\n", PERFTSUITE_SIZE);

    for(int i=0;i<PERFTSUITE_SIZE;i++){

        char fenBuf[128];
        strncpy(fenBuf, perftSuite[i].fen, sizeof(fenBuf)-1);
        fenBuf[sizeof(fenBuf)-1]='\0';

        ParseFEN(fenBuf, pos);

        leafNodes=0;
        int start=getTimeMs();
        Perft(perftSuite[i].depth, pos);
        int end=getTimeMs();
        int elapsed=end-start;

        totalNodes+=leafNodes;
        totalTime+=elapsed;

        int ok = (leafNodes == perftSuite[i].expectedNodes);
        if(ok) passed++; else failed++;

        printf("[%2d/%2d] %-30s depth %d : got %-12"PRIu64" want %-12"PRIu64" %6dms %10"PRIu64" nps  %s\n",
               i+1, PERFTSUITE_SIZE, perftSuite[i].label, perftSuite[i].depth,
               leafNodes, perftSuite[i].expectedNodes, elapsed,
               countNps(leafNodes, elapsed),
               ok ? "PASS" : "FAIL <---");

        if(!ok){
            printf("         FEN: %s\n", perftSuite[i].fen);
        }
    }

    printf("\n%d / %d positions passed", passed, PERFTSUITE_SIZE);
    if(failed) printf("  (%d FAILED)", failed);
    printf("\nTotal nodes: %"PRIu64", total time: %dms, avg %"PRIu64" nps\n\n",
           totalNodes, totalTime, countNps(totalNodes, totalTime));
}