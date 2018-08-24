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

#endif
