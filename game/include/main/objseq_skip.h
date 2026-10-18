#ifndef MAIN_OBJSEQ_SKIP_H_
#define MAIN_OBJSEQ_SKIP_H_
#include "main/objseq.h"

static inline void ObjSeq_skipPlayback(ObjSeqState* state)
{
    state->curFrame = state->endFrame;
    state->pendingConditionId = 0;
    state->sequenceControlFlags &= ~OBJSEQ_CONTROL_RESTART_AT_SAVED_FRAME;
}

static inline int ObjSeq_skipComplete(const ObjSeqState* state, int skipping)
{
    return skipping && state->curFrame >= state->endFrame;
}
#endif
