#include "defs.h"
#include "stdio.h"
#include "string.h"

static int threeFoldRep(const S_BOARD *pos){
    int i=0,r=0;
    for(i=0;i<pos->hisPly;++i){
        if(pos->history[i].posKey==pos->posKey){
            r++;
        }
    }
    return r;
}
static int DrawMaterial(const S_BOARD *pos){
    if(pos->numPieces[wP] || pos->numPieces[bP]) return FALSE;
    if(pos->numPieces[wQ] || pos->numPieces[bQ] || pos->numPieces[wR] || pos->numPieces[bR]) return FALSE;
    if(pos->numPieces[wB] > 1 || pos->numPieces[bB] > 1) {return FALSE;}
    if(pos->numPieces[wN] > 1 || pos->numPieces[bN] > 1) {return FALSE;}
    if(pos->numPieces[wN] && pos->numPieces[wB]){return FALSE;}
    if(pos->numPieces[bN] && pos->numPieces[bB]){return FALSE;}

    return TRUE;
}
static int checkResult(S_BOARD *pos){
    if(pos->fiftyMove>=100){
        printf("1/2-1/2 {fifty move rule (claimed by Engine)}\n");return TRUE;
    }
    if(threeFoldRep(pos)>=2){
        printf("1/2-1/2 {3-fold repitition (claimed by Engine)}\n");return TRUE;
    }
    if(DrawMaterial(pos)==TRUE){
        printf("1/2-1/2 {insufficient material (claimed by Engine)}\n");return TRUE;
    }

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int moveNum=0;
    int found=0;

    for(moveNum=0;moveNum<list->count;++moveNum){
        if(!makeMove(pos,list->moves[moveNum].move)){
            continue;
        }
        found++;
        takeMove(pos);
        break;
    }

    if(found !=0)return FALSE;

    int inCheck=is_square_attacked_BB(pos->kingSq[pos->side],pos->side^1,pos);

    if(inCheck==TRUE){
        if(pos->side==WHITE){
            printf("0-1 {black mates (claimed by Engine)}\n");return TRUE;
        }else{
            printf("1-0 {white mates (claimed by Engine)}\n");return TRUE;
        }
    }else{
        printf("\n1/2-1/2 {stalemate (claimed by Engine)}\n");return TRUE;
    }

    return FALSE;

}

void Console_Loop(S_BOARD *pos, S_SEARCHINFO *info) {

	printf("\nChessEngine %s\n",NAME);
	printf("Type help for commands\n\n");

	info->GAMEMODE = CONSOLEMODE;
	EngineOptions->useBook=FALSE;
	info->POST_THINKING = TRUE;
	setbuf(stdin, NULL);
    setbuf(stdout, NULL);

	int depth = 63, movetime = 5000;
	int engineSide = BOTH;
	int move = NOMOVE;
	char inBuf[80], command[80];

	engineSide = BLACK;
	ParseFEN(START_FEN, pos);

	int perft=0;
	while(TRUE) {


		fflush(stdout);

		if(pos->side == engineSide && checkResult(pos) == FALSE) {
			info->starttime = getTimeMs();
			info->depth = depth;

			if(movetime != 0) {
				info->timeset = TRUE;
				info->stoptime = info->starttime + movetime;
			}else{
			    info->timeset=FALSE;
			}

			SearchPosition(pos, info);
		}

		printf("\nGOOB -> ");

		fflush(stdout);

		memset(&inBuf[0], 0, sizeof(inBuf));
		fflush(stdout);
		if (!fgets(inBuf, 80, stdin))
        continue;

		sscanf(inBuf, "%s", command);

		if(!strcmp(command, "help")) {
			printf("Commands:\n");
			printf("quit - quit game\n");
			printf("movelist - see all possible moves\n");
			printf("caplist - see all capture moves\n");
			printf("force - stop ChessEngine To think\n");
			printf("print - show board\n");
			printf("post - show what the Engine is thinking\n");
			printf("nopost - do not show thinking\n");
			printf("new - start new game\n");
			printf("go - set computer thinking\n");
			printf("depth x - set depth to x\n");
			printf("perft x - perft to depth x\n");
			printf("clear - clear hashtables\n");
			printf("evaltest - test evaluation\n");
			printf("searchtest - test search function\n");
			printf("searchtest2 - position with race of kings\n");
			printf("book - use book\n");
			printf("ftm - show board state things\n");
			printf("nobook - don't use book\n");
			printf("time x - set thinking time to x seconds (depth still applies if set)\n");
			printf("view - show settings\n");
			printf("setboard x - Parse fen x to a board\n");
			printf("undo - Undo move\n");
			printf("** note ** - to reset time and depth, set to 0\n");
			printf("enter moves using algebraic notation\n\n\n");
			continue;
		}

		if(!strcmp(command, "setboard")){
			engineSide = BOTH;
			ParseFEN(inBuf+9, pos);
			continue;
		}
		if(!strcmp(command, "evaltest")){
			S_BOARD pos1[1];
            test(pos1);
			continue;
		}
		if(!strcmp(command, "searchtest")){
            clearPvTable(pos->pvTable);
			ParseFEN(TEST_SEARCH,pos);
            engineSide=pos->side;
			continue;
		}
		if(!strcmp(command, "searchtest2")){
		    clearPvTable(pos->pvTable);
			ParseFEN(KING_RACE,pos);
            engineSide=pos->side;
			continue;
		}
		if(!strcmp(command, "movelist")){
            S_MOVELIST lists[1];
            GenerateAllMoves(pos,lists);
			PrintMoveList(lists);
			continue;
		}
		if(!strcmp(command, "caplist")){
            S_MOVELIST lists[1];
            GenerateAllCaptures(pos,lists);
			PrintMoveList(lists);
			continue;
		}

		if(!strcmp(command, "quit")) {
			info->quit = TRUE;
			break;
		}
		if(!strcmp(command, "ftm")) {
			printf("fiftymove %d\n",pos->fiftyMove);
			continue;
		}

		if(!strcmp(command, "post")) {
			info->POST_THINKING = TRUE;
			continue;
		}
		if(!strcmp(command, "clear")) {
			clearPvTable(pos->pvTable);
			continue;
		}

		if(!strcmp(command, "print")) {
			PrintBoard(pos);
			continue;
		}

		if(!strcmp(command, "nopost")) {
			info->POST_THINKING = FALSE;
			continue;
		}

		if(!strcmp(command, "force")) {
			engineSide = BOTH;
			continue;
		}
		if(!strcmp(command, "undo")) {
            takeMove(pos);
            pos->ply=0;
			engineSide = BOTH;
			continue;
		}

		if(!strcmp(command, "view")) {
            //printf("fiftymove %d \n",pos->fiftyMove);
            if (EngineOptions->useBook==TRUE)printf("book true \n");
            else printf("book false \n");
			if(depth == MAXDEPTH) printf("depth not set \n");
			else printf("depth %d\n",depth);

			if(movetime != 0) printf("movetime %ds\n",movetime/1000);
			else printf("movetime not set\n");

			continue;
		}

		if(!strcmp(command, "depth")) {
			sscanf(inBuf, "depth %d", &depth);
		    if(depth==0) depth = MAXDEPTH;
			continue;
		}
		if(!strcmp(command, "perft")) {
			sscanf(inBuf, "perft %d", &perft);
		    if(perft==0) perft = MAXDEPTH;
		    PerftTest(perft,pos);
		    engineSide=BOTH;
			continue;
		}

		if(!strcmp(command, "time")) {
			sscanf(inBuf, "time %d", &movetime);
			movetime *= 1000;
			continue;
		}

		if(!strcmp(command, "new")) {
            clearPvTable(pos->pvTable);
			engineSide = BLACK;
			ParseFEN(START_FEN, pos);
			continue;
		}

		if(!strcmp(command, "go")) {
			engineSide = pos->side;
			continue;
		}

		if(!strcmp(command, "book")) {
			EngineOptions->useBook=TRUE;
			continue;
		}

		if(!strcmp(command, "nobook")) {
			EngineOptions->useBook=FALSE;
			continue;
		}

		move = ParseMove(inBuf, pos);
		if(move == NOMOVE) {
			printf("Command INVALID:%s\n",inBuf);
			continue;
		}

		makeMove(pos, move);
		pos->ply=0;
    }
}
