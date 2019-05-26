#include <stdio.h>
#include <assert.h>
#include <sstream>
#include <stddef.h>
#include "dosbox.h"
#include "engine.h"
#include "mem.h"

#include "cpu.h"
#include "memory.h"
#include "debug.h"
#include "mapper.h"
#include "setup.h"
#include "programs.h"
#include "paging.h"
#include "callback.h"
#include "cpu/lazyflags.h"
#include "support.h"
#include "control.h"

#include "engine.h"

#include "inout.h"
#include "callback.h"
#include "pic.h"
#include "fpu.h"
#include "mmx.h"

#include "debug.h"
#include "shell.h"

#include "engine_support.h"





//#include "sub_160_26DB3A.h"
//#include "sub_main.h"
//#include "test-engine.h"


Bit8u pretemp;
Bit8u pre2temp;
Bit8u pre3temp;
Bit8u pre4temp;
Bit8u temp;

//bool stopturnon = true;
//int stopindex=0;

Bit32u oldmem=-123456789;
long count = 0;
FILE *fptestep;
FILE *fptestepcc;
unsigned long findvarseg=0x168;
//unsigned long findvaradr= 0x2a51a4;
//unsigned long findvaradr= 0x351660;
//unsigned long findvaradr = 0xaaa355200;
//unsigned long findvaradr = 0x19f0ec;
//unsigned long findvaradr = 0x35606d;
unsigned long findvaradr = 0xa5c78e00;

unsigned long findvarval = 0x034c;


unsigned long prepreprepreprelastsel;
unsigned long prepreprepreprelastoff;
unsigned long prepreprepreprelastesp;

unsigned long preprepreprelastsel;
unsigned long preprepreprelastoff;
unsigned long preprepreprelastesp;

unsigned long prepreprelastsel;
unsigned long prepreprelastoff;
unsigned long prepreprelastesp;

unsigned long preprelastsel;
unsigned long preprelastoff;
unsigned long preprelastesp;

unsigned long prelastsel;
unsigned long prelastoff;
unsigned long prelastesp;

unsigned long lastsel;
unsigned long lastoff;
unsigned long lastesp;
bool pause=false;
char findname[100];

long callcount = 0;
char findnamec[100];
FILE* fptestepc;

char charbuffer[500];
char findnamecc[500];
//write sequence

void write_sequence() {
}

//write sequence
int lastwriteindexsequence = 0;
FILE* fseq[10];
Bit32u writesequencecodeadress[10];
int writesequencecount[10];
int writesequencesize[10];
int writesequencecount2[10];
Bit32u writesequencedataadress[10];
//Bit32u writesequencedataadress2 = 0;
//Bit32u writesequencedataadress3 = 0;
char findnamex[100];
void writesequence(Bit32u codeadress, int count, int size, Bit32u dataadress) {
    writesequencecodeadress[lastwriteindexsequence] = codeadress;
    writesequencecount[lastwriteindexsequence] = count;
    writesequencesize[lastwriteindexsequence] = size;
    writesequencedataadress[lastwriteindexsequence] = dataadress;
    writesequencecount2[lastwriteindexsequence] = 0;
    //writesequencedataadress2 = dataadress2;
    //writesequencedataadress3 = dataadress3;
    sprintf(findnamex, "sequence-%08X-%08X.bin", writesequencecodeadress, dataadress);
    fopen_s(&fseq[lastwriteindexsequence], findnamex, "ab");
    fclose(fseq[lastwriteindexsequence]);
    lastwriteindexsequence++;
}

void savesequence(int index,int actsize, Bit32u dataadress) {
    sprintf(findnamex, "sequence-%08X-%08X.bin", writesequencecodeadress[index], dataadress);
    fopen_s(&fseq[index], findnamex, "ab+");
    //fwrite(&actcount, 4, 4, fseq);
    unsigned char buffer[1];
    for (long i = 0; i < actsize; i++) {
        buffer[0] = (unsigned char)mem_readb(i+ dataadress);
        fwrite(buffer, 1, 1, fseq[index]);
    }

    fclose(fseq[index]);
};
//write sequence


//call stop
Bit32u addprocedurestopadress = 0;
Bit32u addprocedurestopfindvaradr = 0;
int addprocedurestopcount = -1;
bool addprocedurestopforce = false;
bool addprocedurestoponmemchange = false;
bool stoponmemchange = false;
Bit32u addprocedurecounteradress = 0;
Bit32u addprocedurecount = 0;
void addprocedurestop(Bit32u adress,int count,bool force,bool memchange, Bit32u memadress, Bit32u counteradress) {
    addprocedurestopadress = adress;
    addprocedurestopcount = count;
    addprocedurestopforce = force;
    addprocedurestoponmemchange = memchange;
    addprocedurestopfindvaradr = memadress;
    addprocedurecounteradress = counteradress;
}
//call stop



void writesubcall(char* text, int level) {
    fopen_s(&fptestepc, findnamec, "a+");
    for (int i = 0;i<level;i++)fprintf(fptestepc, " ");
    fprintf(fptestepc, text);
    fclose(fptestepc);
};

bool inspect_on = false;
char findnamespy[100];
FILE* fptestepspy;

void spywrite(int adress) {
    Bit32u actmem = mem_readd(SegPhys(ds) + adress);
    fprintf(fptestepspy, "%08X:%08X |", adress, actmem);    
}

void spytest(int procedure) {
    if (reg_eip == procedure)
    {
        sprintf(findnamespy, "spy.txt");
        fopen_s(&fptestepspy, findnamespy, "a+");

        fprintf(fptestepspy, "-----------------------\n");
        fprintf(fptestepspy, "proc:%08X\n", procedure);
        spywrite(0x3514b0);//adress
        spywrite(0x3514b4);//adress
        spywrite(0x3514b8);//adress
        spywrite(0x3514bc);//adress
        fprintf(fptestepspy, "\n");
        spywrite(0x3514c0);//adress
        spywrite(0x3514c4);//adress
        spywrite(0x3514c8);//adress
        spywrite(0x3514cc);//adress
        fprintf(fptestepspy, "\n");
        spywrite(0x3514d0);//adress
        spywrite(0x3514d4);//adress
        spywrite(0x3514d8);//adress
        spywrite(0x3514dc);//adress
        fprintf(fptestepspy, "\n");
        spywrite(0x3514e0);//adress
        spywrite(0x3514e4);//adress
        spywrite(0x3514e8);//adress
        spywrite(0x3514ec);//adress
        fprintf(fptestepspy, "\n\n");
        spywrite(0x3514b0 + 0x80);//adress
        spywrite(0x3514b0+0x84);//adress
        spywrite(0x3514b0+0x88);//adress
        spywrite(0x3514b0+0x8c);//adress
        fprintf(fptestepspy, "\n");
        fclose(fptestepspy);
    }
}

void spytest_269497() {
    if (reg_eip == 0x269497)
    {
        sprintf(findnamespy, "spy.txt");
        fopen_s(&fptestepspy, findnamespy, "a+");

        fprintf(fptestepspy, "**********************\n");
        fprintf(fptestepspy, "v1:%08X,v4:%08X,v3:%08X,v2:%08X,v0:%08X\n", reg_edx, reg_edi, reg_ecx, reg_eax, reg_ebx);
        /*reg_ecx
        edx - v1
        edi    v4
        ecx v3
         eax   v2
         ebx   v0*/

        
        fclose(fptestepspy);
    }
}

void spytest_268bbc() {
    if (reg_eip == 0x268bbc)
    {
        sprintf(findnamespy, "spy.txt");
        fopen_s(&fptestepspy, findnamespy, "a+");

        fprintf(fptestepspy, "**********************\n");
        fprintf(fptestepspy, "i:%08X,result:%08X,v4:%08X\n", reg_ebx, reg_eax, reg_edx);
        /*reg_ecx
        edx - v1
        edi    v4
        ecx v3
         eax   v2
         ebx   v0*/


        fclose(fptestepspy);
    }
}

void spyinspect() {
    if (inspect_on)
    {        
        spytest(0x268610);//procedure
        //spytest(0x228560);//procedure
        spytest(0x269450);//procedure
        spytest(0x268580);//procedure
        spytest(0x26a520);//procedure
        spytest(0x26a5be);//procedure
        spytest(0x26a830);//procedure
        spytest(0x26a893);//procedure
        spytest(0x268610);//procedure
        spytest(0x268c10);//procedure
        spytest(0x2682a0);//procedure

        spytest(0x268b70);//procedure
        spytest(0x268b83);//procedure

        spytest_269497();//procedure
        spytest_268bbc();//procedure    
    }
}

void addspy() {
    sprintf(findnamespy, "spy.txt");
    fopen_s(&fptestepspy, findnamespy, "wt");
    fclose(fptestepspy);
    inspect_on = true;
};

long xcounter = 0;
long xcounter2 = 0;

bool killmouse = false;
int mousetest = 0;

bool firstrunx = true;

void enginestep() {
    
    if (count == 0) {
        //writesequence(0x2285ff, 0x50,320*200, 0x3aa0a4, 0, 0);
        //writesequence(0x2285ff, 0x50, 0x1b50, 0x2aa51c, 0, 0);//x_BYTE_D951C
        //writesequence(0x2285ff, 0x50, 0x14600, 0x2cbee0, 0, 0);
        //writesequence(0x2285ff, 0x50, 0x14600, 0x2c7ee0, 0, 0);
        //writesequence(0x2285ff, 0x50, 0x36e16, 0x356038, 0, 0);
        //writesequence(0x2285d1, 0x50, 0x36e16, 0x356038, 0, 0);
        //writesequence(0x2387d9, 10000, 0x36e16, 0x356038, 0, 0);
        //writesequence(0x238a3d, 0x3e8*2, 0x36e16, 0x356038, 0, 0);
        //writesequence(0x2285ff, 20, 0x36e16, 0x356038, 0, 0);
        //writesequence(0x2285ff, 0x20*0xb0, 0xb0, 0x3514b0, 0, 0);
        //writesequence(0x2285ff, 0x20 * 0xc4e, 0xc4e, 0x2b3a74, 0, 0);
        //writesequence(0x2685a7, 200, 0xb0, 0x3514b0, 0, 0);
        //writesequence(0x2285ff, 2000, 0xc4e, 0x2b3a74, 0, 0);

        //writesequence(0x222bd3, 0x500, 0x28*4, 0x2c3c20, 0, 0);

        //writesequence(0x2285ff, 0x10, 0x14600, 0x2c7ee0, 0, 0);//x_BYTE_F6EE0_tablesx
        //writesequence(0x2285ff, 0x10, 0x28, 0x2c3c20, 0, 0);//x_DWORD_F2C20ar
        //writesequence(0x2285ff, 0x10000, 0x28, 0x2c3c20, 0, 0);//x_DWORD_F2C20ar

        //writesequence(0x2285ff, 0x20, 0x10000, 0x2dc4e0, 0, 0);//x_BYTE_10B4E0
        //writesequence(0x2285ff, 0x20, 0x10000, 0x2fc4e0, 0, 0);//x_BYTE_12B4E0
        //writesequence(0x2285ff, 0x20, 0x10000, 0x30c4e0, 0, 0);//x_BYTE_13B4E0

        //writesequence(0x269450, 0x300, 0xb0, 0x3514b0, 0, 0);
        //writesequence(0x269450, 0x300, 0xc4e, 0x2b3a74, 0, 0);

        //writesequence(0x2694bc, 0x300, 0xb0, 0x3514b0, 0, 0);
        //writesequence(0x2694bc, 0x300, 0xc4e, 0x2b3a74, 0, 0);

        //writesequence(0x268610, 0x300, 0xb0, 0x3514b0, 0, 0);

        writesequence(0x001f1c80, 0x30, 0x70000, 0x2dc4e0);
        writesequence(0x001f1c80, 0x30, 0x36e16, 0x356038);
        
        //addprocedurestop(0x235a50, 0x0, true, true, 0x358ffc00 + 0x333);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x35932f);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x2b98e0);

        //addprocedurestop(0x1f11c0, 0x0, true, true, 0x2bac3000);

        //addprocedurestop(0x1f11ff, 0x0, true, true, 0x2bac3000);

        //next addprocedurestop(0x1f11c0, 0x0, true, true, 0xa5d57000);

        //next addprocedurestop(0x1f1130, 0x0, true, true, 0xa5d57000);

        //next addprocedurestop(0x238a3d, 0x79, true, true, 0xa5d57000);

        //next addprocedurestop(0x236F70, 0x0, true, true, 0x363b0e + 0x4c);

        //next addprocedurestop(0x238a3d, 0xa4, true, true, 0xa5d57000);

        //addprocedurestop(0x236F70, 0x0, true, true, 0x2ea12b);

        //addprocedurestop(0x228592, 0x0, true, true, 0x2c596200);


        //addprocedurestop(0x244c90, 0x0, true, true, 0x363a6600 + 0xc);
        //addprocedurestop(0x238a3d, 0x79, true, true, 0x363a6600 + 0xc);

        //addprocedurestop(0x20f38d, 0x0, true, true, 0x3aa0a400 + 0x4ed);
        //addprocedurestop(0x242cf9, 0xa3, true, true, 0x3aa0a400 + 0x1cde);
        //addprocedurestop(0x244600, 0x0, true, true, 0x3aa0a4 + 0x59b);

        //addprocedurestop(0x21ee85, 0x0, true, true, 0x2c3c3000);

        //addprocedurestop(0x2221a0, 0x0, true, true, 0x2c3c3000);
        //addprocedurestop(0x23354c, 0x0, true, true, 0x3aa0a400 + 0x51d, 0x242cf9);
        //addprocedurestop(0x233d56, 0x2, true, true, 0x3aa0a400 + 0x51d, 0x242cf9);
        //addprocedurestop(0x1f8a00, 0x0, true, true, 0x35ce7600, 0x242cf9);

        //addprocedurestop(0x228560, 0x0, true, true, 0x356038 + 0x7b, 0x242cf9);
        //addprocedurestop(0x252930, 0x0, true, true, 0x35603800 + 0x7b, 0x242cf9);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x2c77f8, 0x25283c);
        //addprocedurestop(0x25283c, 0x3, true, true, 0x2c76f000 + 0x78, 0x25283c00);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x2c657a, 0x2526c0);
        //addprocedurestop(0x2526c0, 306, true, true, 0x2c657a, 0x2526c0);
        //addprocedurestop(0x22b1e0, 0, true, true, 0x2c657a00, 0x2526c0);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x2c657a, 0x22b21a);
        //addprocedurestop(0x228560, 0x0, true, true, 0x38cfcb, 0x242cf9);
        //addprocedurestop(0x242cf9, 1130, true, true, 0x3aa0a400 + 0x51d, 0x242cf9);
        //addprocedurestop(0x22b21a, 306, true, true, 0x2c657a00, 0x22b21a0);

        //addprocedurestop(0x236F70, 0x0, true, true, 0x351560, 0x242cf9);
        //addprocedurestop(0x228560, 0x0, true, true, 0x3aa0a4 + 0xddd0);
        //addprocedurestop(0x228560, 0x0, true, true, 0x3aa0a4 + 0x51d,0x242cf9);
        //addprocedurestop(0x242cf9, 0x46A, true, true, 0x3aa0a400 + 0x51d, 0x242cf9);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x38cfcb,0x12345678);
        //addprocedurestop(0x1f3330, 0x1, true, true, 0x35603800 + 0x365fc + 8);
        //addprocedurestop(0x1f3100, 0x0, true, true, 0x35603800 + 0x365fc + 8);
        //addprocedurestop(0x22b268, 0x16, true, true, 0x35603800 + 0x2f7d);
        //addprocedurestop(0x228560, 0x0, true, true, 0x356038 + 0x8f7e, 0x242cf9);
        //addprocedurestop(0x228560, 0x0, true, true, 0x356038 + 0x8f97, 0x242cf9);
        //addprocedurestop(0x249bf0, 0x0, true, true, 0x35603800 + 0x8f97, 0x242cf9);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x396553 + 4, 0x242cf9);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x356038 + 0xd886, 0x242cf9);
        //addprocedurestop(0x238a3d, 0x71, true, true, 0x356038 + 0xd886, 0x242cf9);

        //addprocedurestop(0x228560, 0x2, true, true, 0x356038 + 0x36df6, 0x242cf9);
        //addprocedurestop(0x236250, 0x0, true, true, 0x35603800 + 0x36df6, 0x242cf9);
        //addprocedurestop(0x228560, 0x2, true, true, 0x3aa0a4 + 0x9b6e, 0x242cf9);

        //addprocedurestop(0x242a00, 0x1, true, true, 0x35603800 + 0x2f7d);

        //addprocedurestop(0x1f3100, 0x0, true, true, 0x35603800 + 0x2f7d);

        //addprocedurestop(0x2342d2, 0x0, true, true, 0x3aa0a400 + 0x51d);

        //addprocedurestop(0x232bb0, 0x0, true, true, 0x3aa0a400 + 0x51d, 0x242cf9);

        //addprocedurestop(0x22f320, 0x9, true, true, 0x2c3c3000);
        //addprocedurestop(0x211fe8, 0x12, true, true, 0x2c3c3000);
        //addprocedurestop(0x211fd8, 0x12, true, true, 0x3622c6+0x14);
        //addprocedurestop(0x21eb44, 0x0, true, true, 0x2c3c3000);

        //addprocedurestop(0x222bd3, 0x0, true, true, 0x3aa0a400 + 0x8f09);

        //addprocedurestop(0x244c90, 0x0, true, true, 0x3aa0a400 + 0x26c0);


        //addprocedurestop(0x228560, 0x0, true, true, 0x362026+0x14, 0x211fd8);
        //addprocedurestop(0x238f20, 0x9, true, true, 0x35603800 + 0x12aa, 0x242cf9);
        //addprocedurestop(0x228560, 0x2, true, true, 0x362026 + 0x14, 0x211fd8);
        //addprocedurestop(0x211fd8, 32, true, true, 0x35603800 + 0x12aa, 0x242cf9);
        //addprocedurestop(0x2387d9, 0, true, true, 0x35603800 + 0x12aa, 0x242cf9);
        //addprocedurestop(0x228560, 0x2, true, true, 0x3aa0a4 + 0x1f71, 0x242cf9);
        //addprocedurestop(0x2454f0, 0x0, true, true, 0x3aa0a400 + 0x1f71, 0x242cf9);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x38cfcb, 0x12345678);
        //addprocedurestop(0x20ce30, 0x0, true, true, 0x38cfcb00, 0x12345678);
        //addprocedurestop(0x228560, 0x07, true, true, 0x356038 + 0x9022, 0x12345678);
        //addprocedurestop(0x26a5d0, 0x0, true, true, 0x3aa0a400 + 0x9b6e, 0x242cf9);
        //addprocedurestop(0x228760, 0x0, true, true, 0x35603800 + 0x12aa, 0x242cf9);
        //addprocedurestop(0x238f20, 0x2, true, true, 0x35603800 + 0x12aa, 0x242cf9);
        //addprocedurestop(0x238756, 0xc2e, true, true, 0x35603800 + 0x12aa, 0x242cf9);
        //addprocedurestop(0x238a3d, 0x1f6a, true, true, 0x38cfcb00, 0x12345678);
        //addprocedurestop(0x228760, 0x0, true, true, 0x3aa0a400 + 0x1f71, 0x242cf9);
        //addprocedurestop(0x1fc8c0, 0x0, true, true, 0x38cfcb00, 0x12345678);
        //addprocedurestop(0x238a3d, 0x1b83, true, true, 0x35603800 + 0x9022, 0x242cf9);
        //addprocedurestop(0x268610, 0x0, true, true, 0x38cfcb00, 0x12345678);
        //addprocedurestop(0x268580, 0x1, true, true, 0x38cfcb00, 0x12345678);
        //addprocedurestop(0x268090, 0x0, true, true, 0x38cfcb00, 0x12345678);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x2b3a74 + 0x21f, 0x12345678);
        //addprocedurestop(0x269b20, 0x0, true, true, 0x2b3a74 + 0x88d, 0x12345678);
        //addprocedurestop(0x228560, 0x0, true, true, 0x2b3a74 + 0x22, 0x12345678);
        //addprocedurestop(0x269450, 0x1, true, true, 0x2b3a7400 + 0x22, 0x12345678);
        //addprocedurestop(0x268b70, 0x1, true, true, 0x2b3a7400 + 0x22, 0x12345678);
        //addprocedurestop(0x2285e0, 0x0, true, true, 0x2b43b6 + 2, 0x12345678);
        //addprocedurestop(0x228560, 0x41, true, true, 0x3aa0a4 + 0x1e29, 0x12345678);
        //addprocedurestop(0x29e550, 0x0, true, true, 0x3514b000 + 0x8e, 0x12345678);
        //addprocedurestop(0x2459c3, 0x0, true, true, 0x3514b000 + 0x8e, 0x12345678);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x3514b0 + 0, 0x268610);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x35153e, 0x268610);


        //addprocedurestop(0x268610, 0x73, true, true, 0x3514b000 + 0, 0x12345678);
        //addprocedurestop(0x268610, 0x50, true, true, 0x3514b000 + 0, 0x12345678);
        //addprocedurestop(0x2681f0, 0x0, true, true, 0x35153e00, 0x268610);
        //addprocedurestop(0x269450, 0x4, true, true, 0x3514b0, 0x268610);
        //addprocedurestop(0x269450, 0x4, true, true, 0x3514ccd00 , 0x268610);
        //addprocedurestop(0x228320, 0x0, true, true, 0x3aa0a4 +0x2168, 0x268610);
        //addprocedurestop(0x20fcc0, 0x0, true, true, 0x34eb5400, 0x268610);
        //addprocedurestop(0x21f370, 0x92, true, true, 0x2bc3a800, 0x268610);
        //addprocedurestop(0x236F70, 0x0, true, true, 0x3655f6+0x1c, 0x268610);
        //addprocedurestop(0x246f60, 0x0, true, true, 0x3655f6 + 0x1c, 0x268610);
        //addprocedurestop(0x249226, 0x9, true, true, 0x3655f600 + 0x1c, 0x268610);
        //addprocedurestop(0x1f1780, 0x0, true, true, 0x3655f600 + 0x1c, 0x268610);
        //addprocedurestop(0x240a70, 0x0, true, true, 0x240a7000, 0x268610);
        //addprocedurestop(0x242a00, 0x7, true, true, 0x356038 + 0x1154e, 0x268610);
        //addprocedurestop(0x228560, 0x0, true, true, 0x35603800 + 0x1153e + 0x45, 0x268610);
        //addprocedurestop(0x2285ff, 0x7, true, true, 0x2dc4e0, 0x268610);
        //addprocedurestop(0x2285ff, 0xb, true, true, 0x30c4e0 + 0x2, 0x2272a000);
        //addprocedurestop(0x2285ff, 0x1a, true, true, 0x356038 + 0x1156c, 0x2272a000);
        //addprocedurestop(0x2285ff, 0x0, true, true, 0x3aa0a4 + 0xe051el, 0x2272a000);
        //addprocedurestop(0x222bd3, 0/*0x46c*/, true, true, 0x2c3c20 + 0x64, 0x2272a000);
        //addprocedurestop(0x21f370, 0x5f, true, true, 0x400604, 0x2272a000);
        //addprocedurestop(0x238b20, 0x0, true, true, 0x3aa0a400 + 0xe051el, 0x2272a000);
        //addprocedurestop(0x20fcc0, 0x0, true, true, 0x35159c00, 0x2272a000);
        //addprocedurestop(0x20D87A, 0x0, true, true, 0x351660, 0x2272a000);
        //addspy();

        sprintf(findname, "find-%04X-%08X.txt", findvarseg, findvaradr);
        fopen_s(&fptestep, findname, "wt");
        fclose(fptestep);

        /*sprintf(findname, "call-all.txt", findvarseg, findvaradr);
        fopen_s(&fptestep, findname, "wt");
        fclose(fptestep);*/
    }
    if (count > 10000)
    {
        spyinspect();
        if (addprocedurestopcount != -1)
        {
            if (addprocedurestopadress && (reg_eip == addprocedurestopadress)) {
                if (addprocedurestopcount == 0)
                {
                    //saveactstate();
                    DEBUG_EnableDebugger();
                    if (addprocedurestoponmemchange) {
                        stoponmemchange = true;
                        findvaradr = addprocedurestopfindvaradr;
                        /*if (0x20eaa0 == addprocedurestopadress)
                        {
                            addprocedurestopadress = 0x20ef1f;
                            addprocedurestopcount = 0x3;
                        }*/
                    }
                    xcounter2++;
                }
                else addprocedurestopcount--;
            }
        }
        if (reg_eip == 0x228388) {//test mouse
            if (mousetest == 2)
            {
                //if (!x_WORD_18074A_mouse_right2_button && !x_WORD_180744_mouse_right_button)//first cycle after press and ...
                {
                    //x_WORD_180744_mouse_right_button = 1;
                    mem_writew(0x351744, 1);

                    //castle change
                    //mem_writed(0x2a5c52 + 0x1856 + 6, 0x22ba40);

                    //x_WORD_E375C_mouse_position_x = temp_mouse_x;
                    //mouse_state = temp_mouse_y;
                    //x_WORD_E375E_mouse_position_y = temp_mouse_y;
                }
                //x_WORD_18074A_mouse_right2_button = 1;
                mem_writew(0x35174a, 1);
            }
            mousetest++;
        }
        /*if (reg_eip == 0x22857e) {//test mouse
            if (mousetest >= 1)
            {
                mem_writed(0x35159c, 0x10);
            }
            mousetest++;
        }*/
        if (reg_eip == 0x237a30) {//skipscreen
            killmouse = true;
        }
        /*if ((reg_eip == 0x26db3a)&&killmouse) {//rotate off
            reg_ecx = 0x140;
            reg_edx = 0xc8;
            //DEBUG_EnableDebugger();
            //=Segs.phys[eip] = 0;
            //reg_eip = 0x26dd26;
        }*/
        /*if ((reg_eip == 0x1f8060) && killmouse) {//skipscreen
            reg_ecx = 0xc8;
            reg_edx = 0x160;
            //DEBUG_EnableDebugger();
            //=Segs.phys[eip] = 0;
            //reg_eip = 0x26dd26;
        }*/
        /*if ((reg_eip == 0x238c10) && killmouse) {//rotate
            reg_ax = 0x6;
            //DEBUG_EnableDebugger();
            //=Segs.phys[eip] = 0;
            //reg_eip = 0x26dd26;
        }*/
        if (reg_eip == 0x236FE1) {//skipscreen
            mem_writeb(0x2a51ad,1);
        }
        if ((reg_eip == 0x2285ff)&&(firstrunx)) {//skipscreen
            //DEBUG_EnableDebugger();
            firstrunx = false;
        }
        if (reg_eip == addprocedurecounteradress) {
            sprintf(findnamecc, "counter-%08X.txt", addprocedurecounteradress);
            fopen_s(&fptestepcc, findnamecc, "a+");
            fprintf(fptestepcc, "%d\n", addprocedurecount);
            fclose(fptestepcc);
            addprocedurecount++;
        }
        for(int ii=0;ii< lastwriteindexsequence;ii++)
        if (writesequencecount[ii] != -1) {
            if (reg_eip == writesequencecodeadress[ii]) {
                if (writesequencecount2[ii] < writesequencecount[ii])
                {
                    savesequence(ii,writesequencesize[ii], writesequencedataadress[ii]);
                    //if(writesequencedataadress2>0)savesequence(writesequencesize, writesequencedataadress2);
                    //if (writesequencedataadress3 > 0)savesequence(writesequencesize, writesequencedataadress3);
                    writesequencecount2[ii]++;
                }
            }
        }
        /*
        if ((reg_eip==0x218240) && (SegValue(cs) == 0x160))
        {
            if(xcounter>= 0xfe)
            {
                DEBUG_EnableDebugger();
            }
            xcounter++;
        }*/
        if ((SegValue(ds) == findvarseg)&& (SegValue(cs) == 0x160))
        {
            Bit32u actmem = mem_readd(SegPhys(ds) + findvaradr);
            if (oldmem != actmem)
            {
                //if (findvarval == actmem)
                //if (0x77 == actmem&0xff)
                {
                    fopen_s(&fptestep, findname, "a+");
                    fprintf(fptestep, "PREPREPREPREPRECALL%04X:%08X/%08X - %08X\n", lastsel, prepreprepreprelastoff, prepreprepreprelastoff - 0x1E1000, prepreprepreprelastesp);
                    fprintf(fptestep, "PREPREPREPRECALL%04X:%08X/%08X - %08X\n", lastsel, preprepreprelastoff, preprepreprelastoff - 0x1E1000, preprepreprelastesp);
                    fprintf(fptestep, "PREPREPRECALL%04X:%08X/%08X - %08X\n", lastsel, prepreprelastoff, prepreprelastoff - 0x1E1000, prepreprelastesp);
                    fprintf(fptestep, "PREPRECALL%04X:%08X/%08X - %08X\n", lastsel, preprelastoff, preprelastoff - 0x1E1000, preprelastesp);
                    fprintf(fptestep, "PRECALL%04X:%08X/%08X - %08X\n", lastsel, prelastoff, prelastoff - 0x1E1000, prelastesp);
                    fprintf(fptestep, "CALL%04X:%08X/%08X - %08X\n", lastsel, lastoff, lastoff - 0x1E1000, lastesp);
                    fprintf(fptestep, "ADR%04X:%08X/%08X\n", SegValue(cs), reg_esp, reg_esp - 0x1E1000);
                    fprintf(fptestep, "NEW VALUE%04X:%08X - %04X,%04X\n", findvarseg, findvaradr, oldmem, actmem);
                    pause = true;
                    fclose(fptestep);
                    sprintf(charbuffer, "NEW VALUE%04X:%08X - %04X,%04X\n", findvarseg, findvaradr, oldmem, actmem);
                    writesubcall(charbuffer, 0);                    
                }
                oldmem = actmem;
            }
        }
        if(pause)
            if (SegValue(cs) == 0x160)
            {
                fopen_s(&fptestep, findname, "a+");
                pause = false;
                fprintf(fptestep, "AFTER 04X:%08X/%08X\n\n", SegValue(cs), reg_esp, reg_esp - 0x1E1000);
                //if (0x6F732F == oldmem)saveactstate();
                if(stoponmemchange)
                    DEBUG_EnableDebugger();
                //DEBUG_EnableDebugger();
                fclose(fptestep);
                //findvarval = 0;//fix
            }
    }
    count++;
}
void saveactstate() {
    char name1[1024];
    sprintf(name1, "engine-registers-%04X-%08X", SegValue(cs), reg_eip);
    char name2[1024];
    sprintf(name2, "engine-memory-%04X-%08X", SegValue(cs), reg_eip);
    FILE *fptw1;
    fopen_s(&fptw1, name1, "wt");
    fprintf(fptw1, "%04X:%08X\n", SegValue(cs), reg_eip);
    fprintf(fptw1, "EAX:%08X,EBX:%08X,ECX:%08X,EDX:%08X\n", reg_eax, reg_ebx, reg_ecx, reg_edx);
    fprintf(fptw1, "ESI:%08X,EDI:%08X,EBP:%08X,ESP:%08X\n", reg_esi, reg_edi, reg_ebp, reg_esp);
    fprintf(fptw1, "CS:%04X,DS:%04X,ES:%04X,FS:%04X,GS:%04X,SS:%04X\n", SegValue(cs), SegValue(ds), SegValue(es), SegValue(fs), SegValue(gs), SegValue(ss));
    fprintf(fptw1, "CF:%01X,ZF:%01X,SF:%01X,OF:%01X,AF:%01X,PS:%01X,IF:%01X\n", (get_CF()>0), (get_ZF()>0), (get_SF()>0), (get_OF()>0), (get_AF()>0), (get_PF()>0), GETFLAGBOOL(IF));

    fclose(fptw1);
    FILE *fptw;
    fopen_s(&fptw, name2, "wb");
    unsigned char buffer[1];
    for (long i = 0;i < 0x1000000;i++) {
        buffer[0] = (unsigned char)mem_readb(i);
        fwrite(buffer, 1, 1, fptw);
    }
    fclose(fptw);
}

int call_0x00000160_0x0026db3axxx(bool use32, Bitu selector, Bitu offset, Bitu oldeip) {

    //call
    Bit32u old_esp = reg_esp;
    Bit32u old_eip = reg_eip;
    Bitu rpl = selector & 3;
    Descriptor call;
    cpu.gdt.GetDescriptor(selector, call);
    CPU_Push32(SegValue(cs));
    CPU_Push32(oldeip);
    reg_eip = offset;
    Segs.expanddown[cs] = call.GetExpandDown();
    Segs.limit[cs] = do_seg_limits ? call.GetLimit() : ((PhysPt)(~0UL));
    Segs.phys[cs] = call.GetBase();
    cpu.code.big = call.Big() > 0;
    Segs.val[cs] = (selector & 0xfffc) | cpu.cpl;

    //push    ds
    reg_eip++;
    Bit32u new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), Segs.val[ds]);
    reg_esp = new_esp;


    //mov ds,cs:[0027a11c] and call call_0x0027a374
    reg_eip += 5;
    Bitu val = mem_readw(Segs.phys[cs] + 0x0027a11c);
    //Bitu val2 =mem_readw(SegPhys(cs)+0x0027a11c);
    CPU_SetSegGeneral(ds, val);
    //reg_esp+=4;

    //push    ebx
    reg_eip++;
    new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), reg_bx);
    reg_esp = new_esp;
    //push    esi
    reg_eip++;
    new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), reg_si);
    reg_esp = new_esp;
    //push    edi
    reg_eip++;
    new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), reg_di);
    reg_esp = new_esp;
    //push    ebp
    reg_eip++;
    new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), reg_bp);
    reg_esp = new_esp;

    printf("eip:%08x\n", reg_eip);
    printf("esp:%08x\n", reg_esp);
    printf("stack:");
    for (int i = Segs.phys[ss] - 8;i < Segs.phys[ss] + 8;i++)
    {
        printf("%02x", mem_readb(i));
        if (i == Segs.phys[ss])printf("|");
    }
    printf("\n");

    /*
    3/168
    if (CPU_SetSegGeneral((SegNames)which, val)) RUNEXCEPTION();*/

    /*
    DO_PREFIX_SEG(_SEG)					\
    BaseDS=SegBase(_SEG);					\
    BaseSS=SegBase(_SEG);					\
    core.base_val_ds=_SEG;					\
    GetRM;Bit16u val;Bitu which=(rm>>3)&7;
    if (rm >= 0xc0 ) {GetEArw;val=*earw;}
    else {GetEAa;val=LoadMw(eaa);}

    */

    //call_0x0027a374(use32, selector, offset, oldeip);
    /*

    //push    ebp
    reg_eip++;
    Bit32u new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), SegValue(ds));
    reg_esp = new_esp;

    //mov     ebp, esp
    reg_eip++;
    Bit32s addip = mem_readd_inline(reg_eip);
    reg_eip += 4;
    Bit32u here = ((PhysPt)(reg_eip - SegPhys(cs)));
    new_esp = (reg_esp&cpu.stack.notmask) | ((reg_esp - 4)&cpu.stack.mask);
    mem_writed(SegPhys(ss) + (new_esp & cpu.stack.mask), here);
    reg_esp = new_esp;
    reg_eip = (Bit32u)((Bit32u)addip + here);

    //sub     esp, 0Ch
    //reg_esp -= 0x0c;

    //sub     esp, 0Ch*/
    /*reg_eip++;
    reg_eip++;

    BaseDS = SegBase(cs);
        BaseSS = SegBase(cs);
        core.base_val_ds = cs;
        goto restart_opcode;


    DO_PREFIX_SEG(cs);*/
    /*
    rm = Fetchb();
    Bit16u val;Bitu which = (rm >> 3) & 7;
    if (rm >= 0xc0) { GetEArw;val = *earw; }
    else { GetEAa;val = LoadMw(eaa); }

    //core.cseip+=1;*/
    //DEBUG_Enable(true);
    return 1;
}


int32_t subx_27A374() { return 1; };

signed char g2b4767 = 1;

int32_t g351710 = 1;

int16_t g2b4760 = 79;

int16_t g2b4762 = 11;

int16_t g35174c = 0;

int16_t g35173e = 25;

int16_t g2b4764 = 25;

int16_t g351742 = 1;

int16_t g351746 = 0;

int16_t g2b475c = 0x1af;

int16_t g2b475e = 0xbb;

int16_t g35174a = 0;

int16_t g351744 = 0;

int16_t g351748 = 0;

int16_t g351740 = 0;

int32_t g2b4758 = 0;

int16_t g2b4ba4 = 4;

int32_t g351734 = 0x40040;

void subx_27A51B(int32_t a1) {};

int call_0x00000160_0x0026DB3Addd(bool use32, Bitu selector, Bitu offset, Bitu oldeip) {
//void sub_26DB3A(int32_t ecx) {
    int32_t ecx;
    int32_t eax2;
    int32_t v3;
    int32_t v4;
    int32_t edx5;
    int32_t eax8;
    int32_t eax9;
    int less_or_equal10;
    int less_or_equal11;
    int zf12;
    int zf13;
    int less_or_equal14;
    int16_t ax15;
    int zf16;
    int zf17;
    int32_t eax18;
    int32_t eax19;
    int zf20;
    int zf21;
    int zf22;
    int zf23;
    int32_t eax24;
    int32_t eax25;
    int zf26;
    int zf27;
    int zf28;
    int zf29;
    int32_t eax30;
    int32_t eax31;
    int zf32;
    int zf33;
    int16_t ax34;
    int zf35;
    int32_t eax36;

    //eax2 = sub_27A374();
    uint16_t actcs = Segs.val[cs];
    uint16_t actds = Segs.val[ds];
    uint32_t actebp = cpu_regs.regs[REGI_BP].dword[DW_INDEX];
    eax2=mem_readw(actcs + 0x0027a11c);
    /*
        27a374:	mov ds,[cs:0x27a11c]
        27a37c : ret
    }*/

    v3 = ecx;
    v4 = edx5;
    uint32_t g2b4768= mem_readw(actds + 0x002b4768);
    if (g2b4768 != 0) {
        g2b4767 = 1;
        mem_writeb(actds + 0x002b4767,g2b4767);

        eax2 = mem_readd(actds + actebp-0xc);

        g351710 = eax2;
        mem_writeb(actds + 0x00351710, g351710);
        //g351660 = mem_readd(actds + 0x00351660);

        v3= mem_readd(actds + actebp - 0x8);
        v4 = mem_readd(actds + actebp - 0x8);

        eax8 = v3;
        g2b4760 = eax8 & 0xffff;
        mem_writeb(actds + 0x002b4760, g2b4760);
        eax9 = v4;
        g2b4762 = eax8 & 0xffff;
        mem_writeb(actds + 0x002b4762, g2b4762);

        if (g2b4760 > 0x27e) {
            g2b4760 = 0x27e;
            mem_writeb(actds + 0x002b4760, g2b4760);
        }
        if (g2b4762 > 0x1de) {
            g2b4762 = 0x1de;
            mem_writeb(actds + 0x002b4762, g2b4762);
        }
        zf12 = (*(unsigned char*)(&g351710) & 2) == 0;
        if (!zf12) {
            if (g35174c == 0) {
                if (g35173e <= 0) {
                    ax15 = g2b4764;
                    g35173e = ax15;
                }
                else {
                    g351742 = 1;
                }
            }
            zf16 = g35174c == 0;
            if (!(!zf16 || (zf17 = g351746 == 0, !zf17))) {
                g351746 = 1;
                eax18 = v3;
                g2b475c = *(int16_t*)(&eax18);
                eax19 = v4;
                g2b475e = *(int16_t*)(&eax19);
            }
            g35174c = 1;
        }
        zf20 = (*(unsigned char*)(&g351710) & 4) == 0;
        if (!zf20) {
            g35174c = 0;
        }
        zf21 = (*(unsigned char*)(&g351710) & 8) == 0;
        if (!zf21) {
            if (!((g35174a != 0) || (g351744 != 0))) {
                g351744 = 1;
                eax24 = v3;
                g2b475c = *(int16_t*)(&eax24);
                eax25 = v4;
                g2b475e = *(int16_t*)(&eax25);
            }
            g35174a = 1;
        }
        zf26 = (*(unsigned char*)(&g351710) & 16) == 0;
        if (!zf26) {
            g35174a = 0;
        }
        zf27 = (*(unsigned char*)(&g351710) & 32) == 0;
        if (!zf27) {
            if (!((g351748 != 0) || (g351740 != 0))) {
                g351740 = 1;
                eax30 = v3;
                g2b475c = *(int16_t*)(&eax30);
                eax31 = v4;
                g2b475e = *(int16_t*)(&eax31);
            }
            g351748 = 1;
        }
        zf32 = (*(unsigned char*)(&g351710) & 64) == 0;
        if (!zf32) {
            g351748 = 0;
        }
/*        if ((g2b4758 == 0) && (ax34 = g2b4ba4, *(int16_t*)((int32_t)(&g351734) + 2) = ax34, sub_26D839(), sub_26D329(), (g351660 & 8) != 0)) {
            eax36 = g351734;
            sub_27A51B(eax36 >> 16);
        }*/
    }
    //__asm__("retf ");
    return 1;
}

FILE *fptrcont;
long callindex = 0;
long callmax = 300;
long calllevel = 0;
void begin_write() {
    char contname[1024];
    sprintf(contname, "calls.txt");
    fopen_s(&fptrcont, contname, "wt");
}

void end_write() {
    fclose(fptrcont);
}

void restart_calls() {
    if (callindex > 10)
    {
        fprintf(fptrcont, "----------\n");
        callindex = 1;
        calllevel = 0;
    }
    /*end_write();
    begin_write();*/
}

void call_write(Bitu selector, Bitu offset)
{
    if (callindex < callmax)
    {
        for(long i=0;i<calllevel;i++)fprintf(fptrcont, " ");
        fprintf(fptrcont, "%04X:%08X - %08X,%d\n", selector, offset, reg_esp, calllevel);
        callindex++;
        calllevel++;
    }
    if (callindex == callmax)
    {
        callindex++;
    }    
}

void engine_ret(Bitu myreg_eip) {
    if ((callindex < callmax)&& (callindex >1))
    {
        calllevel--;
    }
};

void pre_160_0026AB60() {};
void post_160_0026AB60() {reg_eip = CPU_Pop32();};

/*void pre_160_00279D52() {
    //SegValue(cs), reg_eip
    __CS__= SegValue(cs);
    __DS__ = SegValue(ds);
    __ES__ = SegValue(es);
    __SS__ = SegValue(ss);
    __FS__ = SegValue(fs);
    __GS__ = SegValue(gs);
};*/
void post_160_00279D52() {reg_eip = CPU_Pop32();};

void pre_160_00054200() {};
void post_160_00054200() {/* reg_eip = CPU_Pop32(); */};

void pre_160_0005B8D0() {};
void post_160_0005B8D0() {/* reg_eip = CPU_Pop32(); */ };

class ENGPRG : public Program {
public:
    void Run(void) {
        WriteOut("Video refresh rate.\n\n");        
    }
};
ENGPRG* engprg=NULL;
/*void myWriteOut(const char * format, ...) {
    DEBUG_ShowMsg(format);    
}*/


/*REGS readREG(Bit32u address) {
    REGS result;
    //((Bit32u*)result)[0]=mem_readd(SegPhys(ds) + address);
    return result;
}*/



void writecall(Bitu selector, Bitu offset) {
    if (callcount == 0) {
        sprintf(findnamec, "call-all.txt");
        fopen_s(&fptestepc, findnamec, "wt");
        fclose(fptestepc);
    }
    else
    {
        switch (selector) {
        case 0x00000160: {
            switch (offset) {
            //case 0x00236f70:writesubcall("main_0x00236f70\n", 0);break;

            case 0x237210:writesubcall("sub_56210_process_command_line_0x00237210\n", 0);break;
            case 0x0023C8D0:writesubcall("sub_5B8D0_initialize_0x0023C8D0\n", 0);break;
            case 0x00227830:writesubcall("sub_46830_main_loop_0x00227830\n", 0);break;
            case 0x0023CC20:writesubcall("sub_5BC20_0x0023CC20\n", 0);break;
            case 0x00237730:writesubcall("sub_56730_clean_memory_0x00237730\n", 0);break;

            case 0x00257930:writesubcall("sub_76930_menus_and_intros_0x00257930\n", 1);break;

            case 0x00257A40:writesubcall("sub_76A40_lang_setting_0x00257A40\n", 2);break;
            case 0x00257cf0:writesubcall("sub_76CF0_0x00257cf0\n", 2);break;
            case 0x00257d00:writesubcall("_wcpp_1_unwind_leave__131_0x00257d00\n", 2);break;
            case 0x00257d10:writesubcall("sub_76D10_intros_0x00257d10\n", 2);break;
            case 0x00257fa0:writesubcall("sub_76FA0_0x00257fa0\n", 2);break;
            case 0x002589e0:writesubcall("sub_779E0_lang_setting_loop_0x002589e0\n", 2);break;

            case 0x00264850:writesubcall("sub_83850_show_welcome_screen_0x00264850\n", 3);break;
            case 0x00257160:writesubcall("sub_76160_play_intro_0x00257160\n", 3);break;
            }
        }
        }
    }

    callcount++;
};

const int lastcallsstr_count = 500;
Bitu lastcallsstr[lastcallsstr_count];
long lastcallsindex=0;
void savecalls(Bitu offset) {    
    lastcallsstr[lastcallsindex] = offset;
    lastcallsstr[(lastcallsindex +1)% lastcallsstr_count] = 0;
    lastcallsindex++;
    if (lastcallsindex >= lastcallsstr_count)
        lastcallsindex = 0;
};

char* writecallsfilename = "writecalls.txt";
void writecalls() {
    long cbegin = lastcallsindex;
    if (cbegin < 0)cbegin += lastcallsstr_count;
    long cend= cbegin + lastcallsstr_count;
    FILE *fptw;
    fopen_s(&fptw, writecallsfilename, "wt");
    unsigned char buffer[1];
    for (long i = cbegin;i < cend;i++) {
        fprintf(fptw,"%04X-%08X\n", SegValue(ds), lastcallsstr[i%lastcallsstr_count]);
    }
    fclose(fptw);
}

long testcount = 0;



long callindex2=0;
int engine_call(bool use32, Bitu selector, Bitu offset, Bitu oldeip) {
    if (callindex2 > 10000)
    {
        //call_write(selector, offset);
        if (selector == 0x160) {
            prepreprepreprelastsel = preprepreprelastsel;
            prepreprepreprelastoff = preprepreprelastoff;
            prepreprepreprelastesp = preprepreprelastesp;

            preprepreprelastsel = prepreprelastsel;
            preprepreprelastoff = prepreprelastoff;
            preprepreprelastesp = prepreprelastesp;

            prepreprelastsel = preprelastsel;
            prepreprelastoff = preprelastoff;
            prepreprelastesp = preprelastesp;

            preprelastsel= prelastsel;
            preprelastoff= prelastoff;
            preprelastesp= prelastesp;

            prelastsel= lastsel;
            prelastoff= lastoff;
            prelastesp= lastesp;

            lastsel = selector;
            lastoff = offset;
            lastesp = reg_esp;
        }
    }
    else callindex2++;
    writecall(selector,offset);
    //if (offset == 0x00055F70)
    /*if((mem_readw(Segs.phys[cs] + reg_eip)==0x000362cc)||
        (mem_readw(Segs.phys[cs] + reg_eip) == 0xc40c8d45)||
        (mem_readw(Segs.phys[cs] + reg_eip) == 0x0083c40c))
    {
        DEBUG_EnableDebugger();
    }*/
    //uint32_t mycs = Segs.val[cs];
    switch (selector) {
    /*case 0x00000080: {
        DEBUG_EnableDebugger();
        return 0;
    }
    case 0x00000070: {
        DEBUG_EnableDebugger();
        return 0;
    }*/
        
    case 0x00000160: {
        savecalls(offset);
        if ((addprocedurestopadress == offset)&&(addprocedurestopcount!=-1)) {
            if (addprocedurestopcount == 0);// DEBUG_EnableDebugger();
            else
            addprocedurestopcount--;
        }
        switch (offset) {
            case 0x26E8000:{//181000 1A1000 rozdila a rozdil b//main
                //saveactstate();
                begin_write();
                //callindex = 1;
                //DEBUG_EnableDebugger();           
                
                /*char *argv[] = { "netherw.exe","-level","2", NULL };
                int argc = (sizeof(argv) / sizeof(argv[0]))-1;
                char *envp[] = {  NULL };
                //char *envp[] = { "env=xx", NULL };
                //ds:esi - cesta k nazvu
                support_begin();
                int retval=sub_main(argc,(char**)argv, (char**)envp);
                support_end();*/
                //saveactstate();
                //DEBUG_EnableDebugger();
                break;
                    
                }
            case 0x236F70: {
                //saveactstate();
                //if(xcounter>1)
                /*fopen_s(&fptestep, findname, "a+");
                fprintf(fptestep, "PREPREPREPREPRECALL%04X:%08X/%08X - %08X\n", lastsel, prepreprepreprelastoff, prepreprepreprelastoff - 0x1E1000, prepreprepreprelastesp);
                fprintf(fptestep, "PREPREPREPRECALL%04X:%08X/%08X - %08X\n", lastsel, preprepreprelastoff, preprepreprelastoff - 0x1E1000, preprepreprelastesp);
                fprintf(fptestep, "PREPREPRECALL%04X:%08X/%08X - %08X\n", lastsel, prepreprelastoff, prepreprelastoff - 0x1E1000, prepreprelastesp);
                fprintf(fptestep, "PREPRECALL%04X:%08X/%08X - %08X\n", lastsel, preprelastoff, preprelastoff - 0x1E1000, preprelastesp);
                fprintf(fptestep, "PRECALL%04X:%08X/%08X - %08X\n", lastsel, prelastoff, prelastoff - 0x1E1000, prelastesp);
                fprintf(fptestep, "CALL%04X:%08X/%08X - %08X\n", lastsel, lastoff, lastoff - 0x1E1000, lastesp);
                fprintf(fptestep, "ADR%04X:%08X/%08X\n", SegValue(cs), reg_esp, reg_esp - 0x1E1000);
                fprintf(fptestep, "AFTER 04X:%08X/%08X\n\n", SegValue(cs), reg_esp, reg_esp - 0x1E1000);
                //DEBUG_EnableDebugger();
                fclose(fptestep);*/
                //myAddBreakpoint(SegValue(cs), 0x258350, false);
                //myAddBreakpoint(SegValue(cs), 0x258351, false);
                //myAddBreakpoint(SegValue(cs), 0x258352, false);
                //myAddBreakpoint(SegValue(cs), 0x258353, false);
                //myAddBreakpoint(SegValue(cs), 0x25b742, false);
                //myAddBreakpoint(SegValue(cs), 0x25b747, false);
                //myAddBreakpoint(SegValue(cs), 0x25b74a, false);
                //25b742
                //25b747
                //25b74a
                //25b751
                //25b753
                //DEBUG_EnableDebugger();
                //xcounter++;
                break;
            }
            case 0x22a8a0: {
                //stopindex++;
                //if(stopindex>=0x91)DEBUG_EnableDebugger();
                //saveactstate();
                //if(xcounter>1)
                //oldmem = 0x12345678;
                //if(xcounter>= 0x574)
                {
                    //DEBUG_EnableDebugger();
                }
                //xcounter++;
                //DEBUG_EnableDebugger();
                break;
            }
            case 0x22b190:{
                //if (stopindex+1 >= 0x91)DEBUG_EnableDebugger();
                //stopindex++;
                //stopturnon = true;

                break;
            }
             
        }
        break;
    }
    }
    
    return 0;
}


