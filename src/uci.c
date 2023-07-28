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

#define INPUTBUFFER 400*6
#define Euler 2.8



U64 nodesLimitForUci(int elo){
    return (U64)pow(Euler,((elo + 200) / 160));
}
int getInput(char *str) {

    char *ptr;

    if (fgets(str, 8192, stdin) == NULL)
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
void UciReport(const S_SEARCHINFO *info,S_BOARD *pos,int alpha,int beta,int value,int currentDepth,int pvMoves){
    int pvNum;

    int elapsed     = getTimeMs()-info->starttime;
    int bounded     = MAX(alpha, MIN(value, beta));


    int score   = bounded >=  ISMATE ?  (INFINITE - bounded + 1) / 2
                : bounded <= -ISMATE ? -(bounded + INFINITE)     / 2 : bounded;
    char *type  = abs(bounded) >= ISMATE ? "mate" : "cp";

    char *bound = bounded >=  beta ? " lowerbound "
                : bounded <= alpha ? " upperbound " : " ";

    printf("info depth %d seldepth %d score %s %d%stime %d nodes %"PRIu64" hashfull %d ",
           currentDepth, pos->seldepth, type, score,bound, elapsed, info->nodes,hashfullTT(pos->pvTable));

    //pv printing
    printf("pv");
    for(pvNum=0;pvNum<pvMoves;++pvNum){
        printf(" %s",PrMove(pos->pvArray[pvNum]));
    }
    printf("\n");
}
void UciSetOption(char *line,S_BOARD *pos,S_SEARCHINFO *info){


    if (!strncmp(line, "setoption name Hash value ", 26)) {
        int MB=defaultHash;
        sscanf(line,"%*s %*s %*s %*s %d",&MB);
        if(MB < 4) MB = 4;
        if(MB > maxHash) MB = maxHash;
        InitPvTable(pos->pvTable, MB,1);
    }

    /*else if (!strncmp(line, "setoption name Book value ", 26)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            EngineOptions->useBook=TRUE;
            printf("info string Book set to true\n");
        }else{
            EngineOptions->useBook=FALSE;
            printf("info string Book set to false\n");
        }
    }*/

    else if (!strncmp(line, "setoption name Clear Hash", 25)) {
        printf("info string Hashtables cleared\n");
        clearPvTable(pos->pvTable);
    }

    /*else if (!strncmp(line, "setoption name Bookfile value ", 30)) {
        char bookfile[500];
        sscanf(line,"%*s %*s %*s %*s %s",bookfile);
        printf("info string Using book %s\n",bookfile);
        CleanPolyBook();
        InitPolyBook(bookfile);
    }*/

    /*else if (!strncmp(line, "setoption name EvalFile value ", 30)) {
        char bookfile[500];
        sscanf(line,"%*s %*s %*s %*s %s",bookfile);
        int loaded = init_nnue(bookfile);
        if (loaded)printf("info string Using NNUE %s\n",bookfile);
        else printf("info string WARNING!, NNUE INVALID, turn off option USE_NNUE\n");
    }*/

    else if (!strncmp(line, "setoption name EvalHash value ", 30)) {
        int EvalMb=evalHashMB;
        sscanf(line,"%*s %*s %*s %*s %d",&EvalMb);
        if(EvalMb < 4) EvalMb = 4;
        if(EvalMb > maxHash) EvalMb = maxHash;
        InitEvalTable(pos->eTable, EvalMb,1);
    }

    else if (!strncmp(line, "setoption name PawnHash value ", 30)) {
        int pawnMb=pawnHashMB;
        sscanf(line,"%*s %*s %*s %*s %d",&pawnMb);
        if(pawnMb < 4) pawnMb = 4;
        if(pawnMb > maxHash) pawnMb = maxHash;
        InitPawnKingTable(pos->pawnKingTable, pawnMb,1);
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

    /*else if (!strncmp(line, "setoption name useRazoring value ", 33)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            info->useRazoring=TRUE;
            printf("info string useRazoring set to true\n");
        }else{
            info->useRazoring=FALSE;
            printf("info string useRazoring set to false\n");
        }
    }*/

    /*else if (!strncmp(line, "setoption name UseEGNN value ", 29)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            pos->useEGNN=TRUE;
            printf("info string Using EGNN\n");
            clearEvalTable(pos->eTable);
        }else{
            pos->useEGNN=FALSE;
            printf("info string EGNN disabled\n");
            clearEvalTable(pos->eTable);
        }
    }

    else if (!strncmp(line, "setoption name UsePKNN value ", 29)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            pos->usePKNN=TRUE;
            printf("info string Using PKNN\n");
            clearEvalTable(pos->eTable);
        }else{
            pos->usePKNN=FALSE;
            printf("info string PKNN disabled\n");
            clearEvalTable(pos->eTable);
        }
    }*/

    else if (!strncmp(line, "setoption name ContemptDrawPenalty value ", 41)) {
        int contemptDraw=0;
        sscanf(line,"%*s %*s %*s %*s %d",&contemptDraw);
        if(contemptDraw < -300) contemptDraw = -300;
        if(contemptDraw > 300) contemptDraw = 300;
        pos->contemptDrawPenalty=contemptDraw;
        clearEvalTable(pos->eTable);
        printf("info string ContemptDrawPenalty set to %d\n",contemptDraw);
        //printf("info string contempts only works in Classical evaluation\n");
    }

    else if (!strncmp(line, "setoption name ContemptComplexity value ", 40)) {
        int contemptDraw=0;
        sscanf(line,"%*s %*s %*s %*s %d",&contemptDraw);
        if(contemptDraw < -300) contemptDraw = -300;
        if(contemptDraw > 300) contemptDraw = 300;
        pos->contemptComplexity=contemptDraw;
        clearEvalTable(pos->eTable);
        printf("info string ContemptComplexity set to %d\n",contemptDraw);
        //printf("info string contempts only works in Classical evaluation\n");
    }

    /*else if (!strncmp(line, "setoption name Clear Eval", 25)) {
        printf("info string Eval hashtables cleared\n");
        clearEvalTable(pos->eTable);
    }*/

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
            clearPvTable(pos->pvTable);
            printf("info string Set BruteForceMode to true\n");
        }else{
            info->bruteForceMode=FALSE;
            clearPvTable(pos->pvTable);
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

    /*else if (!strncmp(line, "setoption name USE_NNUE value ", 30)) {
        char *ptrTrue=NULL;
        ptrTrue=strstr(line,"true");
        if(ptrTrue != NULL){
            pos->USE_NNUE=TRUE;
            printf("info string USE_NNUE set to true\n");
            clearEvalTable(pos->eTable);
        }else{
            pos->USE_NNUE=FALSE;
            printf("info string USE_NNUE set to false\n");
            clearEvalTable(pos->eTable);
        }
    }*/
}
void parseGo(char* line,S_SEARCHINFO *info,S_BOARD *pos){

    info->timeSet     =FALSE;
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
    int movestogo   =30;

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
        time           /=movestogo;
        time           -=50;
        info->timeSet  =TRUE;
        info->stoptime =info->starttime+time+inc;
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
    SearchPosition(pos,info);
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
            ptrChar+=4;
            ParseFEN(ptrChar,pos);
        }
    }
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
    printf("option name Hash type spin default %d min 4 max %d\n",defaultHash,maxHash); //1
    printf("option name EvalHash type spin default %d min 4 max %d\n",evalHashMB,maxHash); //3
    printf("option name PawnHash type spin default %d min 4 max %d\n",pawnHashMB,maxHash); //4
    printf("option name ContemptDrawPenalty type spin default 0 min -300 max 300\n"); //5
    printf("option name ContemptComplexity type spin default 0 min -300 max 300\n"); //6
    printf("option name Clear Hash type button\n"); //12
    printf("option name Ponder type check default false\n"); //10
    printf("option name UCI_AnalyseMode type check default false\n"); //15
    printf("option name UCI_LimitStrength type check default false\n"); //16
    printf("option name UCI_Elo type spin default %d min 1200 max %d\n",defaultElo,defaultElo); //17
    printf("option name UCI_Chess960 type check default false\n"); //18
    printf("option name BruteForceMode type check default false\n"); //19
    printf("option name useFiftyMoveRule type check default true\n"); //20
    printf("uciok\n");
}


void UCILoop(S_BOARD *pos,S_SEARCHINFO *info){
    pos->useFiftyMoveRule        =TRUE;
    pos->contemptComplexity      =0;
    pos->contemptDrawPenalty     =0;
    pos->contempt                =0;
    pos->chess960                =FALSE;
    EngineOptions->analysisMode  =FALSE;
	EngineOptions->uciElo        =defaultElo;
	info->setOptionPonder        =FALSE;
	info->nodeSet                =FALSE;
	info->bruteForceMode         =FALSE;

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
            clearPvTable(pos->pvTable);
            resetContinuationTable(pos);
        }

        else if (strStartsWith(str, "setoption")) {
            UciSetOption(str,pos,info);
            fflush(stdout);
        }

        else if (strStartsWith(str, "position")) {
            parsePosition(str,pos);
        }

        else if (strStartsWith(str, "go")) {
            parseGo(str,info,pos);
            fflush(stdout);
        }

        else if (strEquals(str, "quit")){
            info->quit = TRUE;
            break;
        }

        else if(strEquals(str,"print")){
            PrintBoard(pos);
            fflush(stdout);
        }

        else if(strEquals(str,"evaluate")){
            printf("Eval:%d\n",EvalPosition(pos));
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

		else if(strEquals(str, "help")) {
            printf("commands:\n");
            printf("-uci\n");
            printf("-ucinewgame\n");
            printf("-isready\n");
            printf("-setoption\n");
            printf("-position\n");
            printf("-go\n");
            printf("-quit\n");
            printf("-print\n");
            printf("-evaluate\n");
            printf("-perft\n");
            fflush(stdout);
		}

        else{
            printf("Unknown command: %s\n",str);
            fflush(stdout);
        }

        if(info->quit)break;
    }
}
