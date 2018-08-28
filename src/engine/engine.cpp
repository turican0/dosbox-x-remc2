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

Bit32u oldmem=-123456789;
long count = 0;
FILE *fptestep;
unsigned long findvarseg=0x168;
//unsigned long findvaradr= 0x2a51a4;
//unsigned long findvaradr= 0x351660;
//unsigned long findvaradr = 0xaaa355200;
unsigned long findvaradr = 0x47be3b;


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

void writesubcall(char* text, int level) {
    fopen_s(&fptestepc, findnamec, "a+");
    for (int i = 0;i<level;i++)fprintf(fptestepc, " ");
    fprintf(fptestepc, text);
    fclose(fptestepc);
};

void enginestep() {
    if (count == 0) {        
        sprintf(findname, "find-%04X-%08X.txt", findvarseg, findvaradr);
        fopen_s(&fptestep, findname, "wt");
        fclose(fptestep);

        /*sprintf(findname, "call-all.txt", findvarseg, findvaradr);
        fopen_s(&fptestep, findname, "wt");
        fclose(fptestep);*/
    }
    if (count > 10000)
    {
        if ((SegValue(ds) == findvarseg)&& (SegValue(cs) == 0x160))
        {
            Bit32u actmem = mem_readd(SegPhys(ds) + findvaradr);
            if (oldmem != actmem)
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
                    oldmem = actmem;
            }            
        }
        if(pause)
            if (SegValue(cs) == 0x160)
            {
                fopen_s(&fptestep, findname, "a+");
                pause = false;
                fprintf(fptestep, "AFTER 04X:%08X/%08X\n\n", SegValue(cs), reg_esp, reg_esp - 0x1E1000);
                if (0x6F732F == oldmem)saveactstate();//DEBUG_EnableDebugger();
                fclose(fptestep);
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
            case 0x00236f70:writesubcall("main_0x00236f70\n", 0);break;

            case 0x237210:writesubcall("sub_56210_process_command_line_0x00237210\n", 1);break;
            case 0x0023C8D0:writesubcall("sub_5B8D0_initialize_0x0023C8D0\n", 1);break;
            case 0x00227830:writesubcall("sub_46830_main_loop_0x00227830\n", 1);break;
            case 0x0023CC20:writesubcall("sub_5BC20_0x0023CC20\n", 1);break;
            case 0x00237730:writesubcall("sub_56730_clean_memory_0x00237730\n", 1);break;

            case 0x00251890:writesubcall("sub_70890_print_header_0x00251890\n", 2);break;
            case 0x0023D490:writesubcall("sub_5C490_testers_info_0x0023D490\n", 2);break;
            case 0x002370D0:writesubcall("sub_560D0_create_sound_dir_0x002370D0\n", 2);break;
            case 0x0023CCC0:writesubcall("sub_5BCC0_set_any_variables1_0x0023CCC0\n", 2);break;
            case 0x0023CF50:writesubcall("sub_5BF50_load_psxdata_0x0023CF50\n", 2);break;
            case 0x0023D1B0:writesubcall("sub_5C1B0_set_any_variables2_0x0023D1B0\n", 2);break;
            case 0x00235200:writesubcall("sub_54200_create_user_directiores_0x00235200\n", 2);break;
            case 0x001f11c0:writesubcall("sub_101C0_0x001f11c0\n", 2);break;
            case 0x0023d430:writesubcall("sub_5C430_multi_allocation_0x0023d430\n", 2);break;
            case 0x00227DD0:writesubcall("sub_46DD0_init_sound_and_music_0x00227DD0\n", 2);break;

            case 0x00229350:writesubcall("sub_48350_0x00229350\n", 2);break;
            case 0x00257930:writesubcall("sub_76930_0x00257930\n", 2);break;

            case 0x0025CEC0:writesubcall("sub_7BEC0_0x0025CEC0\n", 3);break;
            case 0x0024FDB0:writesubcall("sub_6EDB0_set_mouse_position_by_res_0x0024FDB0\n", 3);break;
            case 0x00262DB0:writesubcall("sub_81DB0_0x00262DB0\n", 3);break;
            case 0x00257A40:writesubcall("sub_76A40_0x00257A40\n", 3);break;

            case 0x0025BC00:writesubcall("sub_7AC00_0x0025BC00\n", 4);break;
            case 0x002589E0:writesubcall("sub_779E0_0x002589E0\n", 4);break;
            case 0x0025BDE0:writesubcall("sub_7ADE0_0x0025BDE0\n", 4);break;

            case 0x235600:writesubcall("sub_54600_mouse_reset\n", 5);break;
            case 0x24FBF0:writesubcall("sub_6EBF0\n", 5);break;

            case 0x0025c5a0:writesubcall("sub_7B5A0_0x0025c5a0\n", 6);break;
            case 0x0026dd27:writesubcall("sub_8CD27_0x0026dd27\n", 6);break;
            case 0x0027B166:writesubcall("unknown_libname_2_findfirst_0x0027B166\n", 6);break;
            case 0x0027b1b3:writesubcall("unknown_libname_4_0x0027b1b3\n", 6);break;



            case 0x00222a90:writesubcall("sub_41A90_VGA_pallette_install\n", 10);break;
            }
        }
        }
    }

    callcount++;
};

long testcount = 0;


long xcounter = 0;
int engine_call(bool use32, Bitu selector, Bitu offset, Bitu oldeip) {
    if (callindex > 0)
    {
        call_write(selector, offset);
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
        switch (offset) {
        case 0xf023C8D0: {//tisk SETTING UP LEVELS - yy // přes int 10
                //myWriteOut("Video refresh rate.\n\n");
                /*CConsoleLogger another_console;
                another_console.Create("This is the first console");
                another_console.printf("WOW !!! COOLL !!! another console ???");*/
                /*support_begin();
                pre_160_0005B8D0();
                sub_5B8D0();
                post_160_0005B8D0();
                support_end();
                myWriteOut("TomTom\n\n");*/
                //if(engprg==NULL)engprg = new ENGPRG;
                //engprg->Run();
                //WriteOut("Video refresh rate.\n\n");
                //DEBUG_EnableDebugger();
                break;
            }
            case 0x00236f70:{//181000 1A1000 rozdila a rozdil b//main
                //saveactstate();
                begin_write();
                callindex = 1;
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
            case 0x256200b: {
                //saveactstate();
                //DEBUG_EnableDebugger();
                break;
            }
            case 0x257930a: {

                /*fopen_s(&fptestep, findname, "a+");
                fprintf(fptestep, "0x222a90PAL%04X:%08X\n", 0x0168, reg_esp + 0x10);
                fclose(fptestep);*/
                //saveactstate();
                //if(xcounter>87)
                //DEBUG_EnableDebugger();
                xcounter++;
                break;
            }
            case 0x270935: {
                //case 0x264b50: {
                //case 0x23cf50: {
                //case 0x00271D6E: {
                //case 0x00227830: {
                //case 0x00279b2c: {
                //case 0x001f11c0: {
                //case 0x0027B32d: {//tady   v3 = (x_BYTE *)x_DWORD_E9C30;//002bac30 //je to v 23c8d0, 23cf50, 265250, 27B32d, nekde u 27b381,27b453
                //case 0x00265250: {
                //case 0x0023CF50: {
                //case 0x0023C8D0: {
                //case 0x0023D1B0: {
                //case 0x00237210: {
                //case 0x00266070: {
                //case 0x00264CD0: {
                //case 0x00234E60: {
                //case 0x00235200: {
                //case 0x00267A00: {                
                if (reg_edx & 0xff00)
                {
                    saveactstate();
                    DEBUG_EnableDebugger();
                }
                break;
            }
            case 0x00279D52: {//int386
             /*   
                Bit32u var1 = mem_readd(SegPhys(ss) + reg_esp + 0x4);
                Bit32u var2 = mem_readd(SegPhys(ss) + reg_esp + 0x8);
                Bit32u var3 = mem_readd(SegPhys(ss) + reg_esp + 0xc);

                if (var1 == 0x21)
                {
                    pre_160_00279D52();
                    //Bit32u var1 = mem_readd(SegPhys(ss) + 0x14);
                    //Bit32u var2 = mem_readd(SegPhys(ss) + 0x18);
                    //Bit32u var3 = mem_readd(SegPhys(ss) + 0x1C);

                    REGS regs1;
                    regs1.edx = mem_readd(SegPhys(ds) + var2);
                    regs1.ecx = mem_readd(SegPhys(ds) + var2 + 0x4);
                    regs1.ebx = mem_readd(SegPhys(ds) + var2 + 0x8);
                    regs1.eax = mem_readd(SegPhys(ds) + var2 + 0xc);
                    regs1.edi = mem_readd(SegPhys(ds) + var2 + 0x10);
                    regs1.esi = mem_readd(SegPhys(ds) + var2 + 0x14);
                    regs1.cflag = mem_readd(SegPhys(ds) + var2 + 0x18);
                    REGS regs2;
                    regs2.edx = mem_readd(SegPhys(ds) + var3);
                    regs2.ecx = mem_readd(SegPhys(ds) + var3 + 0x4);
                    regs2.ebx = mem_readd(SegPhys(ds) + var3 + 0x8);
                    regs2.eax = mem_readd(SegPhys(ds) + var3 + 0xc);
                    regs2.edi = mem_readd(SegPhys(ds) + var3 + 0x10);
                    regs2.esi = mem_readd(SegPhys(ds) + var3 + 0x14);
                    regs2.cflag = mem_readd(SegPhys(ds) + var3 + 0x18);

                    //(SegPhys(ss) + (new_esp
                    //esp+14,ebp+4
                    //esp + 18, ebp + 8
                    //esp + 1c, ebp + c
                    int386(var1, &regs1, &regs2);
                    post_160_00279D52();
                }
                else
                    if (var1 == 0x31)
                    {                        
                        if (testcount < 1000)
                        {
                            pre_160_00279D52();
                            //Bit32u var1 = mem_readd(SegPhys(ss) + 0x14);
                            //Bit32u var2 = mem_readd(SegPhys(ss) + 0x18);
                            //Bit32u var3 = mem_readd(SegPhys(ss) + 0x1C);

                            REGS regs1;
                            regs1.edx = mem_readd(SegPhys(ds) + var2);
                            regs1.ecx = mem_readd(SegPhys(ds) + var2 + 0x4);
                            regs1.ebx = mem_readd(SegPhys(ds) + var2 + 0x8);
                            regs1.eax = mem_readd(SegPhys(ds) + var2 + 0xc);
                            regs1.edi = mem_readd(SegPhys(ds) + var2 + 0x10);
                            regs1.esi = mem_readd(SegPhys(ds) + var2 + 0x14);
                            regs1.cflag = mem_readd(SegPhys(ds) + var2 + 0x18);
                            REGS regs2;
                            regs2.edx = mem_readd(SegPhys(ds) + var3);
                            regs2.ecx = mem_readd(SegPhys(ds) + var3 + 0x4);
                            regs2.ebx = mem_readd(SegPhys(ds) + var3 + 0x8);
                            regs2.eax = mem_readd(SegPhys(ds) + var3 + 0xc);
                            regs2.edi = mem_readd(SegPhys(ds) + var3 + 0x10);
                            regs2.esi = mem_readd(SegPhys(ds) + var3 + 0x14);
                            regs2.cflag = mem_readd(SegPhys(ds) + var3 + 0x18);

                            //first_shell->WriteOut("\nint31:%08X:%08X\n");
                            //first_shell->WriteOut("\nint31\n");

                            //(SegPhys(ss) + (new_esp
                            //esp+14,ebp+4
                            //esp + 18, ebp + 8
                            //esp + 1c, ebp + c
                            //if(callindex>0)DEBUG_EnableDebugger();
                            if (regs1.edx == 0x100) { /return 0; }//Allocate DOS Memory Block
                            if (regs1.edx == 0x300) { return 0; }//Simulate Real Mode Interrupt
                            if (regs1.edx == 0x101) { return 0; }//Free DOS Memory Block
                            if (regs1.edx == 0x600) { post_160_00279D52();return 0; }//Lock Linear Region
                            int386(var1, &regs1, &regs2);
                            post_160_00279D52();

                            long testx = mem_readd(SegPhys(ds) + var2);
                            if((testx!=0x100)&& (testx != 0x300))
                                int xx = 1;
                            testcount++;
                        }
                        //else DEBUG_EnableDebugger();
                    }
                    //else DEBUG_EnableDebugger();
                    
                //int 10 F00 0 300 11   5003 0 300 11
                
                //Category: video
                //INT 10 - VESA SuperVGA BIOS (VBE) - GET SuperVGA INFORMATION

                //AX = 4F00h
                //ES:DI -> buffer for SuperVGA information (see #00077)
                //Return: AL = 4Fh if function supported
                //AH = status
                //00h successful
                //ES:DI buffer filled
                //01h failed
                //---VBE v2.0---
                //02h function not supported by current hardware configuration
                //03h function invalid in current video mode
                //Desc:	determine whether VESA BIOS extensions are present and the capabilities
                //supported by the display adapter
                //SeeAlso: AX=4E00h,AX=4F01h,AX=7F00h"SOLLEX",AX=A00Ch
                //Index:	installation check;VESA SuperVGA
                

                return 1;*/
                break;
            }
              
        }
        break;
    }
    }
    
    return 0;
}


