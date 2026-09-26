/*
 *  mc2check.h - hledani nekonzistenci v Magic Carpet 2
 *
 *  Kazdy level se spusti dvakrat, po kazdem snimku se udela otisk (CRC32)
 *  sledovanych struktur a otisky obou behu se porovnaji. Vse se ridi
 *  promennymi prostredi, aby slo behy skriptovat:
 *
 *    MC2CHK         1 = zapnout harness
 *    MC2CHK_LEVEL   cislo levelu (vychozi 1)
 *    MC2CHK_FRAMES  kolik snimku zaznamenat (vychozi 500)
 *    MC2CHK_OUT     vystupni soubor s otisky (vychozi mc2chk.txt)
 *    MC2CHK_DUMP    adresar; kdyz je nastaveny, ulozi se i syrove bajty
 *                   sledovanych oblasti kazdeho snimku
 *    MC2CHK_PLAY    soubor se zaznamem vstupu (prazdne = zadny vstup)
 *    MC2CHK_SEQ     adresar; kdyz je nastaveny, pise se kazdy snimek i do
 *                   sequence-002285FF-*.bin ve formatu regresnich testu remc2
 *    MC2CHK_SEQZ    1 = misto .bin jen porovnavane oblasti (mapy, D41A0) do .binz,
 *                   zmeny proti predchozimu snimku
 *    MC2CHK_SEQ_SCREEN  1 = v .binz i obrazovka
 *    MC2CHK_NOINPUT 1 = ze zaznamu nevkladat vstupy hracu
 *    MC2CHK_NOSPELLS 1 = ze zaznamu nenastavovat kouzla
 *    MC2CHK_WATCH   linearni adresa (hex); kazda zmena hodnoty se zapise i s EIP
 *                   instrukce, ktera ji zmenila (IDA adresa = EIP - 0x1E1000)
 *    MC2CHK_WATCH_SIZE  1, 2 nebo 4 bajty (vychozi 1)
 *    MC2CHK_TRACE   seznam EIP (hex, oddelene carkou, max 8): pri kazdem
 *                   prichodu na adresu se zapisou registry, odkud se prislo
 *                   (EIP predchozi instrukce) a 16 B pameti na [eax] a [ecx]
 *    MC2CHK_TRACE_FRAME  jen v tomto snimku (-1 = vsechny)
 *    MC2CHK_TRACE_EAX    jen kdyz eax ma tuto hodnotu (hex, prazdne = vzdy)
 *    MC2CHK_POKE    "adresa=hodnota,..." (hex, po bajtech): zapise se jednou,
 *                   jakmile je level vybrany - pro overovani, na cem beh zavisi
 *    MC2CHK_RAWSTAGEPTR  1 = nechat sub_12780 cist ukazatel ze StageVars2 tak,
 *                   jak ho cte original (i kdyz uz je to offset - viz nize)
 *    MC2CHK_WD      watchdog: max. poctu kroku CPU nez level nabehne
 *                   (vychozi 4000000000)
 *
 *  Sledovane oblasti odpovidaji compare_with_sequence() v remc2.
 */

#ifndef MC2CHECK_H
#define MC2CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <string>
#include "sequence_codec.h"
#if defined(WIN32) || defined(_WIN32)
#include <process.h>   /* _exit() */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
/* WIN32_LEAN_AND_MEAN tuhle deklaraci vynecha, doplnujeme ji rucne
 * (funkce je exportovana z kernel32.dll) */
extern "C" USHORT NTAPI RtlCaptureStackBackTrace(ULONG, ULONG, PVOID*, PULONG);
#define MC2CHK_STACKTRACE 1
#endif

/* ---- sledovane oblasti ---------------------------------------------- */

#define MC2CHK_MAPBASE   0x2DC4E0u   /* blok map, celkem 0x70000 B        */
#define MC2CHK_D41A0     0x356038u   /* herni stav, 0x36E16 B             */

struct mc2chk_region {
    const char* name;
    Bit32u      base;
    Bit32u      offset;
    Bit32u      size;
};

static const mc2chk_region mc2chk_regions[] = {
    { "terrain", MC2CHK_MAPBASE, 0x00000, 0x10000 },  /* mapTerrainType_10B4E0 */
    { "height",  MC2CHK_MAPBASE, 0x10000, 0x10000 },  /* mapHeightmap_11B4E0   */
    { "shading", MC2CHK_MAPBASE, 0x20000, 0x10000 },  /* mapShading_12B4E0     */
    { "angle",   MC2CHK_MAPBASE, 0x30000, 0x10000 },  /* mapAngle_13B4E0       */
    { "entidx",  MC2CHK_MAPBASE, 0x50000, 0x20000 },  /* mapEntityIndex_15B4E0 */
    { "d41a0",   MC2CHK_D41A0,   0x00000, 0x36E16 },  /* D41A0_0               */
};
#define MC2CHK_NREGIONS (int)(sizeof(mc2chk_regions)/sizeof(mc2chk_regions[0]))

/* ---- stav ------------------------------------------------------------ */

static bool        mc2chk_on        = false;
static int         mc2chk_level     = 1;
static int         mc2chk_frames    = 500;
static int         mc2chk_frame     = 0;
static bool        mc2chk_started   = false;   /* level uz byl vybran */
static long long   mc2chk_watchdog  = 4000000000LL;
static int         mc2chk_testexit  = 0;   /* sebetest zachytu konce procesu */
static char        mc2chk_outname[512];
static char        mc2chk_dumpdir[512];
static char        mc2chk_playfile[512];
static FILE*       mc2chk_fp        = NULL;
static Bit32u      mc2chk_crctab[256];
static bool        mc2chk_crcready  = false;

/* ---- sekvence ve formatu remc2 ----------------------------------------
 * Tytez soubory, jake pise writeseqall(0x2285FF) a cte compare_with_sequence()
 * v remc2: sequence-002285FF-<adresa>.bin, snimky za sebou od prvniho snimku
 * herni smycky. Oblasti a jejich poradi jsou prevzate z writeseqall().
 */
struct mc2chk_seqfile { Bit32u base; Bit32u size; FILE* fp; };
static mc2chk_seqfile mc2chk_seq[] = {
    { 0x2DC4E0u, 0x70000u,     NULL },   /* mapy                  */
    { 0x356038u, 0x36E16u,     NULL },   /* D41A0_0               */
    { 0x3AA0A4u, 320u * 200u,  NULL },   /* obrazovka             */
    { 0x3514B0u, 0xABu,        NULL },
    { 0x2B3A74u, 0xC4Eu,       NULL },   /* str_E2A74             */
    { 0x34C4E0u, 0x2u,         NULL },
};
#define MC2CHK_NSEQ (int)(sizeof(mc2chk_seq)/sizeof(mc2chk_seq[0]))
static char  mc2chk_seqdir[512];
/* pro rozdeleni, co z prehravani meni hru: vstupy, kouzla, nebo nic z toho */
static bool  mc2chk_noinput  = false;
static bool  mc2chk_nospells = false;
/* hlidani zapisu do jedne adresy */
static Bit32u mc2chk_watch_addr = 0;
static int    mc2chk_watch_size = 1;
static bool   mc2chk_watch_have = false;
static Bit32u mc2chk_watch_last = 0;
static Bit32u mc2chk_watch_prev_eip = 0;
static int    mc2chk_watch_hits = 0;
static int    mc2chk_watch_from = 0;   /* zapisovat az od tohoto snimku */
/* vypis registru na zvolenych adresach */
static Bit32u mc2chk_trace_eip[8];
static int    mc2chk_trace_hits[8];
static int    mc2chk_trace_count = 0;
static int    mc2chk_trace_frame = -1;
static bool   mc2chk_trace_eax_on = false;
static Bit32u mc2chk_trace_eax = 0;
static Bit32u mc2chk_trace_prev_eip = 0;
/* jednorazove zapisy do pameti */
static Bit32u mc2chk_poke_addr[16];
static Bit8u  mc2chk_poke_val[16];
static int    mc2chk_poke_count = 0;
static bool   mc2chk_poke_done = false;
/* kontrola rozsahu ukazatele v sub_12780 jako v remc2 */
static bool   mc2chk_rawstageptr = false;
static int    mc2chk_stageptr_hits = 0;
static Bit8u mc2chk_seqbuf[0x70000];
/* .binz: "MC2SEQZ1", uint32 velikost snimku; za snimek uint32 delka a useky zmen proti
 * predchozimu snimku (prvni proti nulam): varint stejnych, varint zmenenych, zmenene bajty */
static bool   mc2chk_seqz = false;
static bool   mc2chk_seqscreen = false;
static Bit8u* mc2chk_seqprev[8];
/* The run writes "MC2SEQZ1" into <file>.z1tmp frame by frame; at its end the file becomes
 * <file> in "MC2SEQZ4" (sequence_codec.h, the one format remc2 gets from DOSBox). */
static char   mc2chk_seqpath[8][600];
static Bit8u  mc2chk_seqout[2 * 0x70000 + 64];

static Bit32u mc2chk_varint(Bit8u* out, Bit32u v) {
    Bit32u n = 0;
    for (; v >= 0x80; v >>= 7) out[n++] = (Bit8u)(v | 0x80);
    out[n++] = (Bit8u)v;
    return n;
}

static void mc2chk_seqz_frame(FILE* fp, const Bit8u* cur, Bit8u* prev, Bit32u size) {
    Bit8u* out = mc2chk_seqout;
    Bit32u n = 4, i = 0;
    while (i < size) {
        Bit32u start = i;
        while (i < size && cur[i] == prev[i]) i++;
        n += mc2chk_varint(out + n, i - start);
        start = i;
        while (i < size) {
            if (cur[i] != prev[i]) { i++; continue; }
            Bit32u j = i;
            while (j < size && j < i + 4 && cur[j] == prev[j]) j++;
            if (j < size && j < i + 4) { i = j; continue; }   /* kratka mezera se prepise */
            break;
        }
        n += mc2chk_varint(out + n, i - start);
        memcpy(out + n, cur + start, i - start);
        n += i - start;
    }
    const Bit32u len = n - 4;
    memcpy(out, &len, 4);
    fwrite(out, 1, n, fp);
    memcpy(prev, cur, size);
}

static void mc2chk_crcinit(void) {
    for (Bit32u i = 0; i < 256; i++) {
        Bit32u c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        mc2chk_crctab[i] = c;
    }
    mc2chk_crcready = true;
}

static const char* mc2chk_env(const char* name, const char* fallback) {
    const char* v = getenv(name);
    if (v == NULL || *v == '\0') return fallback;
    return v;
}

static void mc2chk_init(void) {
    const char* on = getenv("MC2CHK");
    if (on == NULL || *on == '\0' || *on == '0') return;

    mc2chk_on = true;
    mc2chk_crcinit();

    mc2chk_level  = atoi(mc2chk_env("MC2CHK_LEVEL", "1"));
    mc2chk_frames = atoi(mc2chk_env("MC2CHK_FRAMES", "500"));
    mc2chk_watchdog = _atoi64(mc2chk_env("MC2CHK_WD", "4000000000"));
    mc2chk_testexit = atoi(mc2chk_env("MC2CHK_TESTEXIT", "0"));
    strncpy(mc2chk_outname, mc2chk_env("MC2CHK_OUT", "mc2chk.txt"), sizeof(mc2chk_outname) - 1);
    strncpy(mc2chk_dumpdir, mc2chk_env("MC2CHK_DUMP", ""), sizeof(mc2chk_dumpdir) - 1);
    strncpy(mc2chk_playfile, mc2chk_env("MC2CHK_PLAY", ""), sizeof(mc2chk_playfile) - 1);
    strncpy(mc2chk_seqdir, mc2chk_env("MC2CHK_SEQ", ""), sizeof(mc2chk_seqdir) - 1);
    mc2chk_seqz = atoi(mc2chk_env("MC2CHK_SEQZ", "0")) != 0;
    mc2chk_seqscreen = atoi(mc2chk_env("MC2CHK_SEQ_SCREEN", "0")) != 0;
    mc2chk_noinput  = atoi(mc2chk_env("MC2CHK_NOINPUT", "0")) != 0;
    mc2chk_nospells = atoi(mc2chk_env("MC2CHK_NOSPELLS", "0")) != 0;
    mc2chk_rawstageptr = atoi(mc2chk_env("MC2CHK_RAWSTAGEPTR", "0")) != 0;
    mc2chk_watch_addr = (Bit32u)strtoul(mc2chk_env("MC2CHK_WATCH", "0"), NULL, 16);
    mc2chk_watch_size = atoi(mc2chk_env("MC2CHK_WATCH_SIZE", "1"));
    mc2chk_watch_from = atoi(mc2chk_env("MC2CHK_WATCH_FROM", "0"));
    if (mc2chk_watch_size != 2 && mc2chk_watch_size != 4) mc2chk_watch_size = 1;
    {
        char list[512];
        strncpy(list, mc2chk_env("MC2CHK_TRACE", ""), sizeof(list) - 1);
        list[sizeof(list) - 1] = '\0';
        for (char* tok = strtok(list, ","); tok != NULL && mc2chk_trace_count < 8; tok = strtok(NULL, ","))
            mc2chk_trace_eip[mc2chk_trace_count++] = (Bit32u)strtoul(tok, NULL, 16);
        mc2chk_trace_frame = atoi(mc2chk_env("MC2CHK_TRACE_FRAME", "-1"));
        const char* e = mc2chk_env("MC2CHK_TRACE_EAX", "");
        mc2chk_trace_eax_on = (*e != '\0');
        mc2chk_trace_eax = (Bit32u)strtoul(e, NULL, 16);
    }
    {
        char list[512];
        strncpy(list, mc2chk_env("MC2CHK_POKE", ""), sizeof(list) - 1);
        list[sizeof(list) - 1] = '\0';
        for (char* tok = strtok(list, ","); tok != NULL && mc2chk_poke_count < 16; tok = strtok(NULL, ",")) {
            char* eq = strchr(tok, '=');
            if (eq == NULL) continue;
            *eq = '\0';
            mc2chk_poke_addr[mc2chk_poke_count] = (Bit32u)strtoul(tok, NULL, 16);
            mc2chk_poke_val[mc2chk_poke_count] = (Bit8u)strtoul(eq + 1, NULL, 16);
            mc2chk_poke_count++;
        }
    }

    mc2chk_fp = fopen(mc2chk_outname, "wt");
    if (mc2chk_fp == NULL) {
        fprintf(stderr, "MC2CHK: nelze otevrit %s\n", mc2chk_outname);
        exit(3);
    }
    fprintf(mc2chk_fp, "# mc2chk level=%d frames=%d\n", mc2chk_level, mc2chk_frames);
    fprintf(mc2chk_fp, "# regions:");
    for (int i = 0; i < MC2CHK_NREGIONS; i++)
        fprintf(mc2chk_fp, " %s@%08X+%05X:%05X", mc2chk_regions[i].name,
                mc2chk_regions[i].base, mc2chk_regions[i].offset, mc2chk_regions[i].size);
    fprintf(mc2chk_fp, "\n# frame rand clock");
    for (int i = 0; i < MC2CHK_NREGIONS; i++) {
        fprintf(mc2chk_fp, " %s", mc2chk_regions[i].name);
        if (mc2chk_regions[i].base == MC2CHK_D41A0) fprintf(mc2chk_fp, " %sm", mc2chk_regions[i].name);
    }
    fprintf(mc2chk_fp, "\n");
    if (mc2chk_watch_addr != 0)
        fprintf(mc2chk_fp, "# hlidani zapisu: 0x%08X, %d B\n", mc2chk_watch_addr, mc2chk_watch_size);
    for (int i = 0; i < mc2chk_trace_count; i++)
        fprintf(mc2chk_fp, "# vypis registru na EIP 0x%08X (IDA 0x%08X)%s\n", mc2chk_trace_eip[i],
                mc2chk_trace_eip[i] - 0x1E1000u, i == mc2chk_trace_count - 1 ? "" : ",");
    if (mc2chk_playfile[0] != '\0')
        fprintf(mc2chk_fp, "# zaznam vstupu: %s%s%s\n", mc2chk_playfile,
                mc2chk_noinput ? " (BEZ vstupu)" : "", mc2chk_nospells ? " (BEZ kouzel)" : "");
    if (mc2chk_seqdir[0] != '\0') {
        for (int i = 0; i < MC2CHK_NSEQ; i++) {
            const Bit32u base = mc2chk_seq[i].base;
            if (mc2chk_seqz && base != 0x2DC4E0u && base != 0x356038u && !(mc2chk_seqscreen && base == 0x3AA0A4u))
                continue;
            char path[600];
            sprintf(path, "%s/sequence-002285FF-%08X.%s", mc2chk_seqdir, base, mc2chk_seqz ? "binz" : "bin");
            strcpy(mc2chk_seqpath[i], path);
            if (mc2chk_seqz)
                strcat(path, ".z1tmp");
            mc2chk_seq[i].fp = fopen(path, "wb");
            if (mc2chk_seq[i].fp == NULL) {
                fprintf(stderr, "MC2CHK: nelze otevrit %s\n", path);
                exit(3);
            }
            if (mc2chk_seqz) {
                fwrite("MC2SEQZ1", 1, 8, mc2chk_seq[i].fp);
                fwrite(&mc2chk_seq[i].size, 4, 1, mc2chk_seq[i].fp);
                mc2chk_seqprev[i] = (Bit8u*)calloc(mc2chk_seq[i].size, 1);
            }
        }
        fprintf(mc2chk_fp, "# sekvence pro remc2: %s\n", mc2chk_seqdir);
    }
    fflush(mc2chk_fp);
}

/* Kolik kroku CPU ubehlo - plni se z enginestep(), at je v zaznamu videt,
 * kde se beh zdrzel nebo zasekl. */
static long long mc2chk_steps = 0;

static void mc2chk_note(const char* what) {
    if (!mc2chk_on || mc2chk_fp == NULL) return;
    fprintf(mc2chk_fp, "# %s (krok %lld)\n", what, mc2chk_steps);
    fflush(mc2chk_fp);
}

/* Jednorazova znacka faze - vola se na kazdem kroku, proto ta pamet. */
static void mc2chk_stage(int idx, const char* what) {
    static bool seen[8] = { false, false, false, false, false, false, false, false };
    if (!mc2chk_on || idx < 0 || idx >= 8 || seen[idx]) return;
    seen[idx] = true;
    mc2chk_note(what);
}

static bool mc2chk_finishing = false;

/* The sequences closed; the .binz ones from <file>.z1tmp into <file> as "MC2SEQZ4",
 * kept only when they give the same frames. */
static void mc2chk_seq_close(void) {
    for (int i = 0; i < MC2CHK_NSEQ; i++) {
        if (mc2chk_seq[i].fp == NULL) continue;
        fclose(mc2chk_seq[i].fp);
        mc2chk_seq[i].fp = NULL;
        if (!mc2chk_seqz) continue;
        const std::string final_path = mc2chk_seqpath[i];
        const std::string temp_path = final_path + ".z1tmp";
        if (seqz::ConvertZ1ToZ4(temp_path, final_path) && seqz::SameFrames(temp_path, final_path))
            remove(temp_path.c_str());
        else {
            remove(final_path.c_str());
            fprintf(stderr, "MC2CHK: %s not converted to MC2SEQZ4, the frames stay in %s\n",
                    final_path.c_str(), temp_path.c_str());
        }
    }
}

/* Konec behu - at uz radny nebo pres watchdog. */
static void mc2chk_finish(int code, const char* why) {
    mc2chk_finishing = true;
    if (mc2chk_fp != NULL) {
        fprintf(mc2chk_fp, "# konec: %s (snimku %d z %d, krok %lld)\n",
                why, mc2chk_frame, mc2chk_frames, mc2chk_steps);
        fclose(mc2chk_fp);
        mc2chk_fp = NULL;
    }
    mc2chk_seq_close();
    exit(code);
}

/* ---- zachyt neocekavaneho konce procesu ------------------------------
 * Behy obcas skonci navratovym kodem -1 bez jedine hlasky. Tohle rozlisi,
 * jestli slo o radne exit() nekde v DOSBoxu, nebo o skutecny pad.
 */
/* Kdo zavolal exit()? Zasobnik se bere primo z obsluhy atexit(), takze
 * ramec volajiciho exit() v nem je. Bez tohohle se o miste konce jen hada. */
static void mc2chk_dump_stack(void) {
#ifdef MC2CHK_STACKTRACE
    void* frames[40];
    USHORT n = CaptureStackBackTrace(0, 40, frames, NULL);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    bool syms = (SymInitialize(proc, NULL, TRUE) != FALSE);

    char sbuf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)sbuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;

    for (USHORT i = 0; i < n; i++) {
        DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
        fprintf(mc2chk_fp, "#   [%2u] %016llX", (unsigned)i, (unsigned long long)addr);
        DWORD64 disp = 0;
        if (syms && SymFromAddr(proc, addr, &disp, sym))
            fprintf(mc2chk_fp, "  %s+0x%llX", sym->Name, (unsigned long long)disp);
        IMAGEHLP_LINE64 line;
        DWORD ldisp = 0;
        line.SizeOfStruct = sizeof(line);
        if (syms && SymGetLineFromAddr64(proc, addr, &ldisp, &line))
            fprintf(mc2chk_fp, "  (%s:%lu)", line.FileName, (unsigned long)line.LineNumber);
        fprintf(mc2chk_fp, "\n");
    }
    if (syms) SymCleanup(proc);
#endif
}

#ifdef MC2CHK_STACKTRACE
/* Obsluha neosetrene vyjimky. signal(SIGSEGV) na MSVC hardwarove chyby
 * nezachyti (ty jdou pres SEH), tohle ano - a to i ve vedlejsich vlaknech. */
static LONG WINAPI mc2chk_seh(EXCEPTION_POINTERS* ep) {
    if (mc2chk_fp != NULL) {
        fprintf(mc2chk_fp, "# PAD: vyjimka %08lX na adrese %p"
                           " (snimku %d z %d, krok %lld)\n",
                (unsigned long)ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress,
                mc2chk_frame, mc2chk_frames, mc2chk_steps);
        mc2chk_dump_stack();
        fclose(mc2chk_fp);
        mc2chk_fp = NULL;
    }
    return EXCEPTION_EXECUTE_HANDLER;   /* at proces skonci, ale se zaznamem */
}
#endif

static void mc2chk_atexit(void) {
    if (!mc2chk_finishing)
        mc2chk_seq_close();//the frames written so far stay usable, as with MC2SEQZ1
    if (mc2chk_finishing || mc2chk_fp == NULL) return;
    fprintf(mc2chk_fp, "# NECEKANY KONEC: proces skoncil pres exit()/return,"
                       " ne nasim zpusobem (snimku %d z %d, krok %lld)\n",
            mc2chk_frame, mc2chk_frames, mc2chk_steps);
    mc2chk_dump_stack();
    fclose(mc2chk_fp);
    mc2chk_fp = NULL;
}

static void mc2chk_signal(int sig) {
    const char* name = "?";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV - pristup mimo pamet"; break;
        case SIGABRT: name = "SIGABRT - abort()"; break;
        case SIGILL:  name = "SIGILL - neplatna instrukce"; break;
        case SIGFPE:  name = "SIGFPE - chyba v aritmetice"; break;
        default: break;
    }
    if (mc2chk_fp != NULL) {
        fprintf(mc2chk_fp, "# PAD: %s (snimku %d z %d, krok %lld)\n",
                name, mc2chk_frame, mc2chk_frames, mc2chk_steps);
        mc2chk_dump_stack();
        fclose(mc2chk_fp);
        mc2chk_fp = NULL;
    }
    _exit(10 + sig);
}

/* ---- vyjimky pri porovnavani D41A0 -----------------------------------
 * Prevzato 1:1 z remc2, funkce test_D41A0_id_pointer()
 * (remc2/engine/engine_support.cpp). Navratove hodnoty:
 *   0 - porovnavat bajt po bajtu
 *   1 - ukazatel: porovnava se jen jestli je nulovy nebo nenulovy
 *       (adresy se mezi behy lisi, obsazenost ne), bere 4 bajty
 *   2 - vynechat uplne (hodiny, texty, pozice mysi, hudba)
 *
 * Pozn.: nekolik vyjimek typu 2 je v remc2 zapsanych jen pro hrace 0
 * (0x2BFA texty, 0x2F79, 0x2FBD, 0x2FC4, 0x2FD8 pozice mysi); hodiny jsou
 * vypsane pro vsech osm. Necham to stejne jako remc2, at je to porovnatelne.
 */
#define MC2CHK_CLOCK_OFF(player) (0x314Du + (player) * 0x84Cu)

/* Smycky z remc2 jsou tu prepsane pres modulo - jinak by na kazdy bajt
 * pripadlo az 1000 iteraci a jeden snimek by trval sekundu. Pokryta
 * mnozina offsetu je stejna. */
static int mc2chk_d41a0_class(Bit32u a) {
    /* text hlaseni vsech hracu (CurrentNotificationText_0x01c) - zavisi na jazyku; stejne jako remc2 */
    if (a >= 0x2bfau && a < 0x2bfau + 8u * 0x84Cu && (a - 0x2bfau) % 0x84Cu < 49u) return 2;
    if (a == 0x2f79)                    return 2;   /* text */
    if (a == 0x2fbd)                    return 2;   /* handle click button */
    if (a == 0x2fc4)                    return 2;   /* event */
    if (a >= 0x2fd8 && a < 0x2fdc)      return 2;   /* pozice mysi (position_backup_20) */

    /* hodiny hracu 1-8: 0x314D + p*0x84C, 4 B */
    if (a >= 0x314du && a < 0x314du + 8u * 0x84Cu && (a - 0x314du) % 0x84Cu < 4u)
        return 2;

    if (a == 0x235) return 2;                       /* hudba */

    if (a >= 0x246 && a < 0x2186) return 1;         /* pole ukazatelu na entity */

    if (a == 0x36552 || a == 0x3655c || a == 0x3655f || a == 0x36566 ||
        a == 0x36570 || a == 0x36608 || a == 0x36620 || a == 0x36df6) return 1;
    if (a >= 0x36628 && a < 0x36630) return 1;

    /* 0x3664C+0xa + 39*k, k = 0..49 (v remc2 dve smycky, ktere se prekryvaji) */
    if (a >= 0x36656u && a <= 0x36656u + 39u * 49u && (a - 0x36656u) % 39u == 0u)
        return 1;

    /* ukazatele v entitach 0x6E8E: offsety 0xA0..0xA8 kazde ze 1000 entit */
    if (a >= 0x6f2eu && a < 0x6f2eu + 168u * 0x3e8u && (a - 0x6f2eu) % 168u < 9u)
        return 1;

    return 0;
}

/* Oblast se nacte do bufferu, at se pamet cte jen jednou. */
static Bit8u mc2chk_buf[0x36E16];

static Bit32u mc2chk_crc_of(const Bit8u* p, Bit32u n) {
    Bit32u crc = 0xFFFFFFFFu;
    for (Bit32u i = 0; i < n; i++)
        crc = mc2chk_crctab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* Otisk D41A0 s vyjimkami podle remc2. */
static Bit32u mc2chk_crc_masked(const Bit8u* p, Bit32u n) {
    Bit32u crc = 0xFFFFFFFFu;
    Bit32u i = 0;
    while (i < n) {
        int c = mc2chk_d41a0_class(i);
        if (c == 2) { i++; continue; }
        if (c == 1) {
            Bit8u b = 0;
            if (i + 3 < n) {
                Bit32u v = (Bit32u)p[i] | ((Bit32u)p[i+1] << 8) |
                           ((Bit32u)p[i+2] << 16) | ((Bit32u)p[i+3] << 24);
                b = v ? 1 : 0;
            }
            crc = mc2chk_crctab[(crc ^ b) & 0xFF] ^ (crc >> 8);
            i += 4;
            continue;
        }
        crc = mc2chk_crctab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        i++;
    }
    return crc ^ 0xFFFFFFFFu;
}

/* Otisk jedne oblasti + volitelne syrovy vypis.
 * Pro D41A0 se navic pocita otisk s vyjimkami (crc_masked). */
static Bit32u mc2chk_region_crc(const mc2chk_region& r, FILE* dump, Bit32u* crc_masked) {
    Bit32u addr = r.base + r.offset;
    /* MEM_BlockRead je proti mem_readb po bajtech radove rychlejsi a pri
     * 3000 snimcich to dela rozdil hodin */
    MEM_BlockRead((PhysPt)addr, mc2chk_buf, r.size);
    if (dump != NULL) fwrite(mc2chk_buf, 1, r.size, dump);
    if (crc_masked != NULL) *crc_masked = mc2chk_crc_masked(mc2chk_buf, r.size);
    return mc2chk_crc_of(mc2chk_buf, r.size);
}

/* Vola se na konci kazdeho snimku hry (EIP 0x2285FF). */
static void mc2chk_on_frame(void) {
    if (!mc2chk_on || !mc2chk_started) return;

    FILE* dump = NULL;
    if (mc2chk_dumpdir[0] != '\0') {
        char path[600];
        sprintf(path, "%s/frame_%05d.bin", mc2chk_dumpdir, mc2chk_frame);
        dump = fopen(path, "wb");
    }

    /* RNG hry a herni hodiny hrace 0 - at je hned videt, jestli rozdily
     * nejsou jen od jineho seedu / jineho realneho casu */
    Bit32u rnd = mem_readd(MC2CHK_D41A0 + 0x8);
    Bit32u clk = mem_readd(MC2CHK_D41A0 + MC2CHK_CLOCK_OFF(0));

    fprintf(mc2chk_fp, "%5d %08X %08X", mc2chk_frame, rnd, clk);
    Bit32u masked = 0;
    for (int i = 0; i < MC2CHK_NREGIONS; i++) {
        bool isd41a0 = (mc2chk_regions[i].base == MC2CHK_D41A0);
        Bit32u c = mc2chk_region_crc(mc2chk_regions[i], dump, isd41a0 ? &masked : NULL);
        fprintf(mc2chk_fp, " %08X", c);
        if (isd41a0) fprintf(mc2chk_fp, " %08X", masked);
    }
    fprintf(mc2chk_fp, "\n");
    fflush(mc2chk_fp);

    if (dump != NULL) fclose(dump);

    if (mc2chk_seqdir[0] != '\0') {
        for (int i = 0; i < MC2CHK_NSEQ; i++) {
            if (mc2chk_seq[i].fp == NULL) continue;
            MEM_BlockRead((PhysPt)mc2chk_seq[i].base, mc2chk_seqbuf, mc2chk_seq[i].size);
            if (mc2chk_seqz) mc2chk_seqz_frame(mc2chk_seq[i].fp, mc2chk_seqbuf, mc2chk_seqprev[i], mc2chk_seq[i].size);
            else fwrite(mc2chk_seqbuf, 1, mc2chk_seq[i].size, mc2chk_seq[i].fp);
        }
        /* prubezne na disk, at po padu zustane aspon vetsina snimku */
        if (mc2chk_frame % 100 == 99)
            for (int i = 0; i < MC2CHK_NSEQ; i++) if (mc2chk_seq[i].fp != NULL) fflush(mc2chk_seq[i].fp);
    }

    mc2chk_frame++;

    /* Sebetest zachytu konce procesu: MC2CHK_TESTEXIT=<snimek> zavola exit()
     * primo, tedy mimo mc2chk_finish(). V zaznamu pak musi byt NECEKANY KONEC
     * i vypis zasobniku - tim se overi, ze aparatura funguje. */
    if (mc2chk_testexit > 0 && mc2chk_frame == mc2chk_testexit)
        exit(0);

    if (mc2chk_frame >= mc2chk_frames)
        mc2chk_finish(0, "hotovo");
}

/* Watchdog - kdyz level vubec nenabehne, at beh neuvizne navzdy. */
static void mc2chk_tick(long long stepcount) {
    if (!mc2chk_on) return;

    /* obsluhy se registruji az tady - musi byt az za svymi definicemi */
    static bool handlers_installed = false;
    if (!handlers_installed) {
        handlers_installed = true;
        atexit(mc2chk_atexit);
#ifdef MC2CHK_STACKTRACE
        SetUnhandledExceptionFilter(mc2chk_seh);
#endif
        signal(SIGSEGV, mc2chk_signal);
        signal(SIGABRT, mc2chk_signal);
        signal(SIGILL,  mc2chk_signal);
        signal(SIGFPE,  mc2chk_signal);
    }

    mc2chk_steps = stepcount;

    /* Vola se pred provedenim instrukce na reg_eip, takze zmenu od minula
     * zpusobila instrukce, na ktere se stalo minule. */
    if (mc2chk_watch_addr != 0 && mc2chk_started) {
        Bit32u v = mc2chk_watch_size == 4 ? mem_readd(mc2chk_watch_addr)
                 : mc2chk_watch_size == 2 ? (Bit32u)mem_readw(mc2chk_watch_addr)
                 : (Bit32u)mem_readb(mc2chk_watch_addr);
        if (!mc2chk_watch_have) {
            mc2chk_watch_have = true;
            mc2chk_watch_last = v;
            if (mc2chk_fp != NULL)
                fprintf(mc2chk_fp, "# WATCH 0x%08X: pocatecni hodnota 0x%X (snimek %d, krok %lld)\n",
                        mc2chk_watch_addr, v, mc2chk_frame, stepcount);
        } else if (v != mc2chk_watch_last) {
            const bool log_it = (mc2chk_frame >= mc2chk_watch_from);
            if (mc2chk_fp != NULL && log_it && mc2chk_watch_hits < 200) {
                fprintf(mc2chk_fp, "# WATCH 0x%08X: 0x%X -> 0x%X, zapsala instrukce na EIP 0x%08X (IDA 0x%08X), snimek %d, krok %lld\n",
                        mc2chk_watch_addr, mc2chk_watch_last, v, mc2chk_watch_prev_eip,
                        mc2chk_watch_prev_eip - 0x1E1000u, mc2chk_frame, stepcount);
                fflush(mc2chk_fp);
            }
            if (log_it) mc2chk_watch_hits++;
            mc2chk_watch_last = v;
        }
        mc2chk_watch_prev_eip = reg_eip;
    }

    /* Ulozeni levelu (sub_57640 -> SaveLevel_55080, na zacatku levelu to je snimek 2)
     * prevede ukazatele na entity ve StageVars2 na offsety a v pameti je tak necha.
     * sub_12780 pak ten offset bere jako ukazatel (IDA 0x127E4: mov ecx,[eax+4]) a cte
     * [ecx+8] a [ecx+0Dh] z konvencni pameti DOSu - vysledek zavisi na tom, co tam
     * emulator zrovna ma. Overeno: 0x723D = E6 spoustel prechod 33 entit, E2 ne.
     * remc2 tam ma kontrolu rozsahu (//fix) a offset nespusti nikdy. Aby se porovnavalo
     * chovani hry a ne obsah pameti emulatoru, dela se tady totez: ukazatel mimo pole
     * entit se pred "test ecx,ecx" (IDA 0x127E7) vynuluje a original skoci na
     * "nespoustet". Ostatni cteni tehoz pole (sub_1D700, sub_1D7C0, sub_1D8C0) maji
     * v originale "cmp esi, dword_EA3E4 / jbe" a tuhle upravu nepotrebuji. */
    if (!mc2chk_rawstageptr && reg_eip == 0x1F37E7u && reg_ecx != 0) {
        const Bit32u first = mem_readd(0x2BB3E4u);            /* Entities_EA3E4[0]    */
        const Bit32u end   = mem_readd(0x2BB3E4u + 4u * 1000u); /* Entities_EA3E4[1000] */
        if (reg_ecx < first || reg_ecx >= end) {
            if (mc2chk_fp != NULL && mc2chk_stageptr_hits < 50)
                fprintf(mc2chk_fp, "# STAGEPTR: StageVars2 @%08X ma v +4 hodnotu %08X mimo entity - nespousti se jako v remc2 (snimek %d)\n",
                        reg_eax, reg_ecx, mc2chk_frame);
            mc2chk_stageptr_hits++;
            reg_ecx = 0;
        }
    }

    if (mc2chk_poke_count > 0 && mc2chk_started && !mc2chk_poke_done) {
        mc2chk_poke_done = true;
        for (int i = 0; i < mc2chk_poke_count; i++) {
            Bit8u before = mem_readb(mc2chk_poke_addr[i]);
            mem_writeb(mc2chk_poke_addr[i], mc2chk_poke_val[i]);
            if (mc2chk_fp != NULL)
                fprintf(mc2chk_fp, "# POKE 0x%08X: 0x%02X -> 0x%02X (snimek %d, krok %lld)\n",
                        mc2chk_poke_addr[i], before, mc2chk_poke_val[i], mc2chk_frame, stepcount);
        }
    }

    /* reg_eip je instrukce, ktera se prave chysta provest */
    if (mc2chk_trace_count > 0 && mc2chk_started) {
        const bool frame_ok = (mc2chk_trace_frame < 0 || mc2chk_trace_frame == mc2chk_frame);
        const bool eax_ok = (!mc2chk_trace_eax_on || reg_eax == mc2chk_trace_eax);
        for (int i = 0; i < mc2chk_trace_count && frame_ok && eax_ok; i++) {
            if (reg_eip != mc2chk_trace_eip[i] || mc2chk_trace_hits[i] >= 300 || mc2chk_fp == NULL) continue;
            mc2chk_trace_hits[i]++;
            fprintf(mc2chk_fp, "# TRACE IDA 0x%05X (z IDA 0x%05X) snimek %d: eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X\n",
                    reg_eip - 0x1E1000u, mc2chk_trace_prev_eip - 0x1E1000u, mc2chk_frame,
                    reg_eax, reg_ebx, reg_ecx, reg_edx, reg_esi, reg_edi, reg_ebp, reg_esp);
            /* [esp] - na vstupu funkce je tam navratova adresa, tedy kdo ji zavolal */
            fprintf(mc2chk_fp, "#     [esp]:");
            for (int b = 0; b < 8; b++) {
                Bit32u v = mem_readd(reg_esp + 4u * b);
                fprintf(mc2chk_fp, " %08X", v);
            }
            {
                Bit32u ret = mem_readd(reg_esp);
                if (ret >= 0x1E1000u && ret < 0x300000u)
                    fprintf(mc2chk_fp, "  (navrat IDA 0x%05X)", ret - 0x1E1000u);
            }
            fprintf(mc2chk_fp, "\n");
            Bit32u ptrs[2] = { reg_eax, reg_ecx };
            const char* names[2] = { "eax", "ecx" };
            for (int k = 0; k < 2; k++) {
                if (ptrs[k] < 0x1000u || ptrs[k] >= 0xFF0000u) continue;   /* neni to adresa */
                fprintf(mc2chk_fp, "#     [%s]:", names[k]);
                for (int b = 0; b < 16; b++) fprintf(mc2chk_fp, " %02X", mem_readb(ptrs[k] + b));
                fprintf(mc2chk_fp, "\n");
            }
        }
        mc2chk_trace_prev_eip = reg_eip;
    }
    if (!mc2chk_started && stepcount > mc2chk_watchdog)
        mc2chk_finish(4, "watchdog - level nenabehl");
}

#endif /* MC2CHECK_H */
