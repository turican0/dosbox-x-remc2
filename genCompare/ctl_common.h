/*
 * ctl_common.h
 *
 * Sdilena cast "rizeni z venku" pro DOSBox-X (engine.cpp) i pro nativni
 * reimplementaci (native_ctl.cpp). Cte externi textovy konfiguracni
 * soubor, ktery rika:
 *   - CO ukladat/dumpovat a ZA JAKE PODMINKY (DUMP)
 *   - JAKE hodnoty v pameti zmenit a ZA JAKYCH PODMINEK (SET, jen DOSBox)
 *   - KDY skoncit / zavrit se (STOP)
 *
 * Podminky (Condition) podporuji: pocet cyklu (presne/vetsi-rovno/kazdych N),
 * dosazeni EIP, cil volani, zmenu hodnoty bunky v pameti, rovnost/nerovnost
 * hodnoty. Jednotlivym radkum lze dat vice podminek zaroven (AND).
 *
 * Cely soubor je zamerne jen hlavickovy (inline funkce) - staci ho
 * #includnout jak z DOSBox-X projektu, tak z nativni reimplementace, beze
 * zmeny. Specificke je jen NACITANI hodnot z pameti/promennych - to resi
 * kazda strana vlastnim "Read" callbackem (viz WatchTable nize).
 *
 * Format konfiguracniho souboru - viz DOSBOX_CTL_PROTOCOL.md.
 */
#ifndef CTL_COMMON_H
#define CTL_COMMON_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <functional>

// ============================================================
// Podminky
// ============================================================

enum class CondType {
    CYCLE_EQ, CYCLE_GE, EVERY, EIP, CALL, CALL_ANY, CHANGED, EQUALS, NOT_EQUALS
};

struct Condition {
    CondType type = CondType::CYCLE_EQ;
    unsigned long long n = 0;      // cycle / every
    unsigned int addr = 0;
    unsigned int width = 4;
    unsigned int value = 0;
    // interni stav pro CHANGED - hodnota pri predchozim vyhodnoceni.
    // -1 = jeste nebylo nacteno (prvni vyhodnoceni nikdy nesplni podminku,
    // jen si zapamatuje pocatecni hodnotu).
    mutable long long last_value = -1;
};

enum class ActionType { DUMP, SET, STOP };

struct Rule {
    ActionType action = ActionType::DUMP;
    std::vector<Condition> conds;      // AND
    std::string label;
    std::vector<std::string> fields;   // prazdne = vsechny sledovane symboly
    unsigned int set_addr = 0, set_width = 4, set_value = 0;
    bool repeat_always = true;         // DUMP: default 'always'; SET/STOP se prepnou na 'once' pri parsovani, pokud neni receno jinak
    bool fired = false;                // pro repeat=once
    int line_no = 0;
};

struct CtlConfig {
    std::string output_file = "trace.txt";
    std::vector<Rule> rules;
};

// ============================================================
// Parsovani konfiguracniho souboru
// ============================================================

inline std::vector<std::string> ctl_split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        size_t start = i;
        while (i < s.size() && !isspace((unsigned char)s[i])) i++;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

inline std::vector<std::string> ctl_split_char(const std::string& s, char c) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(c, start);
        if (p == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

inline unsigned int ctl_parse_hex_or_dec(const std::string& s) {
    return (unsigned int)strtoul(s.c_str(), nullptr, 0);  // "0x.." i desitkove
}

inline bool ctl_parse_condition(const std::string& spec, Condition& out) {
    // spec priklady: "cycle:1000", "cycle_ge:1000", "every:50000",
    //                "eip:0x2A51AD", "call:0x26D0D", "call:*",
    //                "changed:0x197F98:4", "eq:0x197F98:4:0x5", "neq:..."
    auto parts = ctl_split_char(spec, ':');
    if (parts.empty()) return false;
    const std::string& kind = parts[0];

    if (kind == "cycle" && parts.size() == 2) {
        out.type = CondType::CYCLE_EQ; out.n = strtoull(parts[1].c_str(), nullptr, 0); return true;
    }
    if (kind == "cycle_ge" && parts.size() == 2) {
        out.type = CondType::CYCLE_GE; out.n = strtoull(parts[1].c_str(), nullptr, 0); return true;
    }
    if (kind == "every" && parts.size() == 2) {
        out.type = CondType::EVERY; out.n = strtoull(parts[1].c_str(), nullptr, 0); return true;
    }
    if (kind == "eip" && parts.size() == 2) {
        out.type = CondType::EIP; out.addr = ctl_parse_hex_or_dec(parts[1]); return true;
    }
    if (kind == "call" && parts.size() == 2) {
        if (parts[1] == "*") { out.type = CondType::CALL_ANY; return true; }
        out.type = CondType::CALL; out.addr = ctl_parse_hex_or_dec(parts[1]); return true;
    }
    if (kind == "changed" && parts.size() == 3) {
        out.type = CondType::CHANGED;
        out.addr = ctl_parse_hex_or_dec(parts[1]);
        out.width = ctl_parse_hex_or_dec(parts[2]);
        return true;
    }
    if ((kind == "eq" || kind == "neq") && parts.size() == 4) {
        out.type = (kind == "eq") ? CondType::EQUALS : CondType::NOT_EQUALS;
        out.addr = ctl_parse_hex_or_dec(parts[1]);
        out.width = ctl_parse_hex_or_dec(parts[2]);
        out.value = ctl_parse_hex_or_dec(parts[3]);
        return true;
    }
    return false;
}

// Nacte konfiguracni soubor. Pri chybe v konkretnim radku vypise varovani
// na stderr a radek preskoci (nezastavuje cely beh - lepsi neco nez nic
// pri automatizovanem spusteni).
inline bool ctl_load_config(const std::string& path, CtlConfig& cfg) {
    FILE* f = fopen(path.c_str(), "rt");
    if (!f) {
        fprintf(stderr, "[ctl] nepodarilo se otevrit konfiguracni soubor: %s\n", path.c_str());
        return false;
    }
    char linebuf[2048];
    int line_no = 0;
    while (fgets(linebuf, sizeof(linebuf), f)) {
        line_no++;
        std::string line(linebuf);
        // osekej komentar (# az do konce radku, mimo hodnot - jednoduse
        // predpokladame, ze # se v hodnotach nepouziva)
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto toks = ctl_split_ws(line);
        if (toks.empty()) continue;

        std::string verb = toks[0];
        for (auto& c : verb) c = (char)toupper((unsigned char)c);

        std::map<std::string, std::string> kv;
        std::vector<std::string> cond_specs;
        for (size_t i = 1; i < toks.size(); i++) {
            auto eq = toks[i].find('=');
            if (eq == std::string::npos) continue;
            std::string k = toks[i].substr(0, eq);
            std::string v = toks[i].substr(eq + 1);
            if (k == "cond") cond_specs.push_back(v);
            else kv[k] = v;
        }

        if (verb == "OUTPUT") {
            if (kv.count("file")) cfg.output_file = kv["file"];
            continue;
        }

        Rule rule;
        rule.line_no = line_no;
        if (verb == "DUMP") rule.action = ActionType::DUMP;
        else if (verb == "SET") rule.action = ActionType::SET;
        else if (verb == "STOP") rule.action = ActionType::STOP;
        else {
            fprintf(stderr, "[ctl] radek %d: neznamy prikaz '%s', preskakuji\n", line_no, verb.c_str());
            continue;
        }

        bool cond_ok = true;
        for (auto& spec : cond_specs) {
            Condition c;
            if (!ctl_parse_condition(spec, c)) {
                fprintf(stderr, "[ctl] radek %d: neplatna podminka '%s'\n", line_no, spec.c_str());
                cond_ok = false;
                continue;
            }
            rule.conds.push_back(c);
        }
        if (!cond_ok || rule.conds.empty()) {
            fprintf(stderr, "[ctl] radek %d: pravidlo bez platne podminky, preskakuji\n", line_no);
            continue;
        }

        if (kv.count("label")) rule.label = kv["label"];
        if (kv.count("fields")) rule.fields = ctl_split_char(kv["fields"], ',');
        if (kv.count("repeat")) rule.repeat_always = (kv["repeat"] == "always");
        else rule.repeat_always = (rule.action == ActionType::DUMP);  // vychozi: DUMP=always, SET/STOP=once

        if (rule.action == ActionType::SET) {
            if (!kv.count("addr") || !kv.count("value")) {
                fprintf(stderr, "[ctl] radek %d: SET vyzaduje addr= a value=\n", line_no);
                continue;
            }
            rule.set_addr = ctl_parse_hex_or_dec(kv["addr"]);
            rule.set_width = kv.count("size") ? ctl_parse_hex_or_dec(kv["size"]) : 4;
            rule.set_value = ctl_parse_hex_or_dec(kv["value"]);
        }

        cfg.rules.push_back(rule);
    }
    fclose(f);
    fprintf(stderr, "[ctl] nacteno %d pravidel z %s (vystup: %s)\n",
            (int)cfg.rules.size(), path.c_str(), cfg.output_file.c_str());
    return true;
}

// ============================================================
// Watch tabulka sledovanych symbolu (generuje gen_watchtable.cpp)
// ============================================================

struct WatchEntry {
    const char* name;
    unsigned int addr;      // DOS adresa (pro vyhledani podle adresy u CHANGED/EQ/NEQ)
    unsigned int (*read)(); // funkce vracejici aktualni hodnotu - ruzna implementace na kazde strane
};

// ============================================================
// Vyhodnocovaci enginy - spolecna logika, backend-specificke jen ctl_read_mem
// ============================================================

struct CtlEngine {
    unsigned long long cycle = 0;
    const WatchEntry* symbols = nullptr;
    int symbols_count = 0;
    FILE* out = nullptr;

    // callbacky specificke pro danou stranu (DOSBox: mem_readX/mem_writeX;
    // nativni strana: vyhledani podle adresy ve watch tabulce)
    std::function<unsigned int(unsigned int addr, unsigned int width)> read_mem;
    std::function<void(unsigned int addr, unsigned int width, unsigned int value)> write_mem; // muze byt prazdna (nativni strana SET nepodporuje)
    std::function<void()> on_stop; // co udelat pri STOP (typicky zavrit soubor + exit(0))

    void open_output(const std::string& path) {
        if (out) fclose(out);
        out = fopen(path.c_str(), "wt");
        if (!out) fprintf(stderr, "[ctl] nepodarilo se otevrit vystupni soubor: %s\n", path.c_str());
    }

    void close_output() {
        if (out) { fclose(out); out = nullptr; }
    }

    bool eval_condition(const Condition& c, bool is_call_context, unsigned int call_addr, unsigned int cur_eip) const {
        switch (c.type) {
            case CondType::CYCLE_EQ:  return cycle == c.n;
            case CondType::CYCLE_GE:  return cycle >= c.n;
            case CondType::EVERY:     return c.n > 0 && (cycle % c.n) == 0;
            case CondType::EIP:       return !is_call_context && cur_eip == c.addr;
            case CondType::CALL:      return is_call_context && call_addr == c.addr;
            case CondType::CALL_ANY:  return is_call_context;
            case CondType::CHANGED: {
                unsigned int v = read_mem(c.addr, c.width);
                bool changed = (c.last_value >= 0) && ((unsigned int)c.last_value != v);
                c.last_value = (long long)v;
                return changed;
            }
            case CondType::EQUALS:     return read_mem(c.addr, c.width) == c.value;
            case CondType::NOT_EQUALS: return read_mem(c.addr, c.width) != c.value;
        }
        return false;
    }

    bool conds_hold(const Rule& r, bool is_call_context, unsigned int call_addr, unsigned int cur_eip) const {
        for (auto& c : r.conds)
            if (!eval_condition(c, is_call_context, call_addr, cur_eip))
                return false;
        return true;
    }

    void do_dump(const Rule& r) {
        if (!out) return;
        fprintf(out, "CKPT %s cycle=%llu\n", r.label.empty() ? "-" : r.label.c_str(), cycle);
        if (r.fields.empty()) {
            for (int i = 0; i < symbols_count; i++)
                fprintf(out, "%s=%08X\n", symbols[i].name, symbols[i].read());
        } else {
            for (auto& want : r.fields) {
                bool found = false;
                for (int i = 0; i < symbols_count; i++) {
                    if (want == symbols[i].name) {
                        fprintf(out, "%s=%08X\n", symbols[i].name, symbols[i].read());
                        found = true;
                        break;
                    }
                }
                if (!found) fprintf(out, "%s=?NOTFOUND\n", want.c_str());
            }
        }
        fprintf(out, "ENDCKPT\n");
        fflush(out);
    }

    void do_set(Rule& r) {
        if (write_mem) write_mem(r.set_addr, r.set_width, r.set_value);
        else fprintf(stderr, "[ctl] radek %d: SET neni na teto strane podporovan (jen DOSBox)\n", r.line_no);
    }

    void do_stop(Rule& r) {
        fprintf(stderr, "[ctl] STOP (radek %d, cyklus %llu)\n", r.line_no, cycle);
        close_output();
        if (on_stop) on_stop();
    }

    // Vyhodnoti vsechna pravidla. is_call_context/call_addr se pouziji jen
    // pro CALL podminky; pri normalnim kroku (ne volani) je is_call_context=false.
    void step(CtlConfig& cfg, bool is_call_context, unsigned int call_addr, unsigned int cur_eip) {
        for (auto& r : cfg.rules) {
            if (r.fired && !r.repeat_always) continue;
            if (!conds_hold(r, is_call_context, call_addr, cur_eip)) continue;

            switch (r.action) {
                case ActionType::DUMP: do_dump(r); break;
                case ActionType::SET:  do_set(r);  break;
                case ActionType::STOP: do_stop(r); break;
            }
            r.fired = true;
        }
    }
};

#endif
