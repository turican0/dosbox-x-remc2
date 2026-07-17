/*
 * engine.cpp (rizeno externim konfiguracnim souborem - viz DOSBOX_CTL_PROTOCOL.md)
 *
 * Puvodni soubor obsahoval stovky radku jednorazovych ladicich haku
 * (addprocedurestop, writesequence, spytest_*, hardcoded switch-case na
 * konkretni adresy volani...) nahromadenych behem rucniho reverse
 * engineeringu. Ty jsou pryc - byly vazane na konkretni vysetrovaci session.
 *
 * Namisto toho: cely beh (co dumpovat, co patchovat, kdy skoncit) rika
 * externi textovy konfiguracni soubor, ktery muze psat/generovat jiny
 * program bez zasahu do teto binarky. To umoznuje spustit DOSBox-X
 * automaticky zvenku (napr. z tveho C++ orchestratoru), necha ho dumpnout
 * data a sam se ukoncit, porovnat vystup, pripadne cyklus zopakovat s jinym
 * configem.
 *
 * Konfigurak se hleda:
 *   1. v ceste z env. promenne DOSBOX_CTL_FILE (pro paralelni behy - kazdy
 *      dostane jiny soubor)
 *   2. jinak "dosbox_ctl.cfg" v aktualnim adresari
 *
 * DULEZITE - over/uprav pro svuj konkretni build:
 *   TURN_ADVANCE_EIP, GAME_END_EIP nize jsou prevzate z puvodniho souboru
 *   (0x232d2f, 0x236FE6) - jsou to adresy KONKRETNI verze Orion2.exe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>

#include "dosbox.h"
#include "engine.h"
#include "mem.h"
#include "cpu.h"
#include "regs.h"
#include "memory.h"
#include "debug.h"
#include "callback.h"
#include "support.h"
#include "control.h"
#include "engine_support.h"
#include "InputRecorder.h"

#include "ctl_common.h"

// ---- Adresy specificke pro konkretni build hry - over pred pouzitim! ----
static const unsigned int TURN_ADVANCE_EIP = 0x232d2f;  // misto, kde hra cte/zapisuje tah hrace
static const unsigned int GAME_END_EIP     = 0x236FE6;  // konec partie/programu

// ---- Deterministicky vstup (nahravani/prehravani tahu hrace) ----
InputRecorder* m_InputRecorder = nullptr;
std::string m_play_file;
std::string m_record_file;

// ---- rizeni z venku ----
static CtlConfig g_ctl_cfg;
static CtlEngine g_ctl;
static bool g_ctl_initialized = false;

#include "trace_dosbox_symbols.gen.cpp"   // vygenerovano gen_watchtable.cpp -> g_watch_symbols[]

static unsigned int ctl_read_mem(unsigned int addr, unsigned int width) {
    switch (width) {
        case 1: return (unsigned int)mem_readb(addr);
        case 2: return (unsigned int)mem_readw(addr);
        default: return (unsigned int)mem_readd(addr);
    }
}

static void ctl_write_mem(unsigned int addr, unsigned int width, unsigned int value) {
    switch (width) {
        case 1: mem_writeb(addr, value & 0xFF); break;
        case 2: mem_writew(addr, value & 0xFFFF); break;
        default: mem_writed(addr, value); break;
    }
}

static void ctl_do_stop() {
    // Nejjednodussi spolehlivy zpusob, jak z automatizace ukoncit DOSBox-X
    // beh po dumpnuti dat - abrupt exit(0). Pokud ti to zpusobuje problemy
    // (napr. nekorektni zavreni jinych souboru/zasobniku DOSBox-X), nahrad
    // za vlastni mechanismus ukonceni DOSBox-X (typicky throw/E_Exit()
     // definovane v dosbox.h tveho stromu).
    exit(0);
}

static void ctl_init_once() {
    if (g_ctl_initialized) return;
    g_ctl_initialized = true;

    const char* env_path = getenv("DOSBOX_CTL_FILE");
    std::string cfg_path = env_path ? env_path : "dosbox_ctl.cfg";

    ctl_load_config(cfg_path, g_ctl_cfg);

    g_ctl.symbols = g_watch_symbols;
    g_ctl.symbols_count = g_watch_symbols_count;
    g_ctl.read_mem = ctl_read_mem;
    g_ctl.write_mem = ctl_write_mem;
    g_ctl.on_stop = ctl_do_stop;
    g_ctl.open_output(g_ctl_cfg.output_file);
}

// ---- volano DOSBox-X pri kazde instrukci/cyklu CPU ----
void enginestep() {
    ctl_init_once();
    g_ctl.cycle++;

    // EIP/CYCLE/CHANGED/EQ/NEQ podminky se vyhodnocuji tady (kazdy krok).
    g_ctl.step(g_ctl_cfg, /*is_call_context=*/false, /*call_addr=*/0, (unsigned int)reg_eip);

    // ---- deterministicky vstup hrace (InputRecorder) ----
    if (m_play_file.length() > 0 && m_InputRecorder == nullptr) {
        m_InputRecorder = new InputRecorder(m_play_file.c_str());
        m_InputRecorder->StartPlayback();
    } else if (m_record_file.length() > 0 && m_InputRecorder == nullptr) {
        m_InputRecorder = new InputRecorder(m_record_file.c_str());
        m_InputRecorder->StartRecording();
    }

    if (m_InputRecorder != nullptr && reg_eip == TURN_ADVANCE_EIP) {
        // Format ulozenych dat (10 bajtu na tah) prevzat z puvodniho
        // souboru - popisuje konkretni herni strukturu vstupu hrace.
        // Uprav podle skutecneho rozlozeni dat sve hry.
        Bit32u d41A0 = 0x356038;
        int16_t playerIndex = reg_edx;
        Bit32u playerRec = d41A0 + 0x2bde + (0x84C * playerIndex);
        Bit32u x_D41A0 = mem_readd(0x2a51a4);
        int16_t levelNumber = mem_readw(x_D41A0 + 43);
        int32_t turn = mem_readd(playerRec + 18);
        Bit32u inputAddr = d41A0 + 0x6e3e + (0xa * playerIndex);

        if (m_InputRecorder->m_IsPlaying) {
            RecordedEventTurn* ev = m_InputRecorder->GetCurrentPlayerActions(levelNumber, playerIndex, turn);
            if (ev != nullptr) {
                for (int i = 0; i < 10; i++)
                    mem_writeb(inputAddr + i, ev->Bytes[i]);
            }
        }
        if (m_InputRecorder->m_IsRecording) {
            uint8_t bytes[10];
            for (int i = 0; i < 10; i++)
                bytes[i] = mem_readb(inputAddr + i);
            m_InputRecorder->RecordPlayerActions(levelNumber, playerIndex, turn, sizeof(bytes), bytes);
        }
    }

    if (m_InputRecorder != nullptr && reg_eip == GAME_END_EIP) {
        if (m_InputRecorder->m_IsPlaying) m_InputRecorder->StopPlayback();
        if (m_InputRecorder->m_IsRecording) m_InputRecorder->StopRecording();
    }
}

// ---- volano DOSBox-X pri kazdem far call ----
// selector/offset = cil volani (segment:offset v DOS adresnim prostoru).
int engine_call(bool use32, Bitu selector, Bitu offset, Bitu oldeip) {
    ctl_init_once();
    if (selector == 0x160) {
        g_ctl.step(g_ctl_cfg, /*is_call_context=*/true, (unsigned int)offset, (unsigned int)reg_eip);
    }
    return 0;  // 0 = nech DOSBox-X provest volani standardne
}

// ---- volano DOSBox-X pri navratu z far call ----
void engine_ret(Bitu myreg_eip) {
    // Momentalne nic. Chces-li checkpoint i PO navratu (napr. navratova
    // hodnota v EAX), pridej vlastni cond typ (napr. "ret:0xADDR") do
    // ctl_common.h a zavolej g_ctl.step(...) i odsud.
}

// ---- tezky snapshot pro rucni hloubkovou analyzu jednoho bodu ----
// Volej rucne (napr. z debuggeru) v miste, kde compare nahlasil prvni
// rozdil, pro ziskani uplneho obrazu stavu (vsech 16MB pameti).
void saveactstate() {
    char name1[1024];
    sprintf(name1, "engine-registers-%04X-%08X.txt", SegValue(cs), reg_eip);
    char name2[1024];
    sprintf(name2, "engine-memory-%04X-%08X.bin", SegValue(cs), reg_eip);

    FILE* fptw1;
    fopen_s(&fptw1, name1, "wt");
    fprintf(fptw1, "%04X:%08X\n", SegValue(cs), reg_eip);
    fprintf(fptw1, "EAX:%08X,EBX:%08X,ECX:%08X,EDX:%08X\n", reg_eax, reg_ebx, reg_ecx, reg_edx);
    fprintf(fptw1, "ESI:%08X,EDI:%08X,EBP:%08X,ESP:%08X\n", reg_esi, reg_edi, reg_ebp, reg_esp);
    fprintf(fptw1, "CS:%04X,DS:%04X,ES:%04X,FS:%04X,GS:%04X,SS:%04X\n",
            SegValue(cs), SegValue(ds), SegValue(es), SegValue(fs), SegValue(gs), SegValue(ss));
    fprintf(fptw1, "CF:%01X,ZF:%01X,SF:%01X,OF:%01X,AF:%01X,PF:%01X,IF:%01X\n",
            (get_CF() > 0), (get_ZF() > 0), (get_SF() > 0), (get_OF() > 0),
            (get_AF() > 0), (get_PF() > 0), GETFLAGBOOL(IF));
    fclose(fptw1);

    FILE* fptw;
    fopen_s(&fptw, name2, "wb");
    unsigned char buffer[1];
    for (long i = 0; i < 0x1000000; i++) {
        buffer[0] = (unsigned char)mem_readb(i);
        fwrite(buffer, 1, 1, fptw);
    }
    fclose(fptw);
}
