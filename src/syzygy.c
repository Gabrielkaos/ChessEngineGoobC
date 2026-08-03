#include "stdio.h"
#include "string.h"
#include "syzygy.h"
#include "movegen.h"
#include "some_maths.h"
#include "tbprobe.h"

//CMK syzygy.c
//
// Integration notes
// ------------------
// Fathom (fathom/tbprobe.c) works directly off plain bitboards, a
// castling-rights bitmask, an en-passant square and a side-to-move flag
// -- no separate "convert to Fathom's board format" step is needed here,
// because its conventions already match GOOB's exactly:
//   - squares are numbered a1=0 .. h8=63, rank-major (same as defs.h's
//     A1..H8 enum), so pos->enPas / FROMSQ / TOSQ pass straight through;
//   - its TB_CASTLING_K/Q/k/q bits (0x1/0x2/0x4/0x8) are numerically
//     identical to defs.h's WKCA/WQCA/BKCA/BQCA, so pos->castleRights
//     passes straight through too.
// The only translation actually required is piece-type <-> promotion
// encoding when matching Fathom's compact TbMove format back to GOOB's
// own MOVE() encoding (see tbPromoFromPiece/movesMatch below).
//
// Two probing entry points are used, mirroring how Stockfish/Ethereal/
// most other Syzygy-aware engines split the work:
//   - TBProbeRoot(): called ONCE per "go", before the depth loop starts.
//     Ranks every legal root move by DTZ (falling back to WDL if DTZ
//     files are missing) and records the subset that preserves the
//     position's game-theoretic result. The ordinary alpha-beta search
//     is then restricted to that subset (see search.c), so the engine
//     still finds the practically strongest move (fastest mate, safest
//     drawing fortress, etc.) rather than blindly obeying whichever
//     single move DTZ happens to suggest.
//   - TBProbeWDLSearch(): called from inside AlphaBeta on interior nodes
//     once the position is small enough, returning an exact ground-truth
//     score and letting the search skip generating/searching moves
//     entirely for that node.

int SyzygyEnabled     = FALSE;
int SyzygyProbeDepth  = 1;
int Syzygy50MoveRule  = TRUE;
int SyzygyProbeLimit  = 0;
int TBLargestMen      = 0;

//Kept comfortably below ISMATE so real checkmate scores (which run all
//the way up to AB_BOUND) always still sort ahead of a "mere" tablebase
//win/loss, and so that valueToTT/valueFromTT (which only rescale scores
//that already exceed ISMATE) leave TB scores untouched on their way
//into/out of the transposition table.
#define TB_WIN_VALUE (ISMATE - 1000)

void TBFree(void){
    if(SyzygyEnabled) tb_free();
    SyzygyEnabled    = FALSE;
    SyzygyProbeLimit = 0;
    TBLargestMen     = 0;
}

void TBInit(const char *path){

    TBFree();

    if(path == NULL || path[0] == '\0' || !strcmp(path,"<empty>")){
        printf("info string Syzygy tablebases disabled (no SyzygyPath set)\n");
        return;
    }

    if(!tb_init(path)){
        printf("info string Syzygy tablebases FAILED to load from: %s\n",path);
        return;
    }

    if(TB_LARGEST == 0){
        printf("info string No Syzygy tablebase files found in: %s\n",path);
        tb_free();
        return;
    }

    SyzygyEnabled    = TRUE;
    SyzygyProbeLimit = (int)TB_LARGEST;
    TBLargestMen     = (int)TB_LARGEST;
    printf("info string Syzygy tablebases loaded from %s (up to %u men)\n",path,TB_LARGEST);
}

//Builds the seven bitboards Fathom's probing calls expect out of GOOB's
//own per-piece bitboards.
static inline void tbBitboards(const S_BOARD *pos,U64 *white,U64 *black,U64 *kings,
                                U64 *queens,U64 *rooks,U64 *bishops,U64 *knights,U64 *pawns){
    *white   = pos->occupancy[WHITE];
    *black   = pos->occupancy[BLACK];
    *kings   = pos->bitboards[wK] | pos->bitboards[bK];
    *queens  = pos->bitboards[wQ] | pos->bitboards[bQ];
    *rooks   = pos->bitboards[wR] | pos->bitboards[bR];
    *bishops = pos->bitboards[wB] | pos->bitboards[bB];
    *knights = pos->bitboards[wN] | pos->bitboards[bN];
    *pawns   = pos->bitboards[wP] | pos->bitboards[bP];
}

//Cheap eligibility check shared by both probe entry points: Syzygy
//tables encode no castling-rights information at all, so any position
//that still has castling rights available is simply out of scope.
static inline int TBPositionOk(const S_BOARD *pos,int pieceLimit){
    if(!SyzygyEnabled)          return 0;
    if(pos->castleRights != 0)  return 0;
    if(COUNTBIT(pos->occupancy[BOTH]) > pieceLimit) return 0;
    return 1;
}

//Maps a promoted-piece value from GOOB's encoding (wN/wB/wR/wQ/bN/../..)
//to Fathom's TB_PROMOTES_* constant. Mirrors the exact same ISKni/ISRQ/
//ISBQ pattern io.c's PrMove() already uses to print promotion letters.
static inline int tbPromoFromPiece(int promoted){
    if(!promoted)                        return TB_PROMOTES_NONE;
    if(ISKni(promoted))                  return TB_PROMOTES_KNIGHT;
    if(ISRQ(promoted) && ISBQ(promoted)) return TB_PROMOTES_QUEEN;
    if(ISRQ(promoted))                   return TB_PROMOTES_ROOK;
    return TB_PROMOTES_BISHOP;
}

int TBProbeRoot(S_BOARD *pos){

    pos->tbHit           = 0;
    pos->tbRootMoveCount = 0;

    if(!TBPositionOk(pos,SyzygyProbeLimit)) return 0;

    U64 white,black,kings,queens,rooks,bishops,knights,pawns;
    tbBitboards(pos,&white,&black,&kings,&queens,&rooks,&bishops,&knights,&pawns);

    unsigned ep = (pos->enPas != NO_SQ) ? (unsigned)pos->enPas : 0;

    //Has the current position already occurred earlier in the game?
    //(only affects the DTZ/50-move-rule ranking, mirrors Stockfish's
    //"hasRepeated" root-probe argument.)
    int hasRepeated = FALSE;
    for(int i = pos->hisPly - pos->fiftyMove; i < pos->hisPly - 1; ++i){
        if(pos->posKey == pos->history[i].posKey){ hasRepeated = TRUE; break; }
    }

    struct TbRootMoves rm;
    memset(&rm,0,sizeof(rm));

    int ok = tb_probe_root_dtz(white,black,kings,queens,rooks,bishops,knights,pawns,
                                (unsigned)pos->fiftyMove,(unsigned)pos->castleRights,ep,
                                pos->side==WHITE,hasRepeated,Syzygy50MoveRule,&rm);

    if(!ok){
        //DTZ files missing/incomplete for this material -- WDL-only
        //ranking is still enough to guarantee correct (if not always
        //fastest) play.
        ok = tb_probe_root_wdl(white,black,kings,queens,rooks,bishops,knights,pawns,
                                (unsigned)pos->fiftyMove,(unsigned)pos->castleRights,ep,
                                pos->side==WHITE,Syzygy50MoveRule,&rm);
    }

    if(!ok || rm.size == 0) return 0;

    //Higher tbRank is always at least as good for the side to move;
    //every move that shares the best rank preserves the position's
    //game-theoretic result, so any of them is a safe candidate to hand
    //back to the ordinary search.
    int bestRank = rm.moves[0].tbRank;
    for(unsigned i = 1; i < rm.size; ++i)
        if(rm.moves[i].tbRank > bestRank) bestRank = rm.moves[i].tbRank;

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    for(unsigned i = 0; i < rm.size; ++i){
        if(rm.moves[i].tbRank != bestRank) continue;

        TbMove tm   = rm.moves[i].move;
        int tbFrom  = TB_MOVE_FROM(tm);
        int tbTo    = TB_MOVE_TO(tm);
        int tbPromo = TB_MOVE_PROMOTES(tm);

        for(int j = 0; j < list->count; ++j){
            int mv = list->moves[j].move;
            if(FROMSQ(mv) != tbFrom || TOSQ(mv) != tbTo) continue;
            if(tbPromoFromPiece(PROMOTED(mv)) != tbPromo) continue;

            if(pos->tbRootMoveCount < MAXPOSMOVES)
                pos->tbRootMoves[pos->tbRootMoveCount++] = mv;
            break;
        }
    }

    //Matching every ranked TbMove back onto our own move list should
    //never fail, but if it somehow comes up empty, don't filter at all
    //rather than accidentally forbid every legal move.
    if(pos->tbRootMoveCount == 0) return 0;

    pos->tbHit = 1;
    return 1;
}

int TBRootMoveAllowed(const S_BOARD *pos,int move){
    for(int i = 0; i < pos->tbRootMoveCount; ++i)
        if(pos->tbRootMoves[i] == move) return 1;
    return 0;
}

int TBProbeWDLSearch(S_BOARD *pos,int ply,int *scoreOut){

    //WDL tables carry no fifty-move information, so (like Fathom's own
    //tb_probe_wdl() wrapper enforces) they're only meaningful exactly
    //when the counter is at zero.
    if(pos->fiftyMove != 0) return 0;
    if(!TBPositionOk(pos,MIN(SyzygyProbeLimit,(int)TB_LARGEST))) return 0;

    U64 white,black,kings,queens,rooks,bishops,knights,pawns;
    tbBitboards(pos,&white,&black,&kings,&queens,&rooks,&bishops,&knights,&pawns);

    unsigned ep = (pos->enPas != NO_SQ) ? (unsigned)pos->enPas : 0;

    unsigned wdl = tb_probe_wdl(white,black,kings,queens,rooks,bishops,knights,pawns,
                                (unsigned)pos->fiftyMove,(unsigned)pos->castleRights,ep,
                                pos->side==WHITE);

    if(wdl == TB_RESULT_FAILED) return 0;

    int score;
    switch(wdl){
        case TB_WIN:          score =  (TB_WIN_VALUE - ply); break;
        case TB_LOSS:         score = -(TB_WIN_VALUE - ply); break;
        //A cursed win / blessed loss is a true win/loss that the 50-move
        //rule can rescue into a draw -- score it as a draw unless the
        //user has explicitly told us to ignore the 50-move rule.
        case TB_CURSED_WIN:   score = Syzygy50MoveRule ? 0 :  (TB_WIN_VALUE - ply); break;
        case TB_BLESSED_LOSS: score = Syzygy50MoveRule ? 0 : -(TB_WIN_VALUE - ply); break;
        default:              score = 0; break; //TB_DRAW
    }

    *scoreOut = score;
    return 1;
}
