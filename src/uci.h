#ifndef UCI_H
#define UCI_H

//uci.c
extern U64 nodesLimitForUci(int elo);
extern int getInput(char *str);
extern int strEquals(char *str1, char *str2);
extern int strStartsWith(char *str, char *key);
extern int strContains(char *str, char *key);
extern void UCILoop(S_BOARD *pos,S_SEARCHINFO *info);
extern void UciReportCurrentMove(int depth,int move,int currmovenumber);
extern void UciReport(const S_SEARCHINFO *info,S_BOARD *pos,int alpha,int beta,int value,int currentDepth,int pvMoves);
//extern void boundReport(S_PVTABLE *table,const S_SEARCHINFO *info,int seldepth,int alpha,int beta,int value,int depth,int bestMove);

#endif // UCI_H
