#include "stdio.h"
#include "stdlib.h"
#include "defs.h"
#include "string.h"

//*****NOTE*****
/*////////////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*\

    *******Most of the block of codes are based
    from watching the series by bluefever
    software on youtube about making a
    chess engine in C, and i modified some of the code,
    removed the 120 element array and replace them with bitboards,
    didnt really make a difference in terms of speed, and some improvements
    in the alpha beta*****

    *****Bitboard codes based on BBC chess engine tutorial on youtube by CMK*****
    ****Make sure you go visit their yt channel****

    ***BASICALLY MY CODE IS A MIX OF VICE AND BBC AND WUKONG***
    HEHEHE

\*\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\//////////////////////////////////////////*/

int main()
{
    AllInit();

    S_BOARD pos[1];
    pos->pvTable->pTable=NULL;

    S_SEARCHINFO info[1];
    info->quit=FALSE;

    int hashtableMb=128;

    InitPvTable(pos->pvTable,hashtableMb);

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    printf("\n\nby Gabriel M.\n");
	//printf("\nType 'con'\n");

	char line[256];
	while (TRUE) {
		memset(&line[0], 0, sizeof(line));

		fflush(stdout);
		if (!fgets(line, 256, stdin))
			continue;
		if (line[0] == '\n')
			continue;
		if (!strncmp(line, "uci",3)) {
			UCI_loop(pos, info);
			if(info->quit == TRUE) break;
			continue;
		}else if (!strncmp(line, "debug",5))	{
			Console_Loop(pos, info);
			if(info->quit == TRUE) break;
			continue;
        }else if(!strncmp(line, "quit",4))	{
			break;
		}
	}

	free(pos->pvTable->pTable);
	CleanPolyBook();

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
	->
	->
	->
	->

	*/







	/*

        SCARY THINGS
            ->The isItEndGame function in evaluate.c
                >Might not be working
                >Might not actually help

	*/


    return 0;
}
