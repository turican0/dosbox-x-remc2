#include <stdio.h>
#include <assert.h>
#include <sstream>
#include <stddef.h>
#include "dosbox.h"

#include "logging.h"
#ifndef ENGINE_SUPPORT_ACTIVE
#define ENGINE_SUPPORT_ACTIVE
extern Bit8u* readbuffer;
extern char* printbuffer;//char* buffer; // [esp+0h] [ebp-2h]
extern char* printbuffer2;//char v11; // [esp+40h] [ebp+3Eh]
extern char x_DWORD_D41A4_xB6;//2A1644 b6=182
extern char x_DWORD_D41A4_x16;//2A15A4 16=22
extern char x_DWORD_D41A4_x19;//2A51BD 19=25
//extern char* char_355198;
void myWriteOut(const char * format, ...);
int myprintf(const char * format, ...);
void pathfix(char* path, char* path2);
void support_begin();
void support_end();
#endif //ENGINE_SUPPORT_ACTIVE
