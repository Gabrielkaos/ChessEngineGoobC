#include "stdio.h"
#include "stdlib.h"
#include "defs.h"
#include "string.h"
#include "see.h"
#include "evaluate.h"
#include "some_maths.h"
#include "bitboards.h"
#include "inttypes.h"
#include "uci.h"
#include "tt_eval.h"

//extern PKNetwork PKNN;

int main(int argc, char *argv[])
{
    AllInit();

    S_BOARD pos[1];
    S_SEARCHINFO info[1];
    info->quit=FALSE;

    //init Tables
    pos->pvTable->pTable=NULL;
    InitPvTable(pos->pvTable,defaultHash,0);

	pos->eTable->evalTable=NULL;
	InitEvalTable(pos->eTable,evalHashMB,0);

	pos->pawnKingTable->paTable=NULL;
	InitPawnKingTable(pos->pawnKingTable,pawnHashMB,0);


    //init some stacks and minor tables
	initStacks(pos);
	resetContinuationTable(pos);

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

#ifdef DEBUG
    printf("\nWARNING! DEBUG DEFINED MIGHT SLOW DOWN ENGINE\n");
#endif // DEBUG

    printf("\nUCI engine by Gabriel M.\n");
    printf("type 'uci' then 'help' for commands\n\n");


	char line[256];
	while (TRUE) {
		memset(&line[0], 0, sizeof(line));

		fflush(stdout);
		if (!fgets(line, 256, stdin))
			continue;
		if (line[0] == '\n')
			continue;
		if (!strncmp(line, "uci",3)) {
			UCILoop(pos, info);
			if(info->quit == TRUE) break;
			continue;
		}else if (!strncmp(line, "bb",2)){
            printf("%d\n",MOVE(0,0,0,0,MVFLAGCAP));
            continue;
        }else if(!strncmp(line, "quit",4)){
			break;
		}
	}

	free(pos->pvTable->pTable);
	free(pos->eTable->evalTable);
	free(pos->pawnKingTable->paTable);
	//CleanPolyBook();

	/*newly added things

	->removed piecemaj and piecemin
	->removed fhf and fh to see move ordering
	->force king function
	->removed the hits, newwrite,cuts,overwrite
	->king open or semi in eval
	->doubled pawns
	->razoring ? not sure if correctly implemented
	->Eval pruning ? not sure if correctly implemented
	->put the null move code insearch in top of razoring
	->futility pruning
	->mate search pruning
	->in attacks.c replaced king and knight attacks with bitboards
	->removed the foundPv variable
	->LMR
	->Delta Pruning
	->pawn passed extension
	->King Heat Map
	->no pawns in front of king punishment


    /////////////////////////////////////////////
    /                                           /
    //            BITBOARDS  ERA                /
    /                                           /
    /////////////////////////////////////////////
	->added pre calculated attacks
	->added occupancy bitboards in game state
	->added all piece bitboards
	->removed pawns bitboards
	->square attacked based on bitboards
	->square attacked function based on bitboards
	->movegen based on bitboard added
	->totally replaced old Sqattacked function
	->replaced the SETBIT code
	->totally replaced the generate all moves and capture functions
	->evaluation based on piece 64 arrays
	->removed piecelist from existence
	->aspiration window in iterative deepening
	->changed the condition in searchkiller heuristics
	->change long nodes to U64 nodes, in search and perft
	->changed piece values
	->added own movegen function in perft.c
	->added own makemove and takemove function in perft.c
	->if rook and bishop piece is trapped in evaluation
	->moved the entire code in evaluate.c into search.c
	->removed ASSERT in attacks,movegen,makemove,perft,search
	->knight outposts in evaluation
	->moved evaluate function again to evaluate.c from search.c
	->created mirror command in consolemode
	->created test function to debug evaluate function
	->deleted the checkboard function
	->in alphabeta if legal==0 returns INFINITE instead of MATE
	->removed mate pruning
	->added minPiece and majPiece again in board structure and data.c
	->added minpiece and majpiece superiority evaluation
	->removed assert from every file in existence
	->added in evaluate if side has more pawns
	->added in evaluate, bishop outposts
	->replaced the bitboard based doubled pawn checker with an array based one
	->added searchtest in consolemode.c
	->fixed evaluation of isolated passed Pawn
	->fixed all evaluation of outposts
	->deleted ENDGAME variable, replaced with a isItEndGame function
	->added semi outposts, might slow down search
	->changed the doubled pawn checker in eval
	->totally deleted the doubled pawn check in eval
	->added queen in open files in evaluation
	->added doubled pawn again
	->added knight piece trapped
	->added queen trapped
	->if both are queenless when youre better, this is rewarded in eval
	->changed the outpost eval code
	->removed the pawn promotion extenstion
	->called the endgame function twice only in eval function instead of 6 times
	->removed book in uci
	->removed the printf in polybook.c
	->added knight move map
	->added bishop move map
	->made the semi outpost on knights and bishop in eval, only one if statement
	->in bishop pair made >=2 into >1
	->made no pawns in front of king code in eval into simple if statements
	->changed MATE to INFINITE in evaluation pruning
	->futility checker MATE changed into INFINITE
	->deleted the variable MATE in defs.h
	->added break in forloop if score == INF-1 in search.c
	->changed delta pruning in quiescence and defined ENDGAME in search.c
	->added bench in consolemode
	->changed the getlsbindex in defs.h



    /////////////////////////////////////////////
    /                                           /
    //            AFTER GITHUB ERA              /
    /                                           /
    /////////////////////////////////////////////
    ->added movecansimplify function in search.c
    ->added in delta pruning pos->side^1 instead of just pos->side
    ->added pawn_cntrl in board struct,board.c and makemove.c
    ->added badCapture function in search.c
    ->added badCapture to quiescence
    ->replaced with pawn control in semi outpost eval
    ->changed doubled pawn checker
    ->added backwards pawn
    ->made functions for pawnmap in makemove.c
    ->refactor the code for pawnmap in board.c
    ->put count nps in bench and perft test
    ->added nps in uci in search.c
    ->added book and clear hash in uci
    ->removed if gamemode==Xboard in Search function
    ->revived the prints in polybook initbook
    ->added variable end in uci search.c,perft and bench in perft.c
    ->added char namefile in initpolybook argument
    ->added Bookfile option in uci
    ->changed bishop psq tables
    ->commented tosqpce in badcapture
    ->changed the value of bishop pair 30 to 20
    ->added pawns diff in bishop pair value
    ->made bishop more valuable in data.c
    ->made backwards pawn in endgame more punishable
    ->added backwardpawnE and doubledpawnE variable
    ->added doubled pawn in endgame more punishable
    ->REMOVED-used supposed "stockfish formula" for R, not sure if correctly implemented
    ->REMOVED-changed sf formula to list->count
    ->REMOVED-changed Nullmove code insearch using PLY variable now
    ->added R=1 in LMR
    ->added if movestried>6 R+=1
    ->made search.h and evaluate.h

    // old GOOBbeta//

    ->fixed outpost code, anded with Passed Bitboards
    ->added seldepth to S_BOARD
    ->added seldepth in alphabeta and search
    ->dont know if i used seldepth correctly in uci
    ->added rootnode variable in alpha beta
    ->added currmove and currmovenumber not sure if correct
    ->added nodelimit,nodeset in searchinfo in defs.h
    ->added nodes in parseGo in uci.c
    ->added nodeset in checkUp in search.c
    ->separated info score from info strings
    ->added info score mate in uci
    ->added mateIn in searchinfo
    ->added in uci mateIn parseGo
    ->added mateIN brake in search for uci
    ->removed mateBraking in search
    ->added mate distance pruning
    ->maxdepth to 128
    ->maxgamesmoves from 2048 to 3000
    ->maxposmoves from 256 to 300
    ->recompiled goobSimpleEval dont know if I messed up something

    //GOOBbeta

    ->added mobility.c
    ->added mobility.h
    ->added mobility in eval
    ->commented pieceTrapped condition in eval
    ->excluded in mobility if controlled by enemy pawn
    ->added tropism in board struct in defs.h
    ->EvaluatePosition now takes a not Const pointer to board
    ->removed king heatmap in pawn
    ->removed king heatmap in all pieces replaced with king tropism
    ->removed const in all mobility functions
    ->added attCnt and attWeight in board struct in defs.h
    ->added safetyTables in eval
    ->added pawnshiled var in eval.h
    ->moved king eval outside for loop
    ->removed if pce==EMPTY continue in for loop in eval
    ->added clamping of mibility values in mobility functions
    ->added knigShield in board struct
    ->changed king shield code in evalking funcs
    ->added condition to for loop in eval
    ->excluded pawn and king mobility
    ->removed ranksBoard in king eval
    ->added bonus for kings staying in their safe ranks
    ->clamp queen to 14
        //old ChessEngine.exe
    ->distance testing in consolemode
    ->replaced the getTropism code with mediocre's
    ->changed tropism code with mediocre's
    ->deleted king and pawn in mobility.c
    ->fixed queen_possible_moves by adding pawn control
    ->commented out the color dependent evaluations
    ->added more code in bishop pair
    ->added ranksboard again in king pawn shield eval
    ->removed pawn diff in bishop pair
    ->added pawn diff in eval
    ->added pieceList again
    ->eval based on pieceList now
    ->made somethings static
    ->added penalty for kings in file d or e
    ->backwards and doubled pawn independent from phase
    ->added more code in isItBackwardsPawn function
    ->only defined as backwards if white row<4 and black>5
    ->returned the score at the bottom of the eval function
    ->avoiding draws condition in eval
    ->made mobility functions more efficient
    ->removed backwards Pawn eval in pawns eval
    ->added two knights avoid draw in eval again but commented
    ->added see things but not used
    ->commented f in print fen function
    ->added depth in currmove
    ->deleted extra print in uci
    ->added bitboards.c and bitboards.h
    ->removed distance testing in consolemode
    ->removed || if info->post==TRUE in if statement for printing pv in search.c
    ->changed things in printing in consolemode
    ->placed pv printing in their respective gameMode
    ->removed clearhash ucinewgame in uci.c

    //GOOBbeta

    ->added perft2.c and perft.h
    ->added searchtest3 and another fen in defs.h
    ->deleted backwards pawn function in eval
    ->
    ->removed +=doubled in doubled pawn checker eval
    ->doubled pawn value from -7 to -6
    ->changed the entire doubledPawn checker
    ->removed bishop pair extra codes
    ->avoid draw 2 knights added in eval
    ->added pawnBlocked in eval
    ->added bonuses for queen rook in seventh
    ->added my lameass code for pawn storm
    ->pawnshield from 3 to 6
    ->pawnshield2 from 2 to 4
    ->commented out the lame things in king eval functions
    ->removed times 2 in pawn_diff eval
    ->added tempo

    //GOOBbeta
    //can find Qxf3 at depth 16
    /////////////////////////////////////////////
    /                                           /
    //            TAPERED EVAL ERA              /
    /                                           /
    /////////////////////////////////////////////

    ->switch things in pawn storm functions
    ->bishop pair from 20 to 30
    ->removed material in makemove.c and board.c
    ->added pieceValE in data.c
    ->made pos->material[color][phase]
    ->added enum in defs.h phases
    ->added getGamePhase function
    ->removed avoid draw things in eval
    ->added king eval in pos
    ->removed pawn storm
    ->added tapered eval by mediocre
    ->added psq.c and psq.h
    ->removed king material
    ->changed some psq
    ->added draw function by mediocre in adjust score

    //can also find Qxf3 at depth 16

    ->added adjust score endgame and middle separate functions
    ->added opposite colored bishop on adjustScoreEndgame function

    //Qxf3 at depth 17

    ->removed pawn blocked in pawn eval
    ->added white_trapped and black_trapped functions

    //Qxf3 at depth 16

    ->removed kingshield in endgame score
    ->removed pawndiff in eval
    ->removed safetyTables for kings for endgame score
    ->added doubledPawnE in eval.h and IsolatedPawnE
    ->separated isolated and doubledpawn phase scoring in eval

    //can find Qxf3 at depth 16

    ->changed knights/bishops eval by adding pawn protected
    ->removed +2 in outpost knights/bishops
    ->changed pawn value endgame to 100 from 120
    ->deleted king eval on open files on endgame

    //can find Qxf3 at depth 16

    ->added pawnPos in defs.h S_BOARD
    ->moved king eval at the bottom in EvalPosition
    ->added king def by mediocre
    ->commented openking and semi open king in king eval function and pawnshield
    ->changed 36 to openKing and 10 to semiOpenKing in king def funcs
    ->added openNearKing in king def funcs
    ->added in front of king pawn eval if just castled
    ->pawn storm weights in king def funcs changed to 3 from 5
    ->changed materialDraw code to simpler
    ->added backwards pawn eval
    ->added fianchetto in king def
    ->added drawPattern in phase adjuster

    //ChessEngine - could draw goobBeta with black and white

    ->changed isolatedpawnE value from 10 to 12
    ->changed backwardspawnE value from 10 to 11
    ->changed isolatedpawn value from 7 to 8
    ->changed backwardspawn value from 5 to 7
    ->added pawns_cnt based value for bishop
    ->added outpost table for knights in psq.c
    ->removed movemap for knights
    ->replaced with knight outpost table on pawn protected eval
    ->replaced outpost ranks in (knight/bishops) with unContested

    ->used SEE again
    //CE_old

    ->added passers in board defs
    ->added best promdist in board defs
    ->added pawnEval in board defs
    ->now evaluating pawns using a function EvalPawn
    ->changed passers in board defs
    ->added passersCnt in board defs
    ->evaluating passers in different functions
    ->added passers eval

    ->added functions in mobility.c
    ->added mobility in defs board
    ->added attack_squares on board defs
    ->added gamephase in board defs
    ->pos->gamePhase in resetBoard and updateMaterials
    ->pos->gamePhase in makemove.c added
    ->removed gamePhase variable declaration on EvalPosition

    //CE_PASSERS can defeat GoobBeta

    ->added pos->material[OPENING] in search.c moveCanSimplify and Delta Pruning
    ->removed +pieceVal[wK] in search.h ENDGAME variable
    ->replaced condition on search.c move_CanSimplify function with gamePhase
    ->replaced condition on search.c Delta Pruning function with gamePhase

    //+ 1000 nodes searched
    //ChessEngine

    ->changed mobility code in mobility.c
    ->added pinned detector funcs in mobility.c - not used still experimental
    ->set zero in eval of attack rays
    ->now updating attack squares in mobility.c
    ->using attack squares for mobility_safe
    ->doubled passedPawn values

    //CE_Old2
    //can defeat cpw with white

    ->added sq64To120 variable in data.c
    ->added OFFBOARD enum in defs.h
    ->added pos->pieces120 in board struct
    ->added pos->pieces120 in resetboard and updateMaterial
    ->added pos->pieces120 in makemove
    ->coded some mobility funcs in mobility.c
    ->experiments in see.c
    ->coded SEE_NEW in see.c
    ->changed passed pawn checker - SAME EVAL NO CHANGE EXCEPT FASTER
    ->not doing mate distance pruning in rootNode - NO CHANGE
    ->moved the init 1 above mate pruning originally from below incheck extension - NO CHANGE
    ->removed majPiece and minorPiece in everything board.c | makemove.c | defs.h - NO CHANGE
    ->changed Isolated Pawn checker - NO CHANGE IN EVAL

    ->added penalty for doubled passed pawn
    ->added reward for rook behind passed
    ->changed queen value from 1000 to 930
    ->removed badCapture and moveCanSimplify conditions in quiescence

    //ChessEngine

    ->removed all pos->pieces120 defs.h,board.c,makemove.c
    ->fix things in EvalPosition removed comments,moved some eval parts
    ->added IIR after nullMove
    ->added !pvNode in probeHashEntry

    //CE_IIR

    ->added tt_eval.c and tt_eval.h
    ->probing and storing Eval HashTables now in EvalPosition in eval.c
    ->added to uci.c if Clear Hash also clears Eval Table
    ->added EvalHash setoption to uci

    //somehow with different Evaluations now
    //Maybe collisions?, I dont know whats the problem here
    //Maybe something that doesnt concern me I dont know


    ////////////////////////////////////////////////////////
    //FIX ME WHEN YOU SEE ME AGAIN PLEASE
    ////////////////////////////////////////////////////////

    ->added StoreTTEval to materialDraw condition
    ->made GetPawnPosKey in hashkeys.c
    ->added PawnPosKey in board defs and board.c
    ->added PawnPosKey in makemove.c
    ->added pawnTables in board defs
    ->added PawnHashTables
    ->now using in Eval from PawnHash
    ->added pawnHash to uci

    //EvalHash_CE

    ->added ponder in info struct
    ->added setoption ponder in uci.c
    ->removed mate break in searchPosition
    ->modified misc.c ReadInput removed info->stopped = TRUE
    ->added if input == stop = info->stop =TRUE
    ->added if input == ponderhit info->ponderhit=FALSE
    ->added !ponder in checkUp function
    ->removed nodeset in checkUp
    ->added another PonderThing in defs
    ->added own ponder variable in function parseGo in uci.c
    ->added ponder in printf bestmove in Search position
    ->added ponderMove in SearchPosition in search.c

    //CE_PONDER
    //can find Qxf3 at depth 23

    ->added info->nullcut in search info struct
    ->added info->nullcut in ClearforSearch in search.c
    ->commented useless see.c codes
    ->added if info->GAMEMODE==CONSOLEMODE info->STOPPED=TRUE in misc.c
    ->used olithink null move reduction R formula
    ->added mate brake in iterative deepening

    //CE_OLI

    ->removed pawn_cntrl in makemove.c
    ->removed pawncntrl in update list material and reset in board.c
    ->now setting pawn cntrl to zero in eval.c
    ->now adding pawn cntrl in pawns loop in EvalPosition
    ->added t_piecenum=0 in clearPiece
    ->set initHashTable pawn and eval with a value and in uci set default to that value
    ->removed poskey in makemove in bench test
    ->moved static eval to the top
    ->added probcut pruning
    ->added clear tables now in ucinewgame

    //CE_PROBCUT

    ->made checkboard function
    ->added eval array indexed by ply in board defs
    ->clearing eval_stack in clearForSearch
    ->now saving staticEval in alphaBeta to eval_stack
    ->added a variable improving
    ->now using improving to add 1 to R in LMR

    //CE_IMPROVING

    ->R+= !improving + !pvNode + inCheck
    ->R+= inCheck && king evades
    ->removed !pvNode and !incheck in LMR conditions
    ->in improving only if pos->ply >=2
    ->now using Ethereal LMRTABLE formula

    //CE_FORMULA

    ->just did some cleaning in the header files
    ->added some_maths.h
    ->replaced all MAX and min and stuff using some_maths.h
    ->just some cleaning of static variables in c files, transferred some of them to correct header files
    ->added LMRTable and initializing in init.c
    ->using LMRTable in LMR in search.c

    ->added quietMove and quietsseen variables in search.c
    ->declrared quiets and quietsseen in moveloop
    ->added late move pruning before makeMove

    //CE_QUIET

    ->improved drawPatternStronger but still the same
    ->mixed materialDraw and materialDrawSide
    ->added recog.h and recog.c
    ->made pawnhash 32 from 64 and evalhash 64 from 128
    ->made mobility at top of evalPassers
    ->added neighborBB variable in evaluate.c
    ->added initNeighborBB in init.c

    ->added not attacked by bigPiece reward in evalPassers
    ->added candidatePasser eval in evalPawn
    ->added faker eval in evalPawn

    //CE_PAWNS

    ->copied ethereal 12.75 pawn evaluation and scoring
    ->added initEvalMasks in eval.c
    ->added variable pawnConnectedMasks
    ->added pawnConnected in PawnEval
    ->fixed neighborBB init function
    ->added two new variables
    ->fixed forwardFileMask
    ->fixed several in doubled pawn
    ->fixed passed pawn

    //CE_ULTRAPAWNS

    ->made evalKnight function
    ->made evalbishops function
    ->made initEvalThings
    ->removed passers and passersCnt in board struct
    ->totally replaced pos->passers with bitboards
    ->made the rook behind passed pawn checker different using ForwardFileMasks
    ->added pos->rammedPawns in board struct
    ->changed some things in evalBishops and evalKnights - NO CHANGE IN EVAL
    ->changes in eval added to AddAllScore - NO CHANGE IN EVAL
    ->added pos->attacks_queen[2] and pos->attacks_pawn

    ->added closedness in evaluation

    //CE_CLOSEDNESS

    ->changed bishop evaluation with rammed pawns not lame ass
    ->changed bishop pair code with better code
    ->added in evalPassedPawn attacks_queen in uncontestedPassedPawn reward
    ->fixed attacks in evalPassers uses | instead of &
    ->copied ethereal knight outpost code
    ->copied ethereal knight behind pawn code
    ->removed bishop movemap
    ->copied ehtereal bishop outpost code
    ->copied ethereal bishop behind a pawn code
    ->copied ehtereal long diagonals code
    ->removed pawn_cntrl in initeval and pawnloop and board def
    ->removed queen on open and semi file
    ->added pin attack risk on Eval Queen

    //CE_CLOSEDNESS_PLUS

    ->added pos->attackedBy2[2] in board defs
    ->added pos->attacked[2] in board defs
    ->init the two newly added variable in initEvalThings
    ->added the two newly added variable in mobility.c to be calculated

    ->added evaluateSpace in Eval
    ->added evaluateThreat in Eval

    //CE_SPACETHREAT

    ->removed drawMaterialBOTH in beginning of eval function
    ->replaced in search.c by recog_draw in draw detection
    ->added in draw detection in search.c return 1-(info->nodes & 2);
    ->removed mediocres mid and endgame scaling of scores
    ->replaced with ethereals

    //CE_DRAWFACTOR

    ->added complexity variables
    ->added complexity in eval

    //CE_COMPLEXITY

    ->added KingAreasMasks in bitboard.c
    ->added pos->KingAreas[2]
    ->initing pos->KingAreas in initEvalThings
    ->added pos->pawnAttackedBy2[2]
    ->initing pos->pawnAttackedBy2[2] in initEvalThings
    ->changed the king attack safety calculation in mobility.c
    ->added MIN in king safety eval in EvalPosition in eval.c

    //CE_KingArea

    ->added kingFilePawnDistance variable and function in bitboard.c
    ->added getmsb in bitboard.c
    ->added forwardRanksMasks var and func in bitboard.c
    ->added func backmost in bitboard.c
    ->added evalkingPawns func in eval.c
    ->not initing kingShield in initevalThings
    ->commented out w_king_def and b_king_def
    ->replaced with evalpawnKings
    ->added evaluateKingsPawns in eval
    ->added KingDefenders in eval

    //CE_ALLHAILKING

    ->added pos->occupiedMinusBishops and pos->occupiedMinusRooks
    ->added pos->mobilityAreas[2]
    ->inting the newly added variables in initEvalThings

    ->removed piecetrapped
    ->removed the anding of occupancy in knight attacks in mob.c
    ->removed the anding of occupancy in bishops attacks in mob.c
    ->removed the anding of occupancy in rooks attacks in mob.c
    ->removed the anding of occupancy in queens attacks in mob.c
    ->literally changed the entire code for mobility replaced with ethereal's

    //CE_ULTRAMOB

    ->added variable DistanceBetween in eval.c
    ->added distanceBetween func
    ->added evalPassed Function
    ->pos->attacked added in initEvalTHings Ored with pawn attacks
    ->in evalPawn removed pawn_pos in passers check
    ->changed evalPasser code in eval
    ->removed pawn_pos in board defs
    ->removed pawn_dist_best in board defs

    ->added pos->attackedByBishops[2]
    ->added pos->attackedByKnights[2]
    ->initing them in initEvalThings
    ->added pos->kingAttacksCount[2] and initing
    ->changed the entire king Safety attacks calculations
    ->deleted Safety Tables eval in eval.c
    ->added pos->PkSafety[2] and initing
    ->used pkSafety in evalKingsPawn
    ->swapped arrangement of evalkingspawn and evalKings
    ->copied ethereals king safety code
    ->removed ForceKing in eval
    ->added pos->MATERIAL[2]
    ->removed pos->material in initEvalThings
    ->removed pos->material in pieceloops
    ->removed pos->material in addAllScore
    ->removed pos->material in board defs
    ->added the new pos->MATERIAL and initing and added in loops
    ->CLEANED THE ENTIRE EVALUATION FUNCTION!
    ->

    //CE_ULTRASAFEKING
    //can find Qxf3 at depth 24

    ->removed init of tropism in initeval
    ->removed tropism in pieceEval
    ->removed tropism eval in eval
    ->removed tropism in board defs
    ->added knight in siberia in evalKnight
    ->changed bishopPair value from 30,30
    ->removed queenOnSeventh Eval
    ->removed rookBehindPassedPawn
    ->changed rookOnSeventh code
    ->changed rook openfiles code
    ->tempo value from S(10,0) to just 20
    ->changed tempo code
    ->not initing pos->mobility
    ->removed pos->mobility from board defs
    ->copied ethereals piece values
    ->copied ethereals psq tables

    ->changed seldepth code
    ->added update seldepth in qsearch
    ->added uciReport
    ->tried using uciReport
    ->removed static from evaluate.c masks variables
    ->added initAndcalculateMaterial func in eval
    ->removed pos->material+= in pieceLOOPS
    ->replaced piecelist in EvalPawn with bitboard
    ->replaced piecelist in EvalKnight with bitboard
    ->replaced piecelist in EvalBishops with bitboard
    ->replaced piecelist in EvalRooks with bitboard
    ->replaced piecelist in EvalQueens with bitboard
    ->removed piecelist on board.c
    ->removed piecelist on makemove.c
    ->removed piecelist on board defs
    ->changed rootNode and pvNode conditions in search.c - NO CHANGE IN EVAL AND SEARCH(I think(shit))
    ->move up initialization in alphabeta
    ->added searchtest4 in consolemode.c and QUEENG3 var in defs
    ->removed pos->kingSq from makemove.c
    ->init kingSq in initEvalThings
    ->in uci not clearing pawnTable and evalTable just PvTable
    ->cleaned the bench code in perft.c
    ->changed pawnHash to 16 mb from 32
    ->changed evalHash to 32 mb from 64
    ->printing all three entries of tables
    ->removed useEngine=TRUE in initPolyBook
    ->added option for analyzemode in uci
    ->added analysisMode in EngineOptions defs
    ->set analysisMode in uci mode and consolemode to False
    ->added setoption in uci for analysisMode
    ->now printing all options each uci command
    ->added and uses uciPrint void func in uci.c

    ->moved bestMove and PvMove Collect from below aspiration window to below alphabeta - NO CHANGE
    ->added consoleReport void func

    ->removed bound in uciReport
    ->added in asp window boundreporting
    ->removed nps in UciReport

    //CE_COPIED
    //Qxf3 at depth 34 - 45 mins

    ->changed bench to uperft in consolemode.c
    ->deleted nodes and mate in uci in parseGo
    ->deleted nodelimit=TRUE or nodelimit=FALSE in uci.c parseGo
    ->added nodelimit in checkUp in search.c
    ->added limitStrength and uciElo in EngineOptions
    ->initing them in consolemode and ucimode
    ->added UCI_LimitStrength in uci as option and UCI_Elo
    ->added setoption UCI_LimitStrength and UCI_Elo in uci
    ->added limiting nodes in parseGo for UCI_Elo
    ->initing to false nodeset in consolemode

    ->added analyzeMode in info struct
    ->initing info->analyzeMode to false in parseGo and setting in infinite command
    ->initing in consolemode info->analyzemode to false
    ->not limiting strength if uci send go infinite
    ->changed h3 to 160 from 297 in getting nodes from elo func

    ->added nullMoveDepth in info->struct and EngineOPtions
    ->initing them in consolemode and ucimode
    ->added options nullMoveDepth type spin in uciPrint
    ->added setoption NullMoveDepth in UCI_Loop
    ->now using info->nullMoveDepth in nullMove in alphabeta
    ->added AUTHOR in defs and used in uciPrint
    ->added info strings in setoption in uci
    ->added info string to print in initPVTable
    ->added info string in print in initPolyBook
    ->added info string to print in initEvalHash
    ->added info string to print in initPawnHash

    ->added in info struct useRazoring
    ->initing in consolemode and ucimode
    ->using info->useRazoring in alphabeta
    ->added option useRazoring in uciPrint
    ->added setoption useRazoring
    ->removed mateIn and nullcut in searchinfo struct
    ->made iterative deepening set to maxdepth
    ->added depthSet in info struct
    ->added !ponder in mate break/brake
    ->initing depthset in consolemode and ucimode
    ->now using depthSet in iterative deepening
    ->renamed somethings in info struct
    ->cleaned some things in alpha beta and qsearch

    //CE_ELOLIMIT
    //same as CE_COPIED except can limit elo strength

    ->only clearing TT in new/clear command in consolemode
    ->added analyze in consolemode
    ->added if(pos->ply) in if draw in qSearch
    ->cleaned some things in alphaBeta and Qsearch
    ->added searchtest5
    ->added all attackers to square func in attacks.c
    ->added SEE variables in search.h
    ->added SEE func in search.c
    /////////////////////////////////////////////////////
    ->changed beta pruning code near null

    //CE_BETADIFFPRUNE - WEAKER THAN CE_ELOLIMIT

    ->added seeMargin var in alphaBeta
    ->added SEE Pruning in alphaBeta

    //CE_SEEPRUNE - WEAKER THAN CE_ELOLIMIT

    ->reverted back to CE_ELOLIMIT
    ->added SEE in alphabeta
    ->removed the beta change

    //CE_JUSTSEE

    /////////////////////////////////////////////////////
    ->reverted back to CE_ELOLIMIT
    ->moved moveEstimated value to makemove.c from search.c
    ->deleted ethereals SEE function
    ->deleted 2 getGamePhase funcs instead only onde now in board.c
    ->coded an experimental function in see.c
    ->used SEE_BB in movegen.c

    //CE_SEE_BB

    ->const S_BOARD in allaAttackersToSq
    ->const S_BOARD in SEE_BB
    ->reverted to CE_ELOLIMIT
    ->added report UciCurrentMove in search.c
    ->in uci.c Hash default changed to 128 from 64
    ->added deaultHash var in defs and used in main.c and uci.c
    ->added maxHash variable in defs
    ->rmeoved argument in uciPrint max_hash instead using maxHash var
    ->rmeoved max_hash replaced with maxHash in UCI_Loop
    ->in pvtable.c//tteval.c initPvTable removed /2 in info string failed to init
    ->defined NOHASHEVAL=INFINITE+1 in defs.h
    ->made INFINITE value to 40k from 30k //because eval function returns score higher than infinite causing to print mate 0
    ->renamed NOHASHEVAL to VALUE_NONE

    //CE_ELOLIMIT

    ->changed ASSERT code in defs.h
    ->added asserts in test in validate.c
    ->added asserts in attacks.c
    ->added asserts in bitboard.c
    ->added asserts in makemove.c
    ->added asserts in movegen.c
    ->added asserts in perft.c
    ->added asserts in perft2.c
    ->added asserts in pvtable.c
    ->added asserts in recog.c
    ->added asserts in search.c
    ->added asserts in see.c
    ->added asserts in tteval.c
    ->added 1 assert in evaluate.c
    ->moved pawnAttacks from eval.c to attacks.c
    ->added assert to pawnAttacks
    ->put more assert in attacks.c
    /////////////////////////////////////////////
    /                                           /
    //       NEURAL-RACIST EVAL ERA             /
    /                                           /
    /////////////////////////////////////////////
    ->added pkHash in defs
    ->added pkHash in board.c and makemove.c
    ->added nneval.c and nneval.h
    ->initing endgameNNs in main.c
    ->added evalEndgames in eval - NOW ENGINE IS RACIST in evaltest POSITION 670
    ->added network.c and network.h
    ->initing network in main.c
    ->added PkNetwork in eval

    //CE_NEURAL_RACIST0

    ->added pk Entry and pk Table in tteval.h
    ->added pkTable in board defs
    ->made some pk functions in tteval.c
    ->initing pkTable in main.c consolemode
    ->freeing pkTable in main.c
    ->probing and storing pkEval in evaluate
    ->added some clearPKTable in searchtests
    ->added in uciPrint option pawnKingHash
    ->added setoption PawnKingHash in uci.c

    //CE_NEURAL_RACIST

    ->removed PKNETWORK in eval

    //CE_JUSTENDGAMENN

    ->reverted back to previous version
    ->removed evaluateEndgames in eval

    //CE_JUSTPKNN

    ->reverted back to CE_NEURAL_RACIST
    ->added usePKNN and useEGNN in board defs
    ->initing usePKNN and useEGNN in consolemode and ucimode default false
    ->added in uciPrint options UseEGNN and UsePKNN default false
    ->added setoption in uci_loop about NNs
    ->added options in NNs in evalposition
    ->added contemptDrawPenalty and contemptComplexity and contempt in board defs
    ->initing the three newly added vars
    ->added in uciPrint contempts
    ->added setoptions contempt in uci_loop
    ->added contempt in eval

    ->added in uciPrint Clear Eval type button
    ->added setoption Clear Eval in uci_loop
    ->added clearEvalTable to every option that affects EvalPosition

    //CE_HYBRIDNN

    ->EGNN set default to true in uciPrint
    ->initing useEGNN in UCI_Loop default true
    ->storing eval before adding tempo now
    ->storing score without caring what side is moving
    ->added NULL move eval recognizer
    ->replaced RAND_64 in init.c with ethereals

    //CE_REPLACEDRAND64 - Almost no difference to CE_HYBRIDNN

    ->added not best move condition in update searchkillers in alphabeta

    ->fixed pos->contempt in uci.c

    ->removed neighborBB in init.c and eval.c
    ->replaced neighborBB with IsolatedMask
    ->removed PawnConnectedMasks in eval.c
    ->removed OutpostRanksMasks in eval.c
    ->moved getSquareRanksMasks to bitboard.c from eval.c
    ->cleaned the uci.c code
    ->removed nullMoveDepth in EngineOptions
    ->cleaned more code in uci.c
    ->removed nullMoveDepth in info struct
    ->removed NUllMoveDepth in uciPrint
    ->removed limitStrength in EngineOptions
    ->in parseGo initing info->analysis from EngineOptions from FALSE
    ->added function UciSetOption in uci.c
    ->added new UCILoop in uci.c and using it
    ->added fflush(stdout) in ucireports in search.c
    ->removed old UCI_Loop code

    //CE_KILLERUPDATED

    ->added perft to ucimode
    ->deleted speed and time in benchtest
    ->added PRIu64 in board.c consolemode.c perft.c and perft2.c
    ->replaced all the llu in search.c
    ->added to defs the uci string funcs
    ->added MoveBestCaseValue in search.c
    ->added delta pruning before move generation

    CE_BOTHDELTA

    ->removed the after movegen delta

    //CE_BEFOREDELTAONLY

    ->reverted back to CE_KILLERUPDATED
    ->saving evalStack in QSearch now
    ->not overwriting in PVTable

    //CE_DONTOVERWRITE

    ->reverted to CE_KILLERUPDATED

    ->added function AttackerToKingSq in attacks.c
    ->in makemove.c replaced inCheck with kingAttackersSq
    ->replaced inCheck by attackersToKingSq in consolemode.c and in makeMoves in perft.c
    ->pos->hisPly++ moved down to pos->side^=1 originally from above piecePawn
    ->inCheck in search.c replaced with attackerToKingSq
    ->nch in search.c replaced with attackerToKingSq
    ->forwardRanksMasks added to bitboard.c originally from evaluate.c
    ->ForwardFileMasks moved to bitboard.c originallt from eval.c
    ->moved OutpostSquareMasks to bitboard.c originally from eval.c
    ->moved ClearMask to bitboard.c originally from init.c
    ->moved CLRBIT to bitboard.h
    ->removed masks in init.c and defs.h
    ->improving in top of pos->eval_stack originally below moveGen in alphabeta
    ->iid depth changed to 4 from 5

    //CE_IID4
    //Qxf3 at depth 32

    ->moved inCheck to init in top of alphabeta
    ->not dropping to QSearch if inCheck
    ->returning 0 if incheck in near Horizon ply in alphaBeta and Qsearch
    ->just returning 0 if Qsearch board_drawn
    ->changed the mate distance pruning code with berserk

    //CE_CLEANUP

    ->reverted back to CE_IID4
    ->removed mate pruning
    ->mate brake at depth 5 now
    ->only reporting Bound if 1 second has passed
    ->replaced INFINITE-100 in beta pruning with -INF+MAXDEPTH
    ->replaced pieceVal in search.c with SEEPieceValues
    ->replaced pieceVal in see.c with SEEPieceValues

    //CE_SEEPIECEVAL
    //almost the same as CE_IID4 maybe +5 elo diff

    ->moved pieceVal to eval.c
    ->made PiecesVal var and removed pieceVal and pieceValE
    ->made enum 6 pieces in defs.h
    ->made those enum to fit with PiecesVal and computeNetworkIndex
    ->clean eval.c codes
    ->removed psq.c and psq.h
    ->moved int Distance and other related funcs to the top originally below Functions
    ->FIXED A SERIOUS BUG IN EVAL NOW REWARDING MOBILITY BEFORE CALCULATING KING ATTACKS  ------ !!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    //CE_FIXEDMOB

    ->set default to true in UsePKNN in uciPrint
    ->initing UsePKNN in UCILoop to TRUE

    ->now getting fifty move counter in fen in board.c parseFen

    //CE_PKNNAGAIN

    ->added to printBoard checkers
    ->added mate pruning in alphabeta

    ->made GenAllQuiets function in movegen.c
    ->added uci.h
    ->moved the SearchPosition extern from defs.h to search.h
    ->added hashkeys.h
    ->added consolemode.h
    ->moved the ConsoleLoop extern from defs.h to consolemode.h
    ->moved the EvalPosition extern from defs.h to evaluate.h
    ->moved the structs from tt_eval to defs.h
    ->moved uciReport and uciCurrrentMove from search.c to uci.c
    ->moved the struct from tt_eval.h to defs.h
    ->moved tt_eval funcs extern form defs.h to tt_eval.h
    ->added movepicker.c and movepicker.h - STILL NOT USED

    ->added chess960 in board defs
    ->initing chess960 in consolemode and ucimode
    ->added in UCIPrint UciSetoption chess960
    ->added in evaluate.c bishopTrapped if pos->chess960
    ->added rookTrapped in chess960 trapped in eval

    //CE_CHESS960
    //same as CE_PKNNMATEPRUNE with chess960 things
    //can defeat AnMon 40/15
    //can defeat ruffian as white 3sec each move
    //can defeat Hermann as black 5sec each move
    //can draw S.O.S and Rybka in Arena

    ->version updated to v1.3.2
    ->added some codes in checkboard in board.c
    ->externed the drawByMaterial in recog.c to recog.h
    ->deleted DrawMaterial in consolemode.c replaced by drawByMaterial in recog_draw
    ->replaced pos->numPieces in closedness and initMaterial with COUNTBIT(bb)
    ->replced with COUNTBIT(bb) in gamePhase in board.c
    ->removed numPieces in makemove.c
    ->removed pos->numPieces[13] in board defs
    ->used some uci string function in consolemode
    ->cleaned some code in consolemode.c
    ->removed all static in attacks.c consolemode.c and eval.c from vars
    ->changed in eval.c static inlines to INLINE
    ->INLINE in makemove.c
    ->INLINE in movegen.c
    ->INLINE in network.c
    ->INLINE in nneval.c
    ->INLINE in search.c
    ->removed statics in perft and perft2 c files
    ->just static in takeMovess in perft.c no INLINE
    ->removed INLINES in perft.c and perft2.c
    ->removed INLINES in eval.c
    ->removed INLINEs in makemove.c and movegen.c
    ->removed INLINEs in network.c and nneval.c reverted to orig code
    ->removed statics INLINE in search.c
    ->added INLINES in eval.c
    ->moved boundReport from search.c to uci.c
    ->moved MoveBestCaseValue from search.c to makemove.c
    ->moved PickNextMove to movepicker.c from search.c
    ->added INLINEs to search.c
    ->added INLINES to perft.c
    ->added INLINES to makemove.c and movegen.c
    ->cleaned consolemode.c
    ->added depth==0 in perft.c
    ->renamed nodes func in uci.c to nodesLimitForUci
    ->added Unknown command in UciLoop
    ->added divide command in UciLoop
    ->added bench command in UciLoop
    ->added endgame.c and endgame.h
    ->added KnownPositionDrawn in recog.c - NOT USED
    ->replaced pos->bigPieces[pos->side]>=2 in null move with func from bits.c boardHasNonPawnMaterial
    ->removed bigPiece in board defs

    ->replaced in delta prune in Qsearch with SEE_BB originally with phase<ENDING

    ->removed pos->gamePhase in board defs and makemove.c
    ->cleaned getGamePhase code

    //CE_DELTASEE/CE_DATASEE
    //almost equal to CE_CHESS960 +- 3 elo

    ->added argc,argv in main()
    ->made DistanceBetween var from eval global
    ->experiments in endgame.c
    ->removed func getINputFromCli in consolemode.c replaced with old ver
    ->initing useEGNN and usePKNN in consolemode.c to TRUE
    ->modifications in runSearchTest function
    ->clearing everything after running searchtest

    ->now using EGNN and PKNN in searchtest
    ->removed time limit for searchtest 1 and 2
    ->now printing time in consoleReport and passing as args S_SEARCH *info
    ->removed bench in UciLoop
    ->added more ASSERTs in attacks.c and bitboards.c
    ->moved 5 funcs from bitboard.c to attacks.c
    ->added ponderMove in consoleReport
    ->cleaned up the code where you extract the ponderMove in searchPosition
    ->made moveValid func in validate.c
    ->added Asserts in see.c
    ->changed mate brake code
    ->added ASSERTS in perft.c and perft2.c
    ->added ASSERTS in movegen.c
    ->removed and added new asserts in makemove.c

    ->added threat var in AlphaBeta
    ->added threat check in Null Move Pruning
    ->using threat in futility pruning as one of its condition
    ->using threat in LMR as one of its condition

    //CE_THREAT

    ->reverted back to CE_DATASEE/CE_DELTASEE
    ->not using the networks again in searchtest
    ->reverted back to chess960

    ->printing book line in searchPosition
    ->added infinite in searchinfo defs
    ->initing variable in both modes
    ->checking infinite command in uci
    ->using UciInfinite as condition for limiting strength in func checkUp in search.c
    ->changed code in checkUp in search.c
    ->changed name of nodelimit and nodeSet to EloNodeSet and EloNodeLimit
    ->added nodeSet and nodeLimit in searchinfo defs
    ->initing var in both modes
    ->checking in parseGo nodes command and setting node limits
    ->using node limits in search.c checkUp
    ->added mateLimit in searchinfo defs
    ->initing mateLimit in parseGo and checking in commands
    ->using mateLimit in searchPosition

    ->added !info->UciInfinite in SearchPosition Iterative deepening
    ->used mateIn for mate Limits/Break
    ->not using mate break if mateLimit set
    ->removed perft and divide in UCILoop
    ->not limiting elo if analyzeMode in checkUp
    ->changed mate break code to mateIn*2 + 10
    ->experiments with ForceKingAwayFromCenter in main.c and added the func in eval.c
    ->added gamePhase in board defs
    ->initing pos->gamePhase in initEvalThings and using it
    ->moved moveExist func from movegen to makemove
    ->made materialScore func originally initAndCalculateMaterial
    ->removed pos->MATERIAL[2] in board defs
    ->made default elo 2600

    //CE_CHESS960_2
    //same as CHESS960 except not getting gamePhase in move gen

    ->not including pseudomove in PrintMoveList
    ->added ttDepth and ttBound in ProbeHashEntry func args in pvTable.c
    ->added ttDepth and ttBound variable in AlphaBeta
    ->added ttValue and replaced &Score in AlphaBeta Probe
    ->renamed pvMove to ttMove
    ->added multiCut in AlphaBeta
    ->made func Singularity in search.c
    ->added singular and newDepth and extension vars in AlphaBeta
    ->removed check extension and added Qsearch check checker
    ->replaced inCheck in LMR R formula with extension
    ->using newDepth in LMR and PVS
    ->made prototype Singularity before AlphaBeta

    //CE_SINGULAR

    ->made valueFromTT func and valueToTT in pvtable.c
    ->added eval as args in StorPvtable function
    ->added int eval in PV Entry in defs.h
    ->added ttEval vars in AlphaBeta
    ->now storing and probing ttEval in AlphaBeta

    ->changed ProbeHashEntry code in pvtable.c - Now not checking ttDepth > depth
    ->removed alpha,beta,depth as args in ProbeHashEntry
    ->changed code in AlphaBeta where we Probe TT - Changed conditions
    ->added not storing in TT if worse in StorPvTable
    ->added ttHit and using it as condition in NullMovePruning

    //CE_DIFFPROBE

    ->renamed StorePvTable to StoreHashEntry
    ->removed 'for( int' 16 matches just refactored
    ->removed Score var in Qsearch replaced with eval and in move gen loop replaced with value
    ->cleaned QSearch code
    ->removed multiCut = False in movegen loop
    ->added in info defs BruteForceMode
    ->initing in both modes
    ->added uciPrint BruteForceMode type check
    ->added setoption BruteForceMode
    ->now using info->BruteForceMode in search.c AlphaBeta QSearch SearchPosition funcs

    //CE_BRUTEFORCABLE
    //the same as CE_DIFFPROBE except it can turn off all pruning

    ->made func SEE in search.c
    ->added seeMargin[2] in AlphaBeta
    ->initing seeMargin in AlphaBeta
    ->added SEE Pruning in AlphaBeta
    ->pruning SEE only if quiet
    ->removed SEE in movegen
    ->now pruning SEE in QSearch

    //CE_SEEAGAIN

    ->added SEE pruning in ProbCut

    //CE_PROBSEE

    ->changed return alpha to return BestScore
    ->changed Score saving to TT with only bestScore
    ->made func updateKillers in pvtable.c and using it in AlphaBeta and Singularity

    //CE_BESTSCOREPV

    ->reverted back to CE_PROBSEE
    ->experiments in movepicker.c
    ->made mvvlvaScore Global in movegen.c
    ->commented every scoring of moves in movegen and AlphaBeta
    ->using InitAllScores in search.c
    ->made moveIsTactical func in makemove.c

    ->using moveIsTactical in movegen loop and updateKIllers and searchHistory
    ->using moveIsTactical in Singularity
    ->using moveBestCaseValue in ProbCut instead of 1000
    ->added staticEval >= beta as one of the conditions in NMP
    ->used Ethereals NMP depth reduction formula
    ->removed abs(valueNull)<ISMATE in condition

    //CE_LOTSCLEANED

    ->if UciInfinite turn off book
    ->extensions excluded if bruteForceMode
    ->using pvs search in bruteForceMode
    ->added pos->useFiftyMoveRule in board defs
    ->initing those variables in both modes
    ->added in uciOPtions useFIftyMoveRule
    ->using pos->useFiftyMoveRule in recog_draw func
    ->boundReport from uci.c now has a parameter bestMove

    //CE_PVSBRUTE

    ->removed complexity and closedness eval
    ->removed evalSpace in eval
    ->on evalKnight and evalBishop only evaluating piece squaring and mobility
    ->also on queens and rooks eval functions
    ->removed threats eval

    //CE_MODDED_ETH

    ->reverted back to ce_PVSBRUTE
    ->added ForceKingAwayFromCenter in eval
    ->now adding bonus if passer has a rook beneath protecting it
    ->now adding bonus depending on gamePhase in EvalPasser

    ->CE_MODDED_V1

    ->changed name to GUBED
    ->added pessimism

    //CE_PESSIMIST

    ->reverted to PVSBRUTE
    ->added pessimism

    //CE_BRUTEPESSIMIST

    ->removed pessimism

    //CE_PVSBRUTE

    ->added ethereal technique at probing hashtable still accepting
    ->added alpha pruning
    ->changed the condition in storing hash entries
    ->reverted to CE_PVSBRUTE
    ->added nnue files from bbc_nnue
    ->added nnue_pieces variable in eval.c
    ->added USE_NNUE in board defs
    ->initing the variable in all modes
    ->added in uciPrint an option for NNUE
    ->added in setOPtion in uci.c USE_NNUE
    ->now storing in TT_EVAL no need for side to adjust

    //CE_NNUE

    ->now storing tempo in TTEVAL
    ->if infinite mode use nnue
    ->if mate searcher use nnue
    ->if we are worse in material advantage use nnue
    ->now only using nnue if we are legit worse or if we are searching for a long time
    ->added UCi_USE_NNUE variable in board defs
    ->initing variable in both sides
    ->using variable in eval.c and setoption and parsego
    ->in search.c only using null move recognizer if !UCI_NNUE

    //CE_NNUE_HYBRID

    ->removed conditions on USE_NNUE
    ->removed null move recognizer in eval.c and search.c
    ->removed UCI_NNUE
    ->not storing tempo in TTEval
    ->removed all pkFunctions
    ->removed uci option setoption PK Hash also in uciPrint
    ->removed all pk structs
    ->pkEval added in board defs
    ->added pkEval in PAWN TT struct
    ->deleted variable pawnHash

    //CE_NNUE

    ->made getClassicalEval func in eval.c
    ->now scaling the nnue eval
    ->now adding tempo in nnue eval
    ->removed pst tables in evalPieces instead a difference function
    ->added material_score var on top of evalBoard
    ->

    //CE_NNUE_HYBRID

    ->made PSQTMAT TABLE var global in eval.c
    ->added var psqtmat in board defs
    ->initing psqtmat in ResetBoard in board.c
    ->updating psqtmat in makemove.c
    ->reverted to CE_NNUE
    ->using new NNUE file

    //CE_NNUE2

    ->removed the fiftyMove scaling in NNUE eval replaced with scale from Ethereal
    ->new scaling

    //CE_NNUE_SCALED

    ->separated nnue eval to midgame and endgame score
    ->now adding complexity to nnue eval

    //CE_NNUE_HIGH

    ->can now use contempts in NNUE Eval
    ->now nnue is disabled if already winning
    ->reverted to CE_NNUE_HIGH
    ->fixed uci printing in parseGo and Setoption
    ->only using nnue if we position is neutral
    ->added EvalFile option in uciPrint
    ->added EvalFile in setoption

    //CE_NNUE_CONTROLLED

    ->added fiftyMoveRule scaling in evaluation

    //CE_EVALFIFTY

    ->made history.h and history.c
    ->moved updateKillers to history.c
    ->not initing eval_stack in initSearcher
    ->created a function void initStacks and using in main.c
    ->moved MOVE maker macro to defs.h
    ->defined NULLMOVE with only a flagcapture
    ->in applyNullMove now storing in history NULLMOVE not NOMOVE
    ->added moveStack and pieceStack in board defs and initing in initStacks
    ->added p_variables in defs.h
    ->added data in data.c pieceType and made it global
    ->now updating moveStack in makeMove and applyNullMove
    ->now updating pieceStack in makeMove
    ->added macro ALIGN64
    ->added typedef ContinuationTable
    ->added continuation variable in defs.h
    ->made resetContinuationTable void func in board
    ->using that void func in main.c and ucinewgame
    ->made updateHistories, and getHistory in history.c
    ->added quietsTried and quietsPlayed in AlphaBeta
    ->using updateHistories in AlphaBeta
    ->added fmhist and cmhist in AlphaBeta
    ->pruning follow and counter move
    ->added new condition in extension
    ->uciPrint and setoption removed NNUE
    ->not initing nnue in main.c
    ->removed NNUE in eval
    ->added null move recognizer

    //CE_HISTORY

    ->added futility vars in search.h
    ->added typedefs for tables in defs.h
    ->added tables in board defs
    ->initing them
    ->updating histtable in updateHistory
    ->made function void updateCaptureHistory in hist.c
    ->added capturesTried and capturesPlayed in alphaBeta
    ->now updating captureHistory in AlphaBeta
    ->now only updating History if best move was not tactical
    ->made function int getCaptureHistory
    ->made int getHistory
    ->now added hist and initializing in mainloop alphaBeta
    ->Score = -INFINITE in Search Loop
    ->skipQUiets too
    ->now added futility pruning with skipquiets
    ->LMP uses skipquiets too
    ->replaced LMR AND PVS with ethereals
    ->now clamping in LMR R reduction depth
    ->deleted movesSearched
    ->deleted old futility code

    //CE_ULTRA_HISTORY

    ->skipping killer moves if skipQuiets
    ->removed bruteforcemode in uciPrint and setoption
    ->removed bruteforcemode in search.c
    ->removed bruteForceMode in everything
    ->made MAXPOSMOVES to 256 from 300
    ->made MAXGAMESMOVES to MAXPOSMOVES*2 from 3000
    ->deleted returnValue in polybook.c
    ->added typedef cmtable in defs.h
    ->added cmtable in board defs
    ->initing it in resetContinuation Table
    ->updating cmtable in history.c
    ->now scoring counter moves below KILLER in movepicker.c
    ->now using that for sorting move
    ->only pruning quiet moves with score less than SORT_COUNTER
    ->made the SORT_SCORES bigger
    ->now scoring bad quiets based on histtable
    ->removed searchHistory on everything
    ->in InitAllScore only scoring quietmoves if !moveIsTactical
    ->now scoring promotion moves as CAPTURE
    ->now scoring captures from chist
    ->changed in main loop condition to capturesPlayed > 2 from quietMove

    //CE_ULTRA_BASED_HISTORY

    ->changed aspiration windows like ethereals
    ->changed UciPrinting method like ethereals
    ->now cannot use books in uci
    ->removed Bookfile and book in UciPrint and SetOption
    ->added boundReport in UciReporting
    ->now only using one pv both for UCIReport and updating bestmove

    //CE_UCI_NEW

    ->reverted to CE_ULTRA_BASED_HISTORY
    ->removed book in UciPrint and UciSetOption
    ->added getCaptured in makemove.c
    ->now producing promotion moves in genCaptures
    ->removed GenQuietMoves
    ->removed INLINE funcs AddCaptureMove,AddEnpasMove,AddquietMove, replaced with AddMovee
    ->not initing mvvlva and polybook in init.c
    ->removed initMvvlva in movegen.c and removed global mvvlva

    //CE_FIXED_UBH

    ->removed option useRazoring in uci and removed also in search.c
    ->added genType in InitAllScore
    ->added genType in movepicker.h
    ->made SEE global in search.h
    ->now scoring captures -1 in InitAllScore if normal_mode
    ->changed condition in SEE Pruning in mainLoop replaced capturesPlayed > 2
    ->changed skipQuiets Pruning method now pruning
    ->added skipQuiets in Singular
    ->added threshold in InitAllScore
    ->removed genType
    ->replaced SEE Pruning in QSearch and ProbCut
    ->clamping newDepth
    ->in LMR clamping renamed 63 to MAXDEPTH/2 - 1
    ->now we consider it too deep if ply >= MAXDEPTH - 1 = 127
    ->removed in uciPrint and setOption Clear Eval
    ->added generation in PVTable and PVEntry
    ->added updateAge in pvtable.c
    ->clearing variable generation in clearTable
    ->initing variable generation in initTable
    ->updating age in clearSearch
    ->updating age in StoreTTEntry and ProbeHashEntry
    ->added hashfullTT function in pvtable.c
    ->added hashfull printing in Uci and Bound Reporting

    //CE_ULTRA_FIXED_UBH

    ->set maxHash to 1024
    ->set defaultHash to 64
    ->deleted pkHashDefault
    ->set default elo to 2700
    ->renamed pawnTable to pawnKingTable
    ->renamed the funcs in tteval to pawnKing
    ->removed USE_NNUE in board defs
    ->removed GAMEMODE and useRazoring in SEARCH defs and POST_THINKING
    ->removed consolemode.c and .h
    ->removed polykeys.c and .h and polybook.c
    ->removed perft2.c and perft.h
    ->removed endgame.c and .h
    ->removed getNNUEEval in evaluate.c
    ->deleted all nnue files
    ->added noisy parameter in InitTable
    ->also added noisy parameter in EvalTable and PawnKingTable
    ->in UciReport added pvMoves as parameter
    ->removed boundReport replaced with UciReport
    ->now printing bounds in UciReport
    ->fixed futility variables in search.h //embarassing mistake

    //CE_FIXED_TWICE

    ->added bruteforcemode in info defs
    ->initing bruteforcemode in uci
    ->added bruteforcemode in uciPrint and setOption
    ->not using aspiration window if bruteforcemode
    ->not using pruning too in quiscence if brute
    ->not using forward pruning if brute
    ->not using probCut if brute
    ->not using IIR if brute
    ->not pruning quiets if brute
    ->and SEE in main loop search
    ->not using extensions if bruteforcemode
    ->not using LMR if brute
    ->clearing hash if toggled bruteForceMode
    ->made MAXGAMESMOVES more than 512 to 550 //currently the version being used as analyzer in my python app

    //CE_WITH_BRUTE

    ->changed the name again to GOOB

    //CE_GOOB

    ->removed pkEval in board defs
    ->removed pkEval in ProbePawnKingEval
    ->removed pkEval in StorePawnKingEval
    ->removed pkEval in PawnKing Table in defs.h
    ->removed pkEval in eval.c
    ->removed pkEval in everything
    ->removed usePKNN in uciPrint and UCISetOption
    ->removed nneval.c and nneval.h
    ->removed network.c and network.h
    ->removed EGNN in eval.c
    ->removed useEGNN in UciPrint and UciSetOption
    ->removed initPKNN and initEndgameNN
    ->removed evalSpace and evalThreats
    ->removed evalClosedness
    ->removed evalComplexity
    ->added reward for king trapping opponent king in corners

    CE_GOOB_ORIGINAL

    ->
    ->
    ->
    ->

	*/





	/*

        NOTE:
            Evaluating pieces inspired by Ethereal

        SCARY THINGS
            ->Pondering code may not be complete(shit)
            ->evaluate.c
                >I don't know but i notice the code in eval.c is very fragile
                >Just delete one comment and the whole thing will become racist(that kind of fragile)
            ->init.c variables
                >variables there are also very fragile like in evaluate.c
            ->kind of worried about the new RAND_64 generator
            ->now getting the fiftymove counter in parseFEN
	*/


    return 0;
}
