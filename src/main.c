#include "stdio.h"
#include "stdlib.h"
#include "defs.h"
#include "string.h"
#include "pvtable.h"
#include "evaluate.h"
#include "some_maths.h"
#include "bitboards.h"
#include "inttypes.h"
#include "uci.h"
#include "tt_eval.h"
#include "init.h"
#include "board.h"
#include "nnue_loader.h"

int main(int argc, char *argv[])
{

    AllInit();

    S_BOARD pos[1];
    S_SEARCHINFO info[1];
    info->quit=FALSE;
    info->threadNum = 1;

    //init Tables
    pvTable->pTable=NULL;
    InitPvTable(pvTable,defaultHash,0);

	pos->eTable->evalTable=NULL;
	InitEvalTable(pos->eTable,evalHashMB,0);

	pos->pawnKingTable->paTable=NULL;
	InitPawnKingTable(pos->pawnKingTable,pawnHashMB,0);


    //init some stacks and minor tables
	initStacks(pos);
	resetContinuationTable(pos);

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    nnue_init("models/nnue_v1.bin");

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
            TestHASH(QUEENG3);
            continue;
        }else if(!strncmp(line, "quit",4)){
			break;
		}
	}

	free(pvTable->pTable);
	free(pos->eTable->evalTable);
	free(pos->pawnKingTable->paTable);

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

        PLANNING ON GETTING BACK (Jan 8, 2026)
            -I was busy coding other stuff like Catalina LLM, I got bored because of limitation of hardware
            -Thats why Im here. I am planning on coming back.
            -But I can't understand a single thing in the code now. I need to re learn everything.
            -I can't believe I was able to fully have Threads in GOOB, I was only dreaming about it. I don't know how I did that.
            -I am going to commit this version as 2.0.0, difference is it has threading now compared to the older GOOB
	*/


    return 0;

}
