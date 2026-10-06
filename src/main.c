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
#include "init.h"
#include "search.h"
#include "syzygy.h"
#include "nnue_loader.h"
#include "cpu.h"
#include "trace.h"

int main(int argc, char *argv[])
{
    init_cpu_features();

    if (argc > 1 && strcmp(argv[1], "compiler") == 0) {
        print_compiler_info();
        return 0;
    }

    AllInit();

    S_BOARD pos[1];
    S_SEARCHINFO info[1];
    //start from zero: fields no command sets explicitly used to be read as
    //stack garbage (e.g. the command line bench ran with random options)
    memset(pos, 0, sizeof(S_BOARD));
    memset(info, 0, sizeof(S_SEARCHINFO));
    pos->search = alloc_search_thread();
    info->quit=FALSE;
    info->threadNum = 1;

    //init Tables
    pvTable->pTable=NULL;
    InitPvTable(pvTable,defaultHash,0);

    //S_SHARED_TABLES declares ALIGN64 members, so the allocation must be
    //64-byte aligned too (plain malloc only guarantees 16-byte alignment)
    size_t sharedSize = (sizeof(S_SHARED_TABLES) + 63) & ~(size_t)63;
    pos->shared = (S_SHARED_TABLES*) goob_aligned_alloc(64, sharedSize);
    if(pos->shared == NULL){
        printf("info string FATAL: shared table allocation failed\n");
        return 1;
    }


    //init some stacks and minor tables
	initStacks(pos);
	resetContinuationTable(pos);
	InitUciDefaults(pos, info);

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    nnue_init(NULL);
    trace_init();

    if (argc > 1 && (strcmp(argv[1], "bench") == 0 || strcmp(argv[1], "trace-bench") == 0)) {
        int depth = 8;
        if (argc > 2) depth = atoi(argv[2]);
        run_bench(pos, info, pvTable, depth);
        if (pvTable->pTable) goob_aligned_free(pvTable->pTable);
        FreeThreadPool();
        if (pos->shared) goob_aligned_free(pos->shared);
        if (pos->search) free_search_thread(pos->search);
        TBFree();
        return 0;
    }

#ifdef DEBUG
    printf("\nWARNING! DEBUG DEFINED MIGHT SLOW DOWN ENGINE\n");
#endif // DEBUG

    printf("\nUCI engine by Gabriel M. [%s]\n", g_cpu_tier_name);
    printf("type 'uci' then 'help' for commands\n\n");


	char line[256];
	while (TRUE) {
		memset(&line[0], 0, sizeof(line));

		fflush(stdout);
		if (!fgets(line, 256, stdin))
			break;
		if (line[0] == '\n')
			continue;
		if (!strncmp(line, "uci",3)) {
			UCILoop(pos, info);
			if(info->quit == TRUE) break;
			continue;
		}else if (!strncmp(line, "compiler", 8)){
			print_compiler_info();
			continue;
		}else if (!strncmp(line, "bb",2)){
            TestHASH(QUEENG3);
            continue;
        }else if(!strncmp(line, "test",4)){
            runTTReplacementTests();
            continue;
        }else if(!strncmp(line, "trace", 5)){
            handle_trace_command(line, pos, info);
            continue;
        }else if(!strncmp(line, "bench", 5)){
            int depth = 8;
            sscanf(line, "bench %d", &depth);
            run_bench(pos, info, pvTable, depth);
            continue;
        }else if(!strncmp(line, "quit",4)){
			break;
		}
	}

	if (pvTable->pTable) goob_aligned_free(pvTable->pTable);
	FreeThreadPool();
    if (pos->shared) goob_aligned_free(pos->shared);
    if (pos->search) free_search_thread(pos->search);
	TBFree();

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