#ifndef ENGINE_H
#define ENGINE_H

#ifndef DOSBOX_DOSBOX_H
#include "dosbox.h" 
#endif
#ifndef DOSBOX_REGS_H
#include "regs.h"
#endif
#ifndef DOSBOX_MEM_H
#include "mem.h"
#endif

int engine_call(bool use32, Bitu selector, Bitu offset, Bitu oldeip);
void saveactstate();
void engine_ret(Bitu myreg_eip);

void enginestep();
void restart_calls();
bool MC2_PlaybackBlocksHostInput();

// True while a recording is being made OR replayed (m_IsRecording || m_IsPlaying).
// Used to stop DOSBox-X from grabbing/locking the host mouse cursor while a
// recording tool (or MC2_PlaybackBlocksHostInput) is driving the input instead
// of a human at the mouse - grabbing it would fight with external mouse
// injection and gives no benefit since MC2_PlaybackBlocksHostInput already
// keeps host input out of the emulated driver during playback turns.
bool MC2_RecordingOrPlaybackActive();

#endif
