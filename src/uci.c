#include "defs.h"
#include "stdio.h"
#include "string.h"

#define INPUTBUFFER 400*6

void parseGo(char* line,S_SEARCHINFO *info,S_BOARD *pos){

    int movestogo=30,depth=-1,movetime=-1;
    int time=-1,inc=0;
    char *ptr=NULL;
    info->timeset=FALSE;

    if((ptr=strstr(line,"infinite"))){
        ;
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

    if(movetime != -1){
        time=movetime;
        movestogo=1;
    }

    info->starttime=getTimeMs();
    info->depth=depth;

    if(time != -1){
        info->timeset=TRUE;
        time /= movestogo;
        time-=50;
        info->stoptime=info->starttime+inc+time;
    }

    if(info->depth ==-1){
        info->depth=MAXDEPTH;
    }


    printf("time:%d start:%d stop:%d depth:%d timeset:%d\n",
           time,info->starttime,info->stoptime,info->depth,info->timeset);

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

void UCI_loop(S_BOARD *pos,S_SEARCHINFO *info){

    setbuf(stdin,NULL);
    setbuf(stdout,NULL);

    int max_hash=2048;
    EngineOptions->useBook=FALSE;
    ParseFEN(START_FEN, pos);

    char line[INPUTBUFFER];
    printf("id name %s %s\n",NAME,VER);
    printf("id author Gabriel\n");
    printf("option name Hash type spin default 64 min 4 max %d\n",max_hash);
    //printf("option name Book type check default false\n");
    //printf("option name Clear Hash type button\n");
    printf("uciok\n");

    /*S_BOARD *pos=GenBoard();
    S_SEARCHINFO info[1];
    InitPvTable(pos->pvTable);*/

    int MB=64;

    while(TRUE){

        memset(&line[0],0,sizeof(line));
        fflush(stdout);

        if(!(fgets(line,INPUTBUFFER,stdin)))
        continue;

        if(line[0]=='\n')
        continue;

        if(!strncmp(line,"isready",7)){
            printf("readyok\n");
            continue;
        }else if(!strncmp(line,"position",8)){
            parsePosition(line,pos);
        }else if(!strncmp(line,"d",1)){
            PrintBoard(pos);
            continue;
        }else if(!strncmp(line,"ucinewgame",10)){
            parsePosition("position startpos\n",pos);
            clearPvTable(pos->pvTable);
        }else if(!strncmp(line,"go",2)){
            parseGo(line,info,pos);
        }else if(!strncmp(line,"quit",4)){
            info->quit=TRUE;
            break;
        }else if(!strncmp(line,"uci",3)){
            printf("id name %s %s\n",NAME,VER);
            printf("id author Gabriel\n");
            //printf("option name Hash type spin default 64 min 4 max %d\n",max_hash);
            //printf("option name Book type check default false\n");
            //printf("option name Clear Hash type button\n");
            printf("uciok\n");
        }else if (!strncmp(line, "setoption name Hash value ", 26)) {
			sscanf(line,"%*s %*s %*s %*s %d",&MB);
			if(MB < 4) MB = 4;
			if(MB > max_hash) MB = max_hash;
			printf("Set Hash to %d MB\n",MB);
			InitPvTable(pos->pvTable, MB);
		}/*else if (!strncmp(line, "setoption name Book value ", 26)) {
			char *ptrTrue=NULL;
			ptrTrue=strstr(line,"true");
			if(ptrTrue != NULL){
                EngineOptions->useBook=TRUE;
			}else{
                EngineOptions->useBook=FALSE;
			}
		}*/
        if(info->quit) break;
    }
    //free(pos->pvTable->pTable);
}
