#include "engine_support.h"
#include "dos_system.h"

extern DOS_Device *DOS_CON;

void myWriteOut(const char * format, ...) {
    DEBUG_ShowMsg(format);
    /*Bit16u sz = 1;
    unsigned char b = 'a';
    DOS_CON->Write(&b, &sz);*/
}

int myprintf(const char * format, ...) {
    char prbuffer[1024];
    va_list arg;
    int done;
    va_start(arg, format);
    done = vsprintf(prbuffer, format, arg);
    va_end(arg);


    DEBUG_ShowMsg(prbuffer);
    //return strlen(prbuffer);
    return done;
    /*Bit16u sz = 1;
    unsigned char b = 'a';
    DOS_CON->Write(&b, &sz);*/
}


//delete after finalization
void pathfix(char* path, char* path2)
{
    if ((path[0] == 'c') || (path[0] == 'C'))
    {
        long len= strlen(path);
        char* fixstring = "c:/prenos/magic1/cd2/CARPET";
        long fixlen = strlen(fixstring);
        for (int i = len;i > 1;i--)
            path2[i + fixlen -2] = path[i];
        for (int i = 0;i < fixlen;i++)
            path2[i] = fixstring[i];
    }
    else
    {
        long len = strlen(path);
        char* fixstring = "c:/prenos/magic1/cd2/CARPET";
        long fixlen = strlen(fixstring);
        for (int i = len;i > -1;i--)
            path2[i + fixlen] = path[i];
        for (int i = 0;i < fixlen;i++)
            path2[i] = fixstring[i];
    }
}

Bit8u* readbuffer;
char* printbuffer;//char* buffer; // [esp+0h] [ebp-2h]
char* printbuffer2;//char v11; // [esp+40h] [ebp+3Eh]
char x_DWORD_D41A4_xB6 = 'C';//2A1644 b6=182
char x_DWORD_D41A4_x16 = '5';//2A15A4 16=22
char x_DWORD_D41A4_x19=0;//2A51BD 19=25
//char* char_355198 = "8R5";
void support_begin() {
    readbuffer = (Bit8u*)malloc(sizeof(Bit8u) * 1000000);//fix it max 64000
    printbuffer = (char*)malloc(sizeof(char) * 4096);
    //printbuffer[0] = '\0';
    printbuffer2 = (char*)malloc(sizeof(char) * 4096);
    //printbuffer2[0] = '\0';
}
void support_end() {
    free(readbuffer);
    free(printbuffer);//char* buffer; // [esp+0h] [ebp-2h]
    free(printbuffer2);//char v11; // [esp+40h] [ebp+3Eh]
}
