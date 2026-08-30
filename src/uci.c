#include "defs.h"
#include "stdio.h"
#include "string.h"
#include "math.h"
#include "search.h"
#include "evaluate.h"
#include "stdlib.h"
#include "uci.h"
#include "tt_eval.h"
#include "inttypes.h"
#include "some_maths.h"
#include "pvtable.h"
#include "perft.h"
#include "misc.h"
#include "makemove.h"
#include "io.h"
#include "tinycthread.h"
#include "thread.h"
#include "nnue_loader.h"
#include "pknet_loader.h"
#include "syzygy.h"
#include "correction.h"

#define INPUTBUFFER 400*6
#define Euler 2.8

thrd_t mainSearchThread;

thrd_t LaunchSearchThread(S_BOARD *pos, S_SEARCHINFO *info, S_PVTABLE *table){
    THREAD_DATA *thread_data = malloc(sizeof(THREAD_DATA));

    thread_data->info=info;
    thread_data->originalPos=pos;
    thread_data->ttable=table;

    thrd_t th;
    thrd_create(&th,&SearchPositionThread,(void*)thread_data);
    return th;
}

void joinSearchThread(S_SEARCHINFO *info){
    info->stopped=TRUE;
    thrd_join(mainSearchThread,NULL);
}

U64 nodesLimitForUci(int elo){
    return (U64)pow(Euler,((elo + 200) / 160));
}
int getInput(char *str) {

    char *ptr;

    if (fgets(str, INPUTBUFFER, stdin) == NULL)
        return 0;

    ptr = strchr(str, '\n');
    if (ptr != NULL) *ptr = '\0';

    ptr = strchr(str, '\r');
    if (ptr != NULL) *ptr = '\0';

    return 1;
}
int strEquals(char *str1, char *str2) {
    return strcmp(str1, str2) == 0;
}
int strStartsWith(char *str, char *key) {
    return strstr(str, key) == str;
}
int strContains(char *str, char *key) {
    return strstr(str, key) != NULL;
}


void UciReportCurrentMove(int depth,int move,int currmovenumber){
    printf("info depth %d currmove %s currmovenumber %d\n",depth,
                     PrMove(move),
                     currmovenumber);
}
void UciReport(const S_SEARCHINFO *info, S_PVTABLE *table,S_BOARD *pos,int alpha,int beta,int value,int currentDepth,int pvMoves,int multiPvNum){
    int pvNum;

    int elapsed     = getTimeMs()-info->starttime;
    int bounded     = MAX(alpha, MIN(value, beta));


    int score   = bounded >=  ISMATE ?  (AB_BOUND - bounded + 1) / 2
                : bounded <= -ISMATE ? -(bounded + AB_BOUND)     / 2 : bounded;
    char *type  = abs(bounded) >= ISMATE ? "mate" : "cp";

    char *bound = bounded >=  beta ? " lowerbound "
                : bounded <= alpha ? " upperbound " : " ";

    printf("info depth %d seldepth %d multipv %d score %s %d%stime %d nodes %"PRIu64" hashfull %d tbhits %"PRIu64" ",
           currentDepth, pos->seldepth, multiPvNum, type, score,bound, elapsed, info->nodes,hashfullTT(table),info->tbhits);

    //pv printing
    printf("pv");
    for(pvNum=0;pvNum<pvMoves;++pvNum){
        printf(" %s",PrMove(pos->search->pvArray[pvNum]));
    }
    printf("\n");
}
void UciSetOption(char *line,S_BOARD *pos,S_SEARCHINFO *info){


    if (!strncmp(line, "setoption name Hash value ", 26)) {
        int MB=defaultHash;
        sscanf(line,"%*s %*s %*s %*s %d",&MB);
        if(MB < 4) MB = 4;
        if(MB > maxHash) MB = maxHash;
        InitPvTable(pvTable, MB,1);
    }
    else if (!strncmp(line, "setoption name Move Overhead value ", 35)) {
        int mo = 50;
        sscanf(line,"%*s %*s %*s %*s %d",&mo);
        if(mo < 0) mo = 0;
        if(mo > 5000) mo = 5000;
        info->moveOverhead = mo;
        printf("info string Move Overhead set to %d\n", mo);
    }


    else if (!strncmp(line, "setoption name UseNNUE value ", 29)) {
        char *ptrTrue = strstr(line, "true");
        if (ptrTrue != NULL) {
            pos->useNNUE = TRUE;
            printf("info string UseNNUE set to true\n");
        } else {
            pos->useNNUE = FALSE;
            printf("info string UseNNUE set to false\n");
        }
        clearEvalTable(pos->eTable);
    }

    else if (!strncmp(line, "setoption name EvalFile value ", 30)) {
        char path[512] = {0};
        sscanf(line, "%*s %*s %*s %*s %511s", path);
        if (strlen(path) > 0 && strcmp(path, "<empty>") != 0) {
            if (nnue_init(path)) {
                nnue_refresh_accumulator(pos);
                printf("info string EvalFile loaded: %s\n", path);
            } else {
                printf("info string EvalFile FAILED to load: %s\n", path);
            }
        }
        clearEvalTable(pos->eTable);
    }


    else if (!strncmp(line, "setoption name MultiPV value ", 29)) {
        int mpv=1;
        sscanf(line,"%*s %*s %*s %*s %d",&mpv);
        if(mpv < 1) mpv = 1;
        if(mpv > MAXPOSMOVES) mpv = MAXPOSMOVES;
        info->multiPV = mpv;
        printf("info string MultiPV set to %d\n",info->multiPV);
    }

    else if (!strncmp(line, "setoption name Threads value ", 29)) {
        int thr_num=1;
        sscanf(line,"%*s %*s %*s %*s %d",&thr_num);
        if(thr_num < 1) thr_num = 1;
        if(thr_num > MAXTHREADS) thr_num = MAXTHREADS;
        info->threadNum = thr_num;
        printf("info string Threads set to %d\n",info->threadNum);
    }

    else if (!strncmp(line, "setoption name Clear Hash", 25)) {
        printf("info string Hashtables cleared\n");
        clearPvTable(pvTable);
        clearEvalTable(pos->eTable);
        clearPawnKingTable(pos->pawnKingTable);
        ClearThreadTables(info->threadNum);
        clearCorrectionHistory(pos);
        pos->shared->ttMoveHistory = 0;
    }

    else if (!strncmp(line, "setoption name EvalHash value ", 30)) {
        int EvalMb=evalHashMB;
        sscanf(line,"%*s %*s %*s %*s %d",&EvalMb);
        if(EvalMb < 4) EvalMb = 4;
        if(EvalMb > maxHash) EvalMb = maxHash;
        InitEvalTable(pos->eTable, EvalMb,1);
        ReallocThreadTables(currentPawnHashMB, EvalMb);
    }

    else if (!strncmp(line, "setoption name PawnHash value ", 30)) {
        int pawnMb=pawnHashMB;
        sscanf(line,"%*s %*s %*s %*s %d",&pawnMb);
        if(pawnMb < 4) pawnMb = 4;
        if(pawnMb > maxHash) pawnMb = maxHash;
        InitPawnKingTable(pos->pawnKingTable, pawnMb,1);
        ReallocThreadTables(pawnMb, currentEvalHashMB);
    }

    else if (!strncmp(line, "setoption name Ponder value ", 28)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            info->setOptionPonder=TRUE;
            printf("info string Ponder set to true\n");
        }else{
            info->setOptionPonder=FALSE;
            printf("info string Ponder set to false\n");
        }
    }

    else if (!strncmp(line, "setoption name UCI_AnalyseMode value ", 37)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            EngineOptions->analysisMode=TRUE;
            printf("info string AnalysisMode set to true\n");
        }else{
            EngineOptions->analysisMode=FALSE;
            printf("info string AnalysisMode set to false\n");
        }
    }

    else if (!strncmp(line, "setoption name UCI_LimitStrength value ", 39)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            info->EloNodeSet=TRUE;
            printf("info string LimitStrength set to true\n");
        }else{
            info->EloNodeSet=FALSE;
            printf("info string LimitStrength set to false\n");
        }
    }

    else if (!strncmp(line, "setoption name UCI_Elo value ", 29)) {
        int uciElo=defaultElo;
        sscanf(line,"%*s %*s %*s %*s %d",&uciElo);
        if(uciElo < 1200) uciElo = 1200;
        if(uciElo > defaultElo) uciElo = defaultElo;
        EngineOptions->uciElo=uciElo;
        printf("info string Elo set to %d\n",uciElo);
    }

    else if (!strncmp(line, "setoption name ContemptDrawPenalty value ", 41)) {
        int contemptDraw=0;
        sscanf(line,"%*s %*s %*s %*s %d",&contemptDraw);
        if(contemptDraw < -300) contemptDraw = -300;
        if(contemptDraw > 300) contemptDraw = 300;
        pos->contemptDrawPenalty=contemptDraw;
        clearEvalTable(pos->eTable);
        printf("info string ContemptDrawPenalty set to %d\n",contemptDraw);
    }

    else if (!strncmp(line, "setoption name ContemptComplexity value ", 40)) {
        int contemptDraw=0;
        sscanf(line,"%*s %*s %*s %*s %d",&contemptDraw);
        if(contemptDraw < -300) contemptDraw = -300;
        if(contemptDraw > 300) contemptDraw = 300;
        pos->contemptComplexity=contemptDraw;
        clearEvalTable(pos->eTable);
        printf("info string ContemptComplexity set to %d\n",contemptDraw);
    }

    else if (!strncmp(line, "setoption name UCI_Chess960 value ", 34)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            pos->chess960=TRUE;
            printf("info string Set UCI_Chess960 to true\n");
        }else{
            pos->chess960=FALSE;
            printf("info string Set UCI_Chess960 to false\n");
            clearEvalTable(pos->eTable);
        }
    }

    else if (!strncmp(line, "setoption name BruteForceMode value ", 36)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            info->bruteForceMode=TRUE;
            clearPvTable(pvTable);
            printf("info string Set BruteForceMode to true\n");
        }else{
            info->bruteForceMode=FALSE;
            clearPvTable(pvTable);
            printf("info string Set BruteForceMode to false\n");
        }
    }

    else if (!strncmp(line, "setoption name useFiftyMoveRule value ", 38)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            pos->useFiftyMoveRule=TRUE;
            printf("info string  Set useFiftyMoveRule to true\n");
        }else{
            pos->useFiftyMoveRule=FALSE;
            printf("info string Set useFiftyMoveRule to false\n");
        }
    }

    else if (!strncmp(line, "setoption name UsePKNet value ", 30)) {
        pos->usePKNet = strstr(line,"true") ? TRUE : FALSE;
        printf("info string UsePKNet set to %s\n", pos->usePKNet?"true":"false");
        clearEvalTable(pos->eTable);
     }

     else if (!strncmp(line, "setoption name PKNetFile value ", 31)) {
        char path[512]={0};
        sscanf(line, "%*s %*s %*s %*s %511s", path);
        if (strlen(path)>0 && strcmp(path,"<empty>")!=0) {
            pknet_init(path);
        }
        clearEvalTable(pos->eTable);
     }

    else if (!strncmp(line, "setoption name SyzygyPath value ", 32)) {
        //paths can be a ';'(Windows)/':'(Unix) separated list of
        //directories and may legitimately contain spaces, so read the
        //rest of the line verbatim rather than stopping at whitespace
        //the way EvalFile/PKNetFile do above.
        char path[1024] = {0};
        sscanf(line, "%*s %*s %*s %*s %1023[^\n]", path);
        TBInit(path);
    }

    else if (!strncmp(line, "setoption name SyzygyProbeDepth value ", 38)) {
        int d = 1;
        sscanf(line,"%*s %*s %*s %*s %d",&d);
        if(d < 0)   d = 0;
        if(d > 100) d = 100;
        SyzygyProbeDepth = d;
        printf("info string SyzygyProbeDepth set to %d\n",SyzygyProbeDepth);
    }

    else if (!strncmp(line, "setoption name Syzygy50MoveRule value ", 38)) {
        char *ptrTrue = strstr(line,"true");
        Syzygy50MoveRule = (ptrTrue != NULL);
        printf("info string Syzygy50MoveRule set to %s\n",Syzygy50MoveRule?"true":"false");
    }

    else if (!strncmp(line, "setoption name SyzygyProbeLimit value ", 38)) {
        int lim = 7;
        sscanf(line,"%*s %*s %*s %*s %d",&lim);
        if(lim < 0) lim = 0;
        if(lim > 7) lim = 7;
        if(SyzygyEnabled && lim > TBLargestMen) lim = TBLargestMen;
        SyzygyProbeLimit = lim;
        printf("info string SyzygyProbeLimit set to %d\n",SyzygyProbeLimit);
    }

}
void parseGo(char* line,S_SEARCHINFO *info,S_BOARD *pos, S_PVTABLE *table){

    info->timeSet     =FALSE;
    info->softTimeSet = FALSE;
    info->analyzeMode =EngineOptions->analysisMode;
    info->UciInfinite =FALSE;
    info->mateLimit   =-1;

    int depth       =-1;
    int movetime    =-1;
    int time        =-1;
    int inc         =0;
    U64 nodestogo   =0;
    char *ptr       =NULL;
    int ponder      =FALSE;
    int movestogo   =0;

    if((ptr=strstr(line,"infinite"))){
        info->UciInfinite=TRUE;
    }
    if((ptr=strstr(line,"ponder"))){
        ponder=TRUE;
    }
    if((ptr=strstr(line,"binc")) && pos->side==BLACK){
        inc=atoi(ptr+5);
    }
    if((ptr=strstr(line,"winc")) && pos->side==WHITE){
        inc=atoi(ptr+5);
    }
    if((ptr=strstr(line,"btime")) && pos->side==BLACK){
        time=atoi(ptr+6);
    }
    if((ptr=strstr(line,"wtime")) && pos->side==WHITE){
        time=atoi(ptr+6);
    }
    if((ptr=strstr(line,"movestogo"))){
        movestogo=atoi(ptr+10);
    }
    if((ptr=strstr(line,"movetime"))){
        movetime=atoi(ptr+9);
    }
    if((ptr=strstr(line,"depth"))){
        depth=atoi(ptr+6);
    }
    if((ptr=strstr(line,"nodes"))){
        nodestogo=(U64)atoi(ptr+6);
    }
    if((ptr=strstr(line,"mate"))){
        info->mateLimit=atoi(ptr+5);
    }

    //init time limits
    if(movetime != -1){
        time           =movetime;
        movestogo      =1;
    }
    info->starttime=getTimeMs();
    if(time != -1){
        if (movetime != -1) {
            info->timeSet  =TRUE;
            info->stoptime =info->starttime + time;
            info->maximumTime =info->stoptime;
            info->softTimeSet = FALSE;
        } else {
            int moveOverhead = info->moveOverhead;           
            
            // maximum move horizon (mtg), capped like SF's centiMTG (max 50 moves)
            int centiMTG = (movestogo > 0) ? MIN(movestogo * 100, 5000) : 5051;
            if (time < 1000) {
                centiMTG = (int)(time * 5.051);
            }
            if (centiMTG < 1) centiMTG = 1;

            int timeLeft = time + (inc * (centiMTG - 100) - moveOverhead * (200 + centiMTG)) / 100;
            if (timeLeft < 1) timeLeft = 1;
            
            double optScale, maxScale;
            int ply = pos->hisPly;
            
            if (movestogo == 0) {
                double timeForLog = time > 1 ? (double)time : 1.0;
                double logTimeInSec = log10(timeForLog / 1000.0);
                if (logTimeInSec < -3.0) logTimeInSec = -3.0; // avoid infinity if time is tiny
                
                double optConstant = 0.0032116 + 0.000321123 * logTimeInSec;
                if (optConstant > 0.00508017) optConstant = 0.00508017;
                
                double maxConstant = 3.3977 + 3.03950 * logTimeInSec;
                if (maxConstant < 2.94761) maxConstant = 2.94761;
                
                if (info->originalTimeAdjust < 0.0) {
                    info->originalTimeAdjust = 0.3128 * log10((double)timeLeft) - 0.4354;
                }
                
                double term1 = 0.0121431 + pow(ply + 2.94693, 0.461073) * optConstant;
                double term2 = 0.213035 * time / timeLeft;
                optScale = (term1 < term2 ? term1 : term2) * info->originalTimeAdjust;
                if (optScale < 0.0) optScale = 0.0;
                
                maxScale = 6.67704;
                double maxScaleCand = maxConstant + ply / 11.9847;
                if (maxScaleCand < maxScale) maxScale = maxScaleCand;
            } else {
                double term1 = (0.88 + ply / 116.4) / (centiMTG / 100.0);
                double term2 = 0.88 * time / timeLeft;
                optScale = (term1 < term2 ? term1 : term2);
                maxScale = 1.3 + 0.11 * (centiMTG / 100.0);
            }
            
            int optimumTime = (int)(optScale * timeLeft);
            if (optimumTime < 1) optimumTime = 1;
            
            int maximumTimeFromOpt = (int)(maxScale * optimumTime);
            int maximumTimeFromRemaining = (int)(0.825179 * time - moveOverhead);
            
            int max_tmp = (maximumTimeFromOpt < maximumTimeFromRemaining) ? maximumTimeFromOpt : maximumTimeFromRemaining;
            max_tmp -= 10;                              
            if (max_tmp < optimumTime) max_tmp = optimumTime;
            int maximumTime = max_tmp;
            
            info->timeSet  =TRUE;
            info->stoptime =info->starttime + maximumTime;
            info->maximumTime = info->stoptime;
            
            if (info->setOptionPonder) optimumTime += optimumTime / 4;   
            
            info->softTimeSet = TRUE;
            info->optimumTime = info->starttime + optimumTime;
        }
    }

    //limits for limiting strength
    if(info->EloNodeSet==TRUE)info->EloNodelimit=nodesLimitForUci(EngineOptions->uciElo);
    else info->EloNodelimit=0;

    //node limits
    info->nodeSet   = nodestogo > 0;
    info->nodeLimit = nodestogo;

    //depth limits
    info->depth         = depth > 0 ? depth:MAXDEPTH;
    info->depthSet      = depth > 0;

    //init things
    info->ponder        = ponder;
    int contempt        = MakeScore(pos->contemptDrawPenalty + pos->contemptComplexity, pos->contemptDrawPenalty);
    pos->contempt       = pos->side==WHITE ? contempt:-contempt;

    /*if (pos->USE_NNUE){
        printf("info string Using NNUE evaluation\n");
    }*/
    mainSearchThread = LaunchSearchThread(pos, info, table);
}
void parsePosition(char* lineIn,S_BOARD *pos){
    lineIn+=9;
    char *ptrChar=lineIn;
    if(strncmp(lineIn,"startpos",8)==0){
        ParseFEN(START_FEN,pos);
    }else{
        ptrChar=strstr(lineIn,"fen");
        if(ptrChar==NULL){
            ParseFEN(START_FEN,pos);
        }else{
            ptrChar+=3;
            while(*ptrChar==' ') ptrChar++;
            if(ParseFEN(ptrChar,pos)!=0){
                //never search from a half-parsed position - reset to startpos
                printf("info string invalid FEN, using startpos instead\n");
                fflush(stdout);
                ParseFEN(START_FEN,pos);
            }
        }
    }

    //hisPly and the per-ply search stacks must be reset for every new position.
    //makeMove() increments pos->hisPly (capped only by MAXGAMESMOVES), so without
    //this reset it grows without bound across all games in one process and writes
    //past pos->search->history[], corrupting board state and producing illegal moves.
    pos->hisPly = 0;
    initStacks(pos);

    ptrChar=strstr(lineIn,"moves");
    int move;
    if(ptrChar != NULL){
        ptrChar+=6;
        while(*ptrChar){
            move=ParseMove(ptrChar,pos);
            if(move==NOMOVE) break;
            makeMove(pos,move);
            pos->ply=0;
            while(*ptrChar && *ptrChar != ' ')ptrChar++;
            ptrChar++;
        }
    }
}
void uciPrint(){
    printf("id name %s %s\n",NAME,VER);
    printf("id author %s\n",AUTHOR);
    printf("option name Threads type spin default 1 min 1 max %d\n",MAXTHREADS); //1
    printf("option name MultiPV type spin default 1 min 1 max %d\n",MAXPOSMOVES); //1b
    printf("option name Hash type spin default %d min 4 max %d\n",defaultHash,maxHash); //1
    printf("option name EvalHash type spin default %d min 4 max %d\n",evalHashMB,maxHash); //3
    printf("option name PawnHash type spin default %d min 4 max %d\n",pawnHashMB,maxHash); //4
    printf("option name ContemptDrawPenalty type spin default 0 min -300 max 300\n"); //5
    printf("option name ContemptComplexity type spin default 0 min -300 max 300\n"); //6
    printf("option name Move Overhead type spin default 50 min 0 max 5000\n");
    printf("option name Clear Hash type button\n"); //12
    printf("option name Ponder type check default false\n"); //10
    printf("option name UCI_AnalyseMode type check default false\n"); //15
    printf("option name UCI_LimitStrength type check default false\n"); //16
    printf("option name UCI_Elo type spin default %d min 1200 max %d\n",defaultElo,defaultElo); //17
    printf("option name UCI_Chess960 type check default false\n"); //18
    printf("option name BruteForceMode type check default false\n"); //19
    printf("option name useFiftyMoveRule type check default true\n"); //20
    printf("option name UseNNUE type check default false\n");
    printf("option name EvalFile type string default <empty>\n");
    printf("option name UsePKNet type check default false\n");
    printf("option name PKNetFile type string default <empty>\n");
    printf("option name SyzygyPath type string default <empty>\n");
    printf("option name SyzygyProbeDepth type spin default 1 min 0 max 100\n");
    printf("option name Syzygy50MoveRule type check default true\n");
    printf("option name SyzygyProbeLimit type spin default 7 min 0 max 7\n");
    printf("uciok\n");
}


void UCILoop(S_BOARD *pos,S_SEARCHINFO *info){
    pos->useFiftyMoveRule        =TRUE;
    pos->contemptComplexity      =0;
    pos->contemptDrawPenalty     =0;
    pos->contempt                =0;
    pos->chess960                =FALSE;
    pos->useNNUE                 =FALSE;
    EngineOptions->analysisMode  =FALSE;
	EngineOptions->uciElo        =defaultElo;
	info->setOptionPonder        =FALSE;
	info->nodeSet                =FALSE;
	info->bruteForceMode         =FALSE;
	info->multiPV                =1;
	info->originalTimeAdjust     =-1.0;
    info->moveOverhead = 50;
    info->previousTimeReduction = 1.0;   // SF starts this at 1
    info->bestPreviousScore = INFINITE_BOUND; // SF starts this effectively "infinite" so first move isn't treated as a falling eval
    pos->usePKNet                =FALSE;
    SyzygyProbeDepth             =1;
    Syzygy50MoveRule             =TRUE;

    ParseFEN(START_FEN, pos);

    char str[INPUTBUFFER];

    uciPrint();
    fflush(stdout);

    while(getInput(str)){
        if(strEquals(str,"uci")){
            uciPrint();
            fflush(stdout);
        }

        else if(strEquals(str,"isready")){
            printf("readyok\n");
            fflush(stdout);
        }

        else if(strEquals(str,"ucinewgame")){
            parsePosition("position startpos\n",pos);
            clearPvTable(pvTable);
            clearEvalTable(pos->eTable);
            clearPawnKingTable(pos->pawnKingTable);
            ClearThreadTables(info->threadNum);
            resetContinuationTable(pos);
            clearCorrectionHistory(pos);
            info->originalTimeAdjust = -1.0;
        }

        else if (strStartsWith(str, "setoption")) {
            UciSetOption(str,pos,info);
            fflush(stdout);
        }

        else if (strStartsWith(str, "position")) {
            parsePosition(str,pos);
        }

        else if (strStartsWith(str, "go")) {
            parseGo(str,info,pos, pvTable);
            fflush(stdout);
        }

        else if (strEquals(str, "quit")){
            info->quit = TRUE;
            joinSearchThread(info);
            break;
        }

        else if (strEquals(str, "stop")){
            info->ponder=FALSE;
            joinSearchThread(info);
        }

        else if (strEquals(str, "ponderhit")){
            info->ponder = FALSE;
            joinSearchThread(info);
        }

        else if(strEquals(str,"print")){
            PrintBoard(pos);
            fflush(stdout);
        }

        else if(strEquals(str,"evaluate")){
            PrintBoard(pos);
            printf("Eval:%d\n",EvalPosition(pos));
            MirrorBoard(pos);
            PrintBoard(pos);
            printf("Eval Mirrored:%d\n",EvalPosition(pos));
            MirrorBoard(pos);
            fflush(stdout);
        }

        else if (strEquals(str, "perfttest")) {
            PerftSuiteTest(pos);
            fflush(stdout);
        }

        else if(strStartsWith(str, "perft")) {
            int perft=0;
			sscanf(str, "perft %d", &perft);
			if(perft<0)perft=MAXDEPTH;
			if(perft>MAXDEPTH)perft=MAXDEPTH;
			PerftTest(perft,pos);
            fflush(stdout);
		}

		else if(strStartsWith(str, "uperft")) {
            int perft=0;
			sscanf(str, "uperft %d", &perft);
			if(perft<0)perft=MAXDEPTH;
			if(perft>MAXDEPTH)perft=MAXDEPTH;
			BenchTest(perft,pos);
            fflush(stdout);
		}

		else if(strEquals(str, "help")) {
            printf("commands:\n");
            printf("-uci\n");
            printf("-ucinewgame\n");
            printf("-isready\n");
            printf("-setoption\n");
            printf("-position\n");
            printf("-go\n");
            printf("-quit\n");
            printf("-stop\n");
            printf("-print\n");
            printf("-evaluate\n");
            printf("-perft(useful for debugging) x\n");
            printf("-uperft(faster) x\n");
            printf("-perfttest(test on positions)\n");
            fflush(stdout);
		}

        else{
            printf("Unknown command: %s\n",str);
            fflush(stdout);
        }

        if(info->quit)break;
    }
}