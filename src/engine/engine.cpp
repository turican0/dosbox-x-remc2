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
#include "vga.h"

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

// ---- DUMPREGS: rozsireni jen pro DOSBox stranu ----
// Radek "DUMPREGS cond=eip:0xADDR label=jmeno" ve STEJNEM ctl souboru
// zapise pri kazdem zasahu EIP jednoradkovy vypis vsech registru + navratove
// adresy ze zasobniku (ret=) do vystupniho trace souboru. K cemu to je:
// Watcom register calling convention predava argumenty v EAX/EDX/EBX/ECX,
// takze DUMPREGS na VSTUPU funkce (eip = adresa sub_XXXXX) ukaze presne
// hodnoty argumentu, se kterymi original funkci vola - idealni na overovani
// podezrelych konstant z dekompilace (napr. velikosti alokaci, ktere IDA
// mylne prevedla na "&symbol + offset"). "ret=" (dword na [ESP]) navic
// identifikuje volajici misto, takze jde odfiltrovat volani jen z jedne
// konkretni funkce. Sdileny parser v ctl_common.h tyhle radky nezna a jen
// je s varovanim preskoci - zamerne ho nemodifikujeme (je sdileny s
// nativni stranou), parsujeme si je tady sami.
// Dva rezimy watche:
//   cond=eip:0xADDR - zasah presne adresy (vyzaduje znat runtime adresy!)
//   cond=eax:0xVAL  - EAX prave NABYL dane hodnoty (hranovy trigger - loguje
//                     se jen prvni instrukce, po ktere EAX hodnotu ma, ne
//                     kazda dalsi). Nezavisle na adresach - idealni na
//                     kalibraci posunu runtime vs. IDA adres: chytne uz
//                     "mov eax, imm" u volajiciho a EIP zaznamu prozradi,
//                     kde kod skutecne bezi.
struct RegsWatch {
    unsigned int eip = 0;        // 0 = neni eip watch
    unsigned int eax_value = 0;
    bool eax_mode = false;
    bool eax_was_match = false;  // stav hranoveho triggeru
    std::string label;
};
static std::vector<RegsWatch> g_regs_watches;

static void ctl_load_dumpregs(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rt");
    if (!f) return; // chybejici soubor uz ohlasil ctl_load_config
    char linebuf[2048];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        std::string line(linebuf);
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto toks = ctl_split_ws(line);
        if (toks.empty()) continue;
        std::string verb = toks[0];
        for (auto& c : verb) c = (char)toupper((unsigned char)c);
        if (verb != "DUMPREGS") continue;

        RegsWatch w;
        for (size_t i = 1; i < toks.size(); i++) {
            auto eq = toks[i].find('=');
            if (eq == std::string::npos) continue;
            std::string k = toks[i].substr(0, eq);
            std::string v = toks[i].substr(eq + 1);
            if (k == "cond" && v.rfind("eip:", 0) == 0)
                w.eip = ctl_parse_hex_or_dec(v.substr(4));
            else if (k == "cond" && v.rfind("eax:", 0) == 0) {
                w.eax_mode = true;
                w.eax_value = ctl_parse_hex_or_dec(v.substr(4));
            }
            else if (k == "label")
                w.label = v;
        }
        if (w.eip || w.eax_mode) g_regs_watches.push_back(w);
        else fprintf(stderr, "[ctl] DUMPREGS bez platne cond=eip:/eax:, preskakuji\n");
    }
    fclose(f);
    if (!g_regs_watches.empty())
        fprintf(stderr, "[ctl] DUMPREGS: %d sledovanych EIP bodu\n", (int)g_regs_watches.size());
}

// ---- DUMPMEM: vypis useku emulovane pameti (vlna 13 reorion2) ----
// Radek "DUMPMEM cond=eip:0xADDR addr=0xA size=N label=x" vypise pri
// PRVNIM zasahu EIP obsah pameti [addr, addr+size) jako hex radek
// "MEM <label> addr=... size=N bytes=00112233..." do trace souboru.
// Pouziti: vytazeni puvodniho STROJOVEHO KODU funkce, jejiz dekompilace
// selhala (Hex-Rays "could not find valid save-restore pair" a telo
// zredukovane na "while(1);"), primo z bezici hry - hex se pak
// disassembluje rucne a porovna s dekompilatem.
struct MemWatch {
    unsigned int eip = 0;
    unsigned int addr = 0;
    unsigned int size = 0;
    bool fired = false;
    std::string label;
};
static std::vector<MemWatch> g_mem_watches;

static void ctl_load_dumpmem(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rt");
    if (!f) return;
    char linebuf[2048];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        std::string line(linebuf);
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto toks = ctl_split_ws(line);
        if (toks.empty()) continue;
        std::string verb = toks[0];
        for (auto& c : verb) c = (char)toupper((unsigned char)c);
        if (verb != "DUMPMEM") continue;

        MemWatch w;
        for (size_t i = 1; i < toks.size(); i++) {
            auto eq = toks[i].find('=');
            if (eq == std::string::npos) continue;
            std::string k = toks[i].substr(0, eq);
            std::string v = toks[i].substr(eq + 1);
            if (k == "cond" && v.rfind("eip:", 0) == 0)
                w.eip = ctl_parse_hex_or_dec(v.substr(4));
            else if (k == "addr") w.addr = ctl_parse_hex_or_dec(v);
            else if (k == "size") w.size = ctl_parse_hex_or_dec(v);
            else if (k == "label") w.label = v;
        }
        if (w.eip && w.addr && w.size)
            g_mem_watches.push_back(w);
        else
            fprintf(stderr, "[ctl] DUMPMEM potrebuje cond=eip:, addr=, size=, preskakuji\n");
    }
    fclose(f);
    if (!g_mem_watches.empty())
        fprintf(stderr, "[ctl] DUMPMEM: %d sledovanych useku\n", (int)g_mem_watches.size());
}

// ---- DUMPPAL: vypis SKUTECNE vykreslovane VGA DAC palety (reorion2 vlna 25) ----
// Radek "DUMPPAL cond=eip:0xADDR start=N count=M label=x [repeat=always]"
// vypise pri zasahu EIP obsah `vga.dac.rgb[start .. start+count)` - tedy
// hodnoty, ktere DOSBox-X SKUTECNE POUZIVA k vykresleni (na rozdil od
// DUMPMEM, ktery cte jen herni pamet PRED prevodem pres hr_outbyte/DAC).
// K cemu: reorion2 port ma vlastni g_palette[] (viz port_vga.cpp), a otazka
// "shoduje se prubeh/skladba fade rampy se skutecnym originalem" se da
// overit jen porovnanim SKUTECNE zobrazovanych barev, ne jen zdrojovych dat
// v pameti hry (ktera muze byt spravna, ale cesta k DAC/Present rozdilna -
// presne takovy bug uz byl v portu dvakrat: chybejici 6->8bit skalovani a
// roztrzena/neatomicka aktualizace pulky palety mezi dvema Present() volani).
// Kazdy zaznam: "PAL <label> cycle=<N> start=<S> count=<C> rgb=RRGGBB...".
// Hodnoty jsou 6-bitove (0-63) presne jak je VGA DAC uklada - k porovnani s
// portem je nutne portovni 8bit hodnotu vydelit 4 (nebo portovni >>2), NE
// naopak sklalovat DOSBox stranu.
struct PalWatch {
    unsigned int eip = 0;
    unsigned int start = 0;
    unsigned int count = 0;
    bool repeat_always = false;
    bool fired = false;
    std::string label;
};
static std::vector<PalWatch> g_pal_watches;

static void ctl_load_dumppal(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rt");
    if (!f) return;
    char linebuf[2048];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        std::string line(linebuf);
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto toks = ctl_split_ws(line);
        if (toks.empty()) continue;
        std::string verb = toks[0];
        for (auto& c : verb) c = (char)toupper((unsigned char)c);
        if (verb != "DUMPPAL") continue;

        PalWatch w;
        w.count = 256; // vychozi: cely DAC
        for (size_t i = 1; i < toks.size(); i++) {
            auto eq = toks[i].find('=');
            if (eq == std::string::npos) continue;
            std::string k = toks[i].substr(0, eq);
            std::string v = toks[i].substr(eq + 1);
            if (k == "cond" && v.rfind("eip:", 0) == 0)
                w.eip = ctl_parse_hex_or_dec(v.substr(4));
            else if (k == "start") w.start = ctl_parse_hex_or_dec(v);
            else if (k == "count") w.count = ctl_parse_hex_or_dec(v);
            else if (k == "label") w.label = v;
            else if (k == "repeat" && v == "always") w.repeat_always = true;
        }
        if (w.eip)
            g_pal_watches.push_back(w);
        else
            fprintf(stderr, "[ctl] DUMPPAL potrebuje cond=eip:, preskakuji\n");
    }
    fclose(f);
    if (!g_pal_watches.empty())
        fprintf(stderr, "[ctl] DUMPPAL: %d sledovanych useku\n", (int)g_pal_watches.size());
}

static void ctl_check_dumppal() {
    if (g_pal_watches.empty() || !g_ctl.out) return;
    const unsigned int eip = (unsigned int)reg_eip;
    for (auto& w : g_pal_watches) {
        if (eip != w.eip) continue;
        if (w.fired && !w.repeat_always) continue;
        w.fired = true;
        fprintf(g_ctl.out, "PAL %s cycle=%llu start=%u count=%u rgb=",
                w.label.empty() ? "-" : w.label.c_str(), g_ctl.cycle, w.start, w.count);
        for (unsigned int i = 0; i < w.count; i++) {
            unsigned int idx = w.start + i;
            if (idx > 0xFF) break;
            fprintf(g_ctl.out, "%02X%02X%02X",
                    (unsigned int)vga.dac.rgb[idx].red,
                    (unsigned int)vga.dac.rgb[idx].green,
                    (unsigned int)vga.dac.rgb[idx].blue);
        }
        fprintf(g_ctl.out, "\n");
        fflush(g_ctl.out);
    }
}

// ---- DUMPFRAME: hromadny dump vsech snimku+palety pro automatizovane
// porovnani s portem (reorion2 vlna 25p, na zadost uzivatele) ----
// Radek "DUMPFRAME cond=eip:0xADDR framebuf=0xADDR width=W height=H dir=PATH
// [label=x] [maxcount=N]" - pri KAZDEM zasahu EIP zapise JEDEN soubor
// "<dir>/frame_NNNNN.raw" obsahujici:
//   256*3 bajtu   - aktualni vga.dac.rgb[] paleta (6bit R,G,B na zaznam,
//                   presne jako DUMPPAL - port musi pri srovnani sve 8bit
//                   hodnoty vydelit 4/posunout >>2)
//   width*height  - aktualni obsah framebuf (indexovane pixely, PRED DAC
//                   prevodem - stejny format jako port_vga.cpp's
//                   DumpFrameIfRequested() raw dump)
// Format je bit-identicky s portovnim `port_frame.raw`, takze srovnavaci
// nastroj (viz genCompare/compare_frames.*) muze cist obe strany stejne.
// Zamerne PRIMY čtec framebuf pameti (ne interni dosbox render buffer) -
// framebuf musi byt overena adresa skutecneho VESA backbufferu (viz
// DOSBOX_CTL_PROTOCOL.md), stejna trida jako DUMPMEM.
struct FrameWatch {
    unsigned int eip = 0;
    unsigned int framebuf = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int maxcount = 0xFFFFFFFFu;
    unsigned int count = 0;
    std::string dir;
    std::string label;
    // Dedup safety net (reorion2 wave 25p, per user feedback): both sides of
    // this comparison should capture "a frame was actually drawn", not "a
    // periodic tick happened". sub_125814 SHOULD only fire on real dirty
    // redraws, but guard against it firing on unchanged content anyway so
    // the file sequence stays aligned with the port's own dedup'd dump.
    std::vector<uint8_t> last_pal;
    std::vector<uint8_t> last_fb;
    bool have_prev = false;
};
static std::vector<FrameWatch> g_frame_watches;

static void ctl_load_dumpframe(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rt");
    if (!f) return;
    char linebuf[2048];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        std::string line(linebuf);
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto toks = ctl_split_ws(line);
        if (toks.empty()) continue;
        std::string verb = toks[0];
        for (auto& c : verb) c = (char)toupper((unsigned char)c);
        if (verb != "DUMPFRAME") continue;

        FrameWatch w;
        for (size_t i = 1; i < toks.size(); i++) {
            auto eq = toks[i].find('=');
            if (eq == std::string::npos) continue;
            std::string k = toks[i].substr(0, eq);
            std::string v = toks[i].substr(eq + 1);
            if (k == "cond" && v.rfind("eip:", 0) == 0)
                w.eip = ctl_parse_hex_or_dec(v.substr(4));
            else if (k == "framebuf") w.framebuf = ctl_parse_hex_or_dec(v);
            else if (k == "width") w.width = ctl_parse_hex_or_dec(v);
            else if (k == "height") w.height = ctl_parse_hex_or_dec(v);
            else if (k == "maxcount") w.maxcount = ctl_parse_hex_or_dec(v);
            else if (k == "dir") w.dir = v;
            else if (k == "label") w.label = v;
        }
        if (w.eip && w.framebuf && w.width && w.height && !w.dir.empty())
            g_frame_watches.push_back(w);
        else
            fprintf(stderr, "[ctl] DUMPFRAME potrebuje cond=eip:, framebuf=, width=, height=, dir=, preskakuji\n");
    }
    fclose(f);
    if (!g_frame_watches.empty())
        fprintf(stderr, "[ctl] DUMPFRAME: %d sledovanych useku\n", (int)g_frame_watches.size());
}

static void ctl_check_dumpframe() {
    if (g_frame_watches.empty()) return;
    const unsigned int eip = (unsigned int)reg_eip;
    for (auto& w : g_frame_watches) {
        if (eip != w.eip || w.count >= w.maxcount) continue;

        // Snapshot current palette+framebuffer first, THEN compare against
        // the last dumped snapshot - only write out (and advance the
        // sequence) if something actually changed. See FrameWatch comment.
        std::vector<uint8_t> pal(256 * 3);
        for (unsigned int i = 0; i < 256; i++) {
            pal[i * 3 + 0] = vga.dac.rgb[i].red;
            pal[i * 3 + 1] = vga.dac.rgb[i].green;
            pal[i * 3 + 2] = vga.dac.rgb[i].blue;
        }
        std::vector<uint8_t> fb(w.width * w.height);
        for (unsigned int i = 0; i < w.width * w.height; i++)
            fb[i] = (uint8_t)mem_readb(w.framebuf + i);

        bool changed = !w.have_prev || pal != w.last_pal || fb != w.last_fb;
        if (!changed) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%05u.raw", w.dir.c_str(), w.count);
        FILE* fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "[ctl] DUMPFRAME: nelze otevrit %s pro zapis\n", path);
            continue;
        }
        fwrite(pal.data(), 1, pal.size(), fp);
        fwrite(fb.data(), 1, fb.size(), fp);
        fclose(fp);
        if (g_ctl.out) {
            fprintf(g_ctl.out, "FRAME %s cycle=%llu index=%u file=%s\n",
                    w.label.empty() ? "-" : w.label.c_str(), g_ctl.cycle, w.count, path);
            fflush(g_ctl.out);
        }
        w.last_pal = std::move(pal);
        w.last_fb = std::move(fb);
        w.have_prev = true;
        w.count++;
    }
}

static void ctl_check_dumpmem() {
    if (g_mem_watches.empty() || !g_ctl.out) return;
    const unsigned int eip = (unsigned int)reg_eip;
    for (auto& w : g_mem_watches) {
        if (w.fired || eip != w.eip) continue;
        w.fired = true;
        fprintf(g_ctl.out, "MEM %s addr=%08X size=%u bytes=",
                w.label.empty() ? "-" : w.label.c_str(), w.addr, w.size);
        for (unsigned int i = 0; i < w.size; i++)
            fprintf(g_ctl.out, "%02X", (unsigned int)mem_readb(w.addr + i));
        fprintf(g_ctl.out, "\n");
        fflush(g_ctl.out);
    }
}

static void ctl_check_dumpregs() {
    if (g_regs_watches.empty() || !g_ctl.out) return;
    const unsigned int eip = (unsigned int)reg_eip;
    for (auto& w : g_regs_watches) {
        if (w.eax_mode) {
            bool match = ((unsigned int)reg_eax == w.eax_value);
            bool edge = match && !w.eax_was_match;
            w.eax_was_match = match;
            if (!edge) continue;
        } else {
            if (eip != w.eip) continue;
        }
        // ret = dword na vrcholu zasobniku - na VSTUPU funkce je to navratova
        // adresa (u DOS4GW flat modelu je baze SS typicky 0, SegPhys to resi
        // obecne i kdyby nebyla).
        unsigned int ret = (unsigned int)mem_readd(SegPhys(ss) + reg_esp);
        fprintf(g_ctl.out,
                "REGS %s cycle=%llu eip=%08X eax=%08X ebx=%08X ecx=%08X edx=%08X"
                " esi=%08X edi=%08X ebp=%08X esp=%08X ret=%08X\n",
                w.label.empty() ? "-" : w.label.c_str(), g_ctl.cycle, eip,
                (unsigned int)reg_eax, (unsigned int)reg_ebx, (unsigned int)reg_ecx,
                (unsigned int)reg_edx, (unsigned int)reg_esi, (unsigned int)reg_edi,
                (unsigned int)reg_ebp, (unsigned int)reg_esp, ret);
        fflush(g_ctl.out);
    }
}

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
    ctl_load_dumpregs(cfg_path);
    ctl_load_dumpmem(cfg_path);
    ctl_load_dumppal(cfg_path);
    ctl_load_dumpframe(cfg_path);

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

    // DUMPREGS body (registry pri zasahu EIP) - viz komentar u RegsWatch.
    ctl_check_dumpregs();
    // DUMPMEM body (vypis pameti pri zasahu EIP) - viz komentar u MemWatch.
    ctl_check_dumpmem();
    // DUMPPAL body (vypis skutecne VGA DAC palety pri zasahu EIP) - viz PalWatch.
    ctl_check_dumppal();
    // DUMPFRAME body (hromadny snimek+paleta dump) - viz FrameWatch.
    ctl_check_dumpframe();

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
    /*fprintf(fptw1, "CF:%01X,ZF:%01X,SF:%01X,OF:%01X,AF:%01X,PF:%01X,IF:%01X\n",
            (get_CF() > 0), (get_ZF() > 0), (get_SF() > 0), (get_OF() > 0),
            (get_AF() > 0), (get_PF() > 0), GETFLAGBOOL(IF));*/
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
