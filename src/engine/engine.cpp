/*
 * engine.cpp
 *
 * Automated "load level N from the main menu, skip all intro/menu
 * animations, dump every simulation frame, then exit" driver, plus the
 * handful of general-purpose reverse-engineering tools worth keeping
 * around (deterministic input recording/playback, memory-region dumping
 * on a given EIP, in-place level save/load).
 *
 * BACKGROUND / WHY THIS FILE LOOKS THE WAY IT DOES
 * -------------------------------------------------
 * The previous version of this file (kept in version control / your
 * orig-src backup if you need to dig anything back out of it) had grown
 * into ~2300 lines of one-off breakpoint scaffolding, spytest_*() probes,
 * call tracers, and even a couple of dead hand-decompiled reimplementations
 * of sub_0x0026DB3A that nothing calls. All of that was tied to specific
 * past investigation sessions and has been removed.
 *
 * What's been KEPT (per your request), same names/semantics as before:
 *   - InputRecorder-based recording/playback of player turns
 *     (m_play_file / m_record_file, TURN_ADVANCE_EIP / GAME_END_EIP)
 *   - writesequence() / writeseqall() / savesequence() - dump a region of
 *     emulated memory to a file every time a given EIP is hit
 *   - saveInStep() / loadInStep() - in-place level save/load code caves
 *   - saveactstate() - used by the debugger's SAVESTATE command
 *
 * What's NEW is that the "skip intro -> jump straight to new game -> run
 * level X" flow, which used to live hardcoded behind #ifdef TEST_REGRESSIONS
 * with a single fixed level number, is now:
 *   - always on (no ifdef)
 *   - driven by env vars instead of recompiling
 *   - wired directly to writeseqall(MAIN_LOOP_STEP_EIP, 0, dump_steps) once
 *     the level has finished loading
 *   - self-terminating (exit(0)) once the requested number of frames has
 *     been dumped
 *
 * That makes this binary a single-purpose worker: "load this one level,
 * dump this many frames, quit". Running the SAME level twice (two separate
 * process launches, so nothing carries over between them) and diffing the
 * two dump trees is how you find where a run desyncs. This mirrors the
 * external-orchestration approach used in reorion2's engine.cpp - drive
 * DOSBox-X from outside, let it dump and quit on its own, compare on the
 * outside, repeat with different parameters.
 *
 * ENV VARS (read once, on first enginestep()/engine_call()):
 *   NETHERW_LEVEL        level number to load                (default 1)
 *   NETHERW_RUN_PASS     arbitrary tag, only used to keep the dump output
 *                        of two otherwise-identical runs apart, e.g. "1"
 *                        and "2" for a same-level determinism check
 *                                                              (default 1)
 *   NETHERW_DUMP_STEPS   how many hits of MAIN_LOOP_STEP_EIP to dump
 *                                                              (default 3000)
 *   NETHERW_OUT_DIR      base output directory                (default "dump")
 *   NETHERW_PLAY_FILE    optional recorded-input file to play back, if the
 *                        level needs player actions to progress at all
 *   NETHERW_RECORD_FILE  optional file to record player actions into
 *                        (mutually exclusive with NETHERW_PLAY_FILE)
 *
 * Output layout:
 *   <NETHERW_OUT_DIR>/level_<NNN>/pass<P>/sequence-<EIP>-<DATAADDR>.bin
 *
 * See tools/run_regression.py for a driver that loops over a level range,
 * runs each level twice, and calls tools/compare_dumps.py on the result.
 *
 * IMPORTANT - verify/adjust for your build:
 *   All of the *_EIP constants below are addresses from THIS SPECIFIC BUILD
 *   of the game binary. If you rebuild against a different executable,
 *   re-check them before trusting any of this.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <sys/stat.h>
#if defined(WIN32)
#include <direct.h>
#endif

#include "dosbox.h"
#include "engine.h"
#include "mem.h"
#include "cpu.h"
#include "cpu/lazyflags.h"
#include "memory.h"
#include "debug.h"
#include "callback.h"
#include "InputRecorder.h"

// ---------------------------------------------------------------------
// Addresses specific to this build of the game - verify/adjust if you
// rebuild against a different binary. Comments carried over from the
// original investigation notes.
// ---------------------------------------------------------------------
static const unsigned int INTRO_SKIP_EIP      = 0x236FE1; // skip title/intro screen(s)
static const unsigned int SKIP_TO_NEWGAME_EIP = 0x25c254; // menu -> jump straight into "new game"
static const unsigned int RUN_LEVEL_EIP       = 0x2585b8; // "new game" -> picks which level to run
static const unsigned int AFTER_LOAD_EIP      = 0x2368e0; // fires once the level has finished loading
static const unsigned int COUNT_INIT_EIP      = 0x227af1; // fires after the level's frame counter (re)inits
static const unsigned int GATE_ENABLE_EIP     = 0x215540; // gates the hooks below it (see debugafter_215540)
static const unsigned int MAIN_LOOP_STEP_EIP  = 0x2285ff; // once-per-simulation-frame tick; writeseqall target
static const unsigned int TURN_ADVANCE_EIP    = 0x232d2f; // player-turn input point (InputRecorder)
static const unsigned int GAME_END_EIP        = 0x236FE6; // level/session end (InputRecorder)

// x_D41A0_BYTEARRAY_4_struct pointer and the main game-state struct base,
// both used repeatedly below - same addresses/names as before.
static const Bit32u X_D41A0_PTR   = 0x2a51a4;
static const Bit32u D41A0_BASE    = 0x356038;

// =======================================================================
// Config - read once from the environment. Kept as plain globals (rather
// than a struct) to match the rest of this file's style.
// =======================================================================
static int          g_target_level  = 1;
static int          g_run_pass      = 1;
static long         g_dump_steps    = 3000;
static std::string  g_out_dir       = "dump";
static bool         g_config_loaded = false;

static int env_int(const char* name, int def) {
    const char* v = getenv(name);
    if (!v || !*v) return def;
    return atoi(v);
}

static std::string env_str(const char* name, const char* def) {
    const char* v = getenv(name);
    return (v && *v) ? std::string(v) : std::string(def);
}

// Minimal recursive mkdir - only needs to handle the '/'-separated paths
// this file builds itself, so no need for anything fancier.
static void mkdir_p(const std::string& path) {
    std::string partial;
    for (size_t i = 0; i < path.size(); i++) {
        char c = path[i];
        if (c == '/' || c == '\\') {
            if (!partial.empty()) {
#if defined(WIN32)
                _mkdir(partial.c_str());
#else
                mkdir(partial.c_str(), 0775);
#endif
            }
        }
        partial += c;
    }
    if (!partial.empty()) {
#if defined(WIN32)
        _mkdir(partial.c_str());
#else
        mkdir(partial.c_str(), 0775);
#endif
    }
}

static std::string g_level_dir;   // <out_dir>/level_NNN/passP, computed once

static void config_init_once() {
    if (g_config_loaded) return;
    g_config_loaded = true;

    g_target_level = env_int("NETHERW_LEVEL", 1);
    g_run_pass     = env_int("NETHERW_RUN_PASS", 1);
    g_dump_steps   = env_int("NETHERW_DUMP_STEPS", 3000);
    g_out_dir      = env_str("NETHERW_OUT_DIR", "dump");

    std::string play_file   = env_str("NETHERW_PLAY_FILE", "");
    std::string record_file = env_str("NETHERW_RECORD_FILE", "");
    extern std::string m_play_file;
    extern std::string m_record_file;
    m_play_file   = play_file;
    m_record_file = record_file;

    char levelbuf[16];
    char passbuf[16];
    sprintf(levelbuf, "level_%03d", g_target_level);
    sprintf(passbuf, "pass%d", g_run_pass);
    g_level_dir = g_out_dir + "/" + levelbuf + "/" + passbuf;
    mkdir_p(g_level_dir);

    fprintf(stderr, "[engine] level=%d pass=%d dump_steps=%ld out=%s\n",
            g_target_level, g_run_pass, g_dump_steps, g_level_dir.c_str());
}

// =======================================================================
// Deterministic player input (recording/playback) - unchanged behaviour
// from the original file. Empty by default: most levels don't need any
// player action to just sit in their main loop and be dumped, but if
// yours does, point NETHERW_PLAY_FILE at a recording made with
// NETHERW_RECORD_FILE on an earlier run.
// =======================================================================
InputRecorder* m_InputRecorder = nullptr;
std::string m_play_file;
std::string m_record_file;

static void input_recorder_init_once() {
    if (m_InputRecorder != nullptr) return;
    if (!m_play_file.empty()) {
        m_InputRecorder = new InputRecorder(m_play_file.c_str());
        m_InputRecorder->StartPlayback();
    } else if (!m_record_file.empty()) {
        m_InputRecorder = new InputRecorder(m_record_file.c_str());
        m_InputRecorder->StartRecording();
    }
}

// player-turn read/write, called once per TURN_ADVANCE_EIP hit
static void input_recorder_step() {
    if (m_InputRecorder == nullptr) return;

    uint32_t rand_unused = mem_readd(D41A0_BASE + 0x8);
    (void)rand_unused;
    int16_t playerIndex = (int16_t)reg_edx;
    Bit32u D41A0_0_array_type_str_0x2BDE = D41A0_BASE + 0x2bde + (0x84C * playerIndex);
    int d41A0_0_playerInputs_0x6E3E = D41A0_BASE + 0x6e3e + (0xa * playerIndex);
    Bit32u x_D41A0_BYTEARRAY_4_struct = mem_readd(X_D41A0_PTR);
    int16_t levelNumber_43w = mem_readw(x_D41A0_BYTEARRAY_4_struct + 43);
    int32_t turn_2BE0_11248 = mem_readd(D41A0_0_array_type_str_0x2BDE + 18);

    if (m_InputRecorder->m_IsPlaying) {
        RecordedEventTurn* eventTurn =
            m_InputRecorder->GetCurrentPlayerActions(levelNumber_43w, playerIndex, turn_2BE0_11248);
        if (eventTurn != nullptr) {
            for (int i = 0; i < 10; i++)
                mem_writeb(d41A0_0_playerInputs_0x6E3E + i, eventTurn->Bytes[i]);
        }
    }

    if (m_InputRecorder->m_IsRecording) {
        uint8_t turnBytes[10];
        for (int i = 0; i < 10; i++)
            turnBytes[i] = mem_readb(d41A0_0_playerInputs_0x6E3E + i);
        m_InputRecorder->RecordPlayerActions(levelNumber_43w, playerIndex, turn_2BE0_11248,
                                              sizeof(turnBytes), turnBytes);
    }
}

static void input_recorder_stop_if_needed() {
    if (m_InputRecorder == nullptr) return;
    if (m_InputRecorder->m_IsPlaying) m_InputRecorder->StopPlayback();
    if (m_InputRecorder->m_IsRecording) m_InputRecorder->StopRecording();
}

// =======================================================================
// writesequence() / writeseqall() / savesequence()
//
// Dumps `size` bytes starting at `dataadress` to a file every time
// `codeadress` is hit, for `count` hits total (first `savefrom` hits are
// skipped, not written). One call arms one (codeadress, dataadress) pair;
// writeseqall() arms the 6 memory regions that matter for this game's
// simulation state, including the 2-byte RNG seed at 0x34c4e0 that
// AFTER_LOAD_EIP resets to a fixed value on every level load - that's the
// one to watch for the first byte that differs between two passes.
// =======================================================================
static const int MAX_WRITESEQ = 32;
static Bit32u writesequencecodeadress[MAX_WRITESEQ];
static long   writesequencecount[MAX_WRITESEQ];
static long   writesequencesize[MAX_WRITESEQ];
static long   writesequencecount2[MAX_WRITESEQ];
static Bit32u writesequencedataadress[MAX_WRITESEQ];
static Bit32u writesequencesavefrom[MAX_WRITESEQ];
static int    lastwriteindexsequence = 0;

static void writesequence(Bit32u codeadress, long count, long size, Bit32u dataadress, Bit32u savefrom = 0) {
    if (lastwriteindexsequence >= MAX_WRITESEQ) {
        fprintf(stderr, "[engine] writesequence: too many armed sequences, ignoring\n");
        return;
    }
    int i = lastwriteindexsequence;
    writesequencecodeadress[i] = codeadress;
    writesequencecount[i]      = count;
    writesequencesize[i]       = size;
    writesequencedataadress[i] = dataadress;
    writesequencecount2[i]     = 0;
    writesequencesavefrom[i]   = savefrom;
    lastwriteindexsequence++;
}

// Arms the standard set of memory regions this game's determinism checks
// care about, dumped every time `adress` is hit, `count` times, skipping
// the first `skip` hits. Matches the original call signature exactly:
//   writeseqall(0x2285ff, 0, 3000);
static void writeseqall(Bit32u adress, Bit32u skip = 0, long count = 0x10000) {
    writesequence(adress, count, 0x70000,     0x2dc4e0, skip);
    writesequence(adress, count, 0x36e16,     0x356038, skip); // D41A0 main game-state struct
    writesequence(adress, count, 320 * 200,   0x3aa0a4, skip);
    writesequence(adress, count, 0xab,        0x3514b0, skip);
    writesequence(adress, count, 0xc4e,       0x2b3a74, skip);
    writesequence(adress, count, 0x2,         0x34c4e0, skip); // RNG seed - watch this one first
}

static void savesequence(int index, long actsize, Bit32u dataadress) {
    Bit32u dataadress2 = dataadress;
    if (dataadress == 0xffffff01) dataadress2 = reg_esi;
    if (dataadress == 0xffffff02) dataadress2 = reg_edi;
    if (dataadress == 0xffffff03) dataadress2 = reg_ecx;

    char findnamex[512];
    sprintf(findnamex, "%s/sequence-%08X-%08X.bin",
            g_level_dir.c_str(), writesequencecodeadress[index], dataadress);

    FILE* fseq = nullptr;
    fopen_s(&fseq, findnamex, "ab");
    if (!fseq) {
        fprintf(stderr, "[engine] savesequence: could not open %s\n", findnamex);
        return;
    }
    unsigned char buffer[1];
    for (long i = 0; i < actsize; i++) {
        buffer[0] = (unsigned char)mem_readb(i + dataadress2);
        fwrite(buffer, 1, 1, fseq);
    }
    fclose(fseq);
}

// Returns true once every armed writeseqall() entry has recorded its full
// hit count - i.e. the dump for this level/pass is complete.
static bool writeseq_all_done() {
    if (lastwriteindexsequence == 0) return false;
    for (int i = 0; i < lastwriteindexsequence; i++)
        if (writesequencecount2[i] < writesequencecount[i])
            return false;
    return true;
}

// =======================================================================
// saveInStep() / loadInStep()
//
// In-place level save/load via a small code cave: on the Nth hit of
// `adress`, patches in a call to the game's own LoadLevel/SaveLevel
// routine, then restores the original bytes and jumps back. Handy for
// jumping straight into the middle of a level without replaying up to it.
// Unchanged from the original file.
// =======================================================================
static Bit32u saveInStep_Adress;
static int    saveInStep_Step = -1;
void saveInStep(Bit32u adress, int step = 0) {
    saveInStep_Adress = adress;
    saveInStep_Step = step;
}

static Bit32u loadInStep_Adress;
static int    loadInStep_Step = -1;
void loadInStep(Bit32u adress, int step = 0) {
    loadInStep_Adress = adress;
    loadInStep_Step = step;
}

static void saveInStep_check() {
    if (saveInStep_Step <= -1) return;
    if (reg_eip != saveInStep_Adress) return;
    if (saveInStep_Step == 0) {
        Bit32u cave_addr = 0x90000; // depending on where you can safely write
        Bit32u rel_jmp = cave_addr - (reg_eip + 5);
        Bit8u orig[5]; for (int i = 0; i < 5; i++) orig[i] = mem_readb(reg_eip + i);
        mem_writeb(reg_eip, 0xE9);           // JMP
        mem_writed(reg_eip + 1, rel_jmp);

        Bit32u x_D41A0_BYTEARRAY_4_struct = mem_readd(X_D41A0_PTR);
        Bit32u levelIndex = mem_readw(x_D41A0_BYTEARRAY_4_struct + 43);

        Bitu p = cave_addr;
        mem_writeb(p++, 0x60);                                       // PUSHAD
        mem_writeb(p++, 0xB8); mem_writed(p, levelIndex); p += 4;     // MOV EAX, levelIndex
        mem_writeb(p++, 0x50);                                       // PUSH EAX
        mem_writeb(p++, 0x6A); mem_writeb(p++, 0x00);                 // PUSH 0 (0=inGame save,1=mapMenuSave)
        mem_writeb(p++, 0xE8); mem_writed(p, 0x00236080 - (p + 4)); p += 4; // CALL SaveLevel_555D0
        mem_writeb(p++, 0x83); mem_writeb(p++, 0xC4); mem_writeb(p++, 0x08); // ADD ESP,8

        for (int i = 0; i < 5; i++) {                                 // self-restore original bytes
            mem_writeb(p++, 0xC6); mem_writeb(p++, 0x05); mem_writed(p, reg_eip + i); p += 4;
            mem_writeb(p++, orig[i]);
        }
        mem_writeb(p++, 0x61);                                       // POPAD
        mem_writeb(p++, 0xE9); mem_writed(p, reg_eip - (p + 4)); p += 4; // JMP back
    }
    saveInStep_Step--;
}

static void loadInStep_check() {
    if (loadInStep_Step <= -1) return;
    if (reg_eip != loadInStep_Adress) return;
    if (loadInStep_Step == 0) {
        Bit32u cave_addr = 0x90000; // depending on where you can safely write
        Bit32u rel_jmp = cave_addr - (reg_eip + 5);
        Bit8u orig[5]; for (int i = 0; i < 5; i++) orig[i] = mem_readb(reg_eip + i);
        mem_writeb(reg_eip, 0xE9);
        mem_writed(reg_eip + 1, rel_jmp);

        Bit32u x_D41A0_BYTEARRAY_4_struct = mem_readd(X_D41A0_PTR);
        Bit32u levelIndex = mem_readw(x_D41A0_BYTEARRAY_4_struct + 43);

        Bitu p = cave_addr;
        mem_writeb(p++, 0x60);                                       // PUSHAD
        mem_writeb(p++, 0xB8); mem_writed(p, levelIndex); p += 4;     // MOV EAX, levelIndex
        mem_writeb(p++, 0x50);                                       // PUSH EAX
        mem_writeb(p++, 0x6A); mem_writeb(p++, 0x00);                 // PUSH 0
        mem_writeb(p++, 0xE8); mem_writed(p, 0x002365D0 - (p + 4)); p += 4; // CALL LoadLevel_555D0
        mem_writeb(p++, 0x83); mem_writeb(p++, 0xC4); mem_writeb(p++, 0x08); // ADD ESP,8

        // clear OptionsSettingFlag_24 (offset 0x18)
        mem_writeb(p++, 0x8B); mem_writeb(p++, 0x1D); mem_writed(p, 0x002A51A4); p += 4;
        mem_writeb(p++, 0xC6); mem_writeb(p++, 0x43); mem_writeb(p++, 0x18); mem_writeb(p++, 0x01);

        int playerIndex = mem_readw(D41A0_BASE + 0xc);
        Bit32u D41A0_0_array_type_str_0x2BDE = D41A0_BASE + 0x2bde + (0x84C * playerIndex);
        Bit32u addr_word_0x04d = D41A0_0_array_type_str_0x2BDE + 0x4d;
        Bit32u addr_word_0x04f = D41A0_0_array_type_str_0x2BDE + 0x4f;
        mem_writeb(p++, 0x66); mem_writeb(p++, 0xC7); mem_writeb(p++, 0x05); mem_writed(p, addr_word_0x04d); p += 4;
        mem_writew(p, 100); p += 2;
        mem_writeb(p++, 0x66); mem_writeb(p++, 0xC7); mem_writeb(p++, 0x05); mem_writed(p, addr_word_0x04f); p += 4;
        mem_writew(p, 1); p += 2;

        for (int i = 0; i < 5; i++) {
            mem_writeb(p++, 0xC6); mem_writeb(p++, 0x05); mem_writed(p, reg_eip + i); p += 4;
            mem_writeb(p++, orig[i]);
        }
        mem_writeb(p++, 0x61);
        mem_writeb(p++, 0xE9); mem_writed(p, reg_eip - (p + 4)); p += 4;
    }
    loadInStep_Step--;
}

// =======================================================================
// Main per-instruction hook, called by the CPU core every cycle
// (src/cpu/core_normal.cpp).
// =======================================================================
static bool debugafterload    = false; // level fully loaded and running
static int  count_begin       = 1;     // guards against acting during the pre-level boot count-up
static bool debugafter_215540 = false; // gates the dump-arming logic below
static bool g_dump_armed      = false; // writeseqall() has been called for this level
static long g_dump_frames     = 0;     // MAIN_LOOP_STEP_EIP hits seen since arming

void enginestep() {
    config_init_once();
    input_recorder_init_once();

    // ---- skip intro screens ----
    if (reg_eip == INTRO_SKIP_EIP) {
        mem_writeb(0x2A51AD, 1); // x_BYTE_D41AD_skip_screen = 1
    }

    // ---- jump the menu straight to "new game" ----
    if (reg_eip == SKIP_TO_NEWGAME_EIP) {
        mem_writed(0x2B2BAC + 0, 0x258350); // str_E1BAC[0].dword_0 = 0x258350
        mem_writew(0x2B2BAC + 8, 1);        // str_E1BAC[0].word_8  = 1
    }

    // ---- "new game" -> pick and run g_target_level ----
    if (reg_eip == RUN_LEVEL_EIP) {
        mem_writeb(0x34EB8E, 1); // x_DWORD_17DB70str.x_BYTE_17DB8E = 1
        Bit32u x_D41A0_BYTEARRAY_4_struct = mem_readd(X_D41A0_PTR);
        mem_writew(x_D41A0_BYTEARRAY_4_struct + 43, (Bit16u)g_target_level);

        if (mem_readb(0x2B2960 + g_target_level * 22 + 18) == 1)
            mem_writeb(x_D41A0_BYTEARRAY_4_struct + 38545,
                       mem_readb(x_D41A0_BYTEARRAY_4_struct + 38545) | 4u);

        int retval = -1;
        int ri = 0;
        if (!mem_readb(0x2b3970 + 17 * ri + 12))
            retval = 0;
        if (retval == -1) {
            while (g_target_level != mem_readw(0x2b3970 + 17 * ri + 4)) {
                ri++;
                if (!mem_readb(0x2b3970 + 17 * ri + 12))
                    retval = 0;
            }
            if (retval == -1)
                retval = 0x2b3970 + 17 * ri;
        }
        if (retval && mem_readb(retval + 12))
            mem_writeb(x_D41A0_BYTEARRAY_4_struct + 38545,
                       mem_readb(x_D41A0_BYTEARRAY_4_struct + 38545) | 0x10u);
        if (g_target_level == 24)
            mem_writeb(x_D41A0_BYTEARRAY_4_struct + 38545,
                       mem_readb(x_D41A0_BYTEARRAY_4_struct + 38545) | 0x20u);

        reg_eax = 1;
    }

    // ---- level finished loading: arm the frame dump ----
    if (reg_eip == AFTER_LOAD_EIP) {
        debugafterload = true;
        mem_writeb(0x38cf50 + 0x1e, 0x3d);  // fix same run after load
        mem_writew(0x34c4e0, 0x21ed);       // fix same run after load (RNG seed)

        if (!g_dump_armed) {
            g_dump_armed = true;
            g_dump_frames = 0;
            writeseqall(MAIN_LOOP_STEP_EIP, 0, g_dump_steps);
            fprintf(stderr, "[engine] level %d loaded, dumping %ld frames to %s\n",
                    g_target_level, g_dump_steps, g_level_dir.c_str());
        }
    }

    if (reg_eip == COUNT_INIT_EIP) {
        count_begin++;
    }

    if (reg_eip == GATE_ENABLE_EIP) {
        debugafter_215540 = true;
    }

    // ---- auto-close any pause dialog that pops up on level entry ----
    if ((reg_eip == MAIN_LOOP_STEP_EIP) && debugafterload) {
        mem_writeb(mem_readd(0x2A51A4) + 0x18, 0);
    }

    // ---- fix computer/AI speed so timing doesn't depend on host speed ----
    if (reg_eip == 0x26508d) {
        mem_writeb(0x35522c, 0x5);
    }

    // ---- misc fix carried over from the original file ----
    if (reg_eip == 0x2368e4) {
        mem_writed(0x3965c7, 0x35cf6e);
    }
    if (reg_eip == 0x238682) {
        mem_writed(0x38c684 + 0xa, 0x0);
    }

    // ---- deterministic player input (only if configured) ----
    if (m_InputRecorder != nullptr && reg_eip == TURN_ADVANCE_EIP)
        input_recorder_step();
    if (reg_eip == GAME_END_EIP)
        input_recorder_stop_if_needed();

    // ---- writeseqall() dispatch: dump memory on every MAIN_LOOP_STEP_EIP hit ----
    // NOTE: the original file also gated this on (count_begin == 1), which
    // only made sense for its "resume from a mid-level DOSBox savestate"
    // recipe, where count_begin never advances past its initial value of 1.
    // For our fresh-boot -> skip-menu -> load-level flow, COUNT_INIT_EIP
    // fires as part of loading that very level, so count_begin would very
    // plausibly already be 2 by the time debugafterload goes true - gating
    // on it here would silently produce empty dumps. count_begin is still
    // tracked above for diagnostics, just not used as a gate.
    if (debugafter_215540 && debugafterload) {
        for (int ii = 0; ii < lastwriteindexsequence; ii++) {
            if (writesequencecount2[ii] >= writesequencecount[ii]) continue;
            if (reg_eip != writesequencecodeadress[ii]) continue;
            if (writesequencesavefrom[ii] <= (Bit32u)writesequencecount2[ii])
                savesequence(ii, writesequencesize[ii], writesequencedataadress[ii]);
            writesequencecount2[ii]++;
        }
        if (g_dump_armed && reg_eip == MAIN_LOOP_STEP_EIP) {
            g_dump_frames++;
            if (writeseq_all_done()) {
                fprintf(stderr, "[engine] level %d pass %d: dumped %ld frames, exiting\n",
                        g_target_level, g_run_pass, g_dump_frames);
                exit(0);
            }
        }
    }

    saveInStep_check();
    loadInStep_check();
}

// ---------------------------------------------------------------------
// Called by DOSBox-X on every far call. Not currently used for anything
// in this driver - kept as a stub because engine.h declares it and the
// CPU core calls it unconditionally (src/cpu/cpu.cpp,
// src/cpu/core_normal/prefix_66.h). Add a case here if you need a
// call-site hook again.
// ---------------------------------------------------------------------
int engine_call(bool use32, Bitu selector, Bitu offset, Bitu oldeip) {
    (void)use32; (void)selector; (void)offset; (void)oldeip;
    return 0; // 0 = let DOSBox-X perform the call normally
}

// Called by DOSBox-X on every far return. Not currently used - kept as a
// stub for the same reason as engine_call() above.
void engine_ret(Bitu myreg_eip) {
    (void)myreg_eip;
}

// Unused by this driver, kept only because engine.h declares it and some
// other translation unit might still reference it.
void restart_calls() {
}

// ---------------------------------------------------------------------
// Heavy full-memory + register snapshot for manual deep-dive analysis of
// one specific point (e.g. the first byte compare_dumps.py flags as
// differing). Call from the debugger, or add a one-off
// "if (reg_eip == 0x...) saveactstate();" if you need it hands-free.
// Also wired to the debugger's SAVESTATE command (src/debug/debug.cpp).
// ---------------------------------------------------------------------
void saveactstate() {
    char name1[1024];
    sprintf(name1, "engine-registers-%04X-%08X.txt", SegValue(cs), reg_eip);
    char name2[1024];
    sprintf(name2, "engine-memory-%04X-%08X.bin", SegValue(cs), reg_eip);

    FILE* fptw1 = nullptr;
    fopen_s(&fptw1, name1, "wt");
    if (fptw1) {
        fprintf(fptw1, "%04X:%08X\n", SegValue(cs), reg_eip);
        fprintf(fptw1, "EAX:%08X,EBX:%08X,ECX:%08X,EDX:%08X\n", reg_eax, reg_ebx, reg_ecx, reg_edx);
        fprintf(fptw1, "ESI:%08X,EDI:%08X,EBP:%08X,ESP:%08X\n", reg_esi, reg_edi, reg_ebp, reg_esp);
        fprintf(fptw1, "CS:%04X,DS:%04X,ES:%04X,FS:%04X,GS:%04X,SS:%04X\n",
                SegValue(cs), SegValue(ds), SegValue(es), SegValue(fs), SegValue(gs), SegValue(ss));
        fprintf(fptw1, "CF:%01X,ZF:%01X,SF:%01X,OF:%01X,AF:%01X,PF:%01X,IF:%01X\n",
                (get_CF() > 0), (get_ZF() > 0), (get_SF() > 0), (get_OF() > 0),
                (get_AF() > 0), (get_PF() > 0), GETFLAGBOOL(IF));
        fclose(fptw1);
    }

    FILE* fptw = nullptr;
    fopen_s(&fptw, name2, "wb");
    if (fptw) {
        unsigned char buffer[1];
        for (long i = 0; i < 0x1000000; i++) {
            buffer[0] = (unsigned char)mem_readb(i);
            fwrite(buffer, 1, 1, fptw);
        }
        fclose(fptw);
    }
}
