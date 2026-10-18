#include "main/objseq_skip.h"
#include <assert.h>

int main(void)
{
    ObjSeqState state = {0};
    state.curFrame = 15;
    state.prevFrame = 14;
    state.endFrame = 400;
    state.pendingConditionId = 9;
    state.sequenceControlFlags = OBJSEQ_CONTROL_RESTART_AT_SAVED_FRAME | OBJSEQ_CONTROL_SET_LATCH_A;
    state.cmdCursor = 3;
    state.cmdCount = 80;
    ObjSeq_skipPlayback(&state);
    assert(state.curFrame == 400 && state.pendingConditionId == 0);
    assert(state.prevFrame == 14 && state.cmdCursor == 3 && state.cmdCount == 80);
    assert(!(state.sequenceControlFlags & OBJSEQ_CONTROL_RESTART_AT_SAVED_FRAME));
    assert(state.sequenceControlFlags & OBJSEQ_CONTROL_SET_LATCH_A);
    assert(ObjSeq_skipComplete(&state, 1));
    assert(!ObjSeq_skipComplete(&state, 0));
    state.curFrame = 399;
    assert(!ObjSeq_skipComplete(&state, 1));
    state.curFrame = 401;
    assert(ObjSeq_skipComplete(&state, 1));
    state.endFrame = 32767;
    ObjSeq_skipPlayback(&state);
    assert(state.curFrame == 32767 && ObjSeq_skipComplete(&state, 1));
}
