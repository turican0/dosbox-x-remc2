/*
 * native_ctl.h / .cpp (jeden soubor, header+impl pro jednoduchost zapojeni)
 *
 * Obdoba engine.cpp pro nativni reimplementaci (reorion2). Cte STEJNY
 * format konfiguracniho souboru (viz DOSBOX_CTL_PROTOCOL.md), takze jedno
 * config muze rikat DOSBoxu co dumpovat/kdy skoncit a druhe (nebo i totez,
 * krome SET radku) muze rikat totez teto strane.
 *
 * Rozdily oproti DOSBox strane:
 *   - "cyklus" tady neni CPU cyklus, ale libovolny krok, ktery si sam
 *     definujes zavolanim native_ctl_tick() (typicky jednou za herni tah/
 *     frame, nebo jednou za kazde volani instrumentovane funkce).
 *   - CHANGED/EQ/NEQ podminky fungujici na "adresu" funguji jen pro adresy,
 *     ktere maji odpovidajici polozku ve watch tabulce (tj. je to jeden ze
 *     sledovanych globalu) - primy pristup do "pameti" jako u DOSBoxu tu
 *     neexistuje.
 *   - SET neni podporovan (nejde runtime "napsat" do C promenne podle
 *     libovolne adresy bez reflexe) - radky SET se jen ignoruji s varovanim.
 *
 * Pouziti (priklad):
 *   #include "trace_native_symbols.gen.cpp"   // vygenerovano gen_watchtable.cpp - MUSI byt PRVNI
 *   #include "native_ctl.h"
 *
 *   int main() {
 *       native_ctl_init();                 // nacte config (viz env below)
 *       ...
 *       while (hra_bezi) {
 *           herni_tah();
 *           native_ctl_tick();             // vyhodnoti cycle/every/changed/eq podminky
 *       }
 *   }
 *
 *   // uvnitr konkretni sub_XXXXX, kterou chces sledovat pro CALL podminky:
 *   void sub_26D0D(int a1) {
 *       native_ctl_on_call(0x26D0D);       // nebo NATIVE_CTL_ON_CALL(sub_26D0D)
 *       ...
 *   }
 *
 * Config soubor se hleda stejne jako na DOSBox strane:
 *   1. env. promenna DOSBOX_CTL_FILE (aby oba behy sly spustit se stejnym
 *      parem konfiguraku z jednoho externiho orchestratoru)
 *   2. jinak "dosbox_ctl.cfg" v aktualnim adresari
 */
#ifndef NATIVE_CTL_H
#define NATIVE_CTL_H

#include <cstdlib>
#include <cstring>
#include "ctl_common.h"

// g_watch_symbols/g_watch_symbols_count ocekavame JIZ DEFINOVANE - includni
// "trace_native_symbols.gen.cpp" (vygenerovano gen_watchtable.cpp) PRED
// timhle souborem.

static CtlConfig g_native_ctl_cfg;
static CtlEngine g_native_ctl;
static bool g_native_ctl_initialized = false;

// Cteni "pameti" na nativni strane = vyhledani podle adresy ve watch
// tabulce (nema smysl pro libovolnou adresu, jen pro znamy sledovany symbol).
inline unsigned int native_ctl_read_by_addr(unsigned int addr, unsigned int /*width*/) {
    for (int i = 0; i < g_native_ctl.symbols_count; i++) {
        if (g_native_ctl.symbols[i].addr == addr)
            return g_native_ctl.symbols[i].read();
    }
    fprintf(stderr, "[native_ctl] adresa 0x%X neni ve watch tabulce - CHANGED/EQ/NEQ podminka na ni nemuze fungovat\n", addr);
    return 0;
}

inline void native_ctl_default_stop() {
    exit(0);
}

// path == nullptr -> pouzij DOSBOX_CTL_FILE env. promennou, jinak "dosbox_ctl.cfg"
inline void native_ctl_init(const char* path = nullptr) {
    if (g_native_ctl_initialized) return;
    g_native_ctl_initialized = true;

    std::string cfg_path;
    if (path) cfg_path = path;
    else {
        const char* env_path = getenv("DOSBOX_CTL_FILE");
        cfg_path = env_path ? env_path : "dosbox_ctl.cfg";
    }

    ctl_load_config(cfg_path, g_native_ctl_cfg);

    g_native_ctl.symbols = g_watch_symbols;
    g_native_ctl.symbols_count = g_watch_symbols_count;
    g_native_ctl.read_mem = native_ctl_read_by_addr;
    g_native_ctl.write_mem = nullptr;  // SET neni na nativni strane podporovan
    g_native_ctl.on_stop = native_ctl_default_stop;
    g_native_ctl.open_output(g_native_ctl_cfg.output_file);
}

// Zavolej jednou za "krok" (herni tah/frame/instrumentovana funkce - podle
// tveho uvazeni). Vyhodnoti cycle/every/changed/eq/neq podminky.
inline void native_ctl_tick() {
    g_native_ctl.cycle++;
    g_native_ctl.step(g_native_ctl_cfg, /*is_call_context=*/false, /*call_addr=*/0, /*cur_eip=*/0);
}

// Zavolej na zacatku funkce sub_<addr>, kterou chces sledovat pro CALL podminky.
inline void native_ctl_on_call(unsigned int addr) {
    g_native_ctl.step(g_native_ctl_cfg, /*is_call_context=*/true, addr, /*cur_eip=*/0);
}

inline void native_ctl_shutdown() {
    g_native_ctl.close_output();
}

// Pohodlne makro - vytahne adresu ze jmena funkce (sub_XXXXX), stejne jako
// drivejsi TRACE_CHECKPOINT_FOR.
#define NATIVE_CTL_ON_CALL(fn_name) \
    do { \
        unsigned int _addr = 0; \
        sscanf(strchr(#fn_name, '_') + 1, "%X", &_addr); \
        native_ctl_on_call(_addr); \
    } while (0)

#endif
