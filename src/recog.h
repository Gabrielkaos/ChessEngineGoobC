#ifndef RECOG_H
#define RECOG_H

extern int recog_draw(const S_BOARD *pos);
extern int drawByMaterial(const S_BOARD *pos);
extern int drawByRepetitionEthereals(const S_BOARD *pos);
extern int drawRepetition(const S_BOARD *pos);
extern int drawFiftyMoveRule(const S_BOARD *pos);

#endif // RECOG_H
