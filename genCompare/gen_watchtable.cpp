/*
 * gen_watchtable.cpp
 *
 * Samostatny prikazoradkovy nastroj (zadne zavislosti krome standardni
 * knihovny). Projde orion_common.h a pro kazdou globalni promennou, jejiz
 * jmeno primo koduje DOS adresu (Hex-Rays konvence byte_/word_/dword_/...),
 * vygeneruje DVE tabulky se stejnymi jmeny/adresami:
 *
 *   trace_dosbox_symbols.gen.cpp  - ctecky pres mem_readb/w/d (pro engine.cpp)
 *   trace_native_symbols.gen.cpp  - ctecky primo na C promennou (pro reimplementaci)
 *
 * Preklad (priklad MSVC):
 *   cl /EHsc /std:c++17 gen_watchtable.cpp /Fe:gen_watchtable.exe
 * nebo (g++/clang):
 *   g++ -std=c++17 -o gen_watchtable gen_watchtable.cpp
 *
 * Pouziti:
 *   gen_watchtable cesta/k/orion_common.h vystupni_slozka/
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>
#include <algorithm>
#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

struct GlobalSym {
    std::string name;
    std::string addr_hex;   // bez "0x", velka pismena
    unsigned int width;     // 1, 2 nebo 4 (8bajtove typy zatim nepodporovany)
};

static const std::set<std::string> NAME_PREFIXES = { "byte", "word", "dword", "qword", "asc", "dbl", "flt" };

// sirka podle typu z deklarace (pokud rozpoznatelny), jinak podle prefixu jmena
static bool width_from_type(const std::string& type, bool is_ptr, unsigned int& width) {
    static const std::vector<std::pair<std::string, unsigned int>> table = {
        {"char", 1}, {"unsigned char", 1}, {"_BYTE", 1}, {"uint8_t", 1}, {"int8_t", 1},
        {"short", 2}, {"unsigned short", 2}, {"_WORD", 2}, {"int16_t", 2}, {"uint16_t", 2},
        {"int", 4}, {"unsigned int", 4}, {"_DWORD", 4}, {"int32_t", 4}, {"uint32_t", 4},
        {"float", 4},
    };
    if (is_ptr) { width = 4; return true; }
    for (auto& p : table) {
        if (type == p.first) { width = p.second; return true; }
    }
    return false;
}

static unsigned int width_from_prefix(const std::string& prefix) {
    if (prefix == "byte" || prefix == "asc") return 1;
    if (prefix == "word") return 2;
    if (prefix == "dword") return 4;
    return 0; // qword/dbl/flt (8 bajtu) - zatim nepodporovano timhle generatorem
}

static std::vector<GlobalSym> parse_header(const std::string& path) {
    std::vector<GlobalSym> out;
    std::ifstream f(path);
    if (!f) {
        fprintf(stderr, "Nelze otevrit %s\n", path.c_str());
        exit(1);
    }
    std::string line;
    bool in_data_section = false;

    // extern <type> [*] <name> [pole] ;
    static const std::regex decl_re(
        R"(^extern\s+([A-Za-z_][A-Za-z0-9_ ]*?)\s*(\*+)?\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*;)"
    );
    static const std::regex name_addr_re(R"(^(byte|word|dword|qword|asc|dbl|flt)_([0-9A-Fa-f]+)$)");

    while (std::getline(f, line)) {
        // osekej CRLF
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = line;
        size_t s = trimmed.find_first_not_of(" \t");
        if (s != std::string::npos) trimmed = trimmed.substr(s); else trimmed.clear();

        if (trimmed.find("extern deklarace vsech globalnich dat") != std::string::npos) {
            in_data_section = true;
            continue;
        }
        if (!in_data_section) continue;
        if (trimmed.rfind("#if 0", 0) == 0 || trimmed.rfind("#endif", 0) == 0) break;
        if (trimmed.rfind("extern ", 0) != 0) continue;

        std::smatch m;
        if (!std::regex_search(trimmed, m, decl_re)) continue;

        std::string type = m[1].str();
        // osekej mezery na konci typu
        while (!type.empty() && (type.back() == ' ')) type.pop_back();
        bool is_ptr = m[2].matched;
        std::string name = m[3].str();
        bool is_array = m[4].matched;
        if (is_array) continue;  // pole potrebuji rucni rozsah - viz protokol

        std::smatch nm;
        if (!std::regex_search(name, nm, name_addr_re)) continue;
        std::string prefix = nm[1].str();
        std::string addr_hex = nm[2].str();
        for (auto& c : addr_hex) c = (char)toupper((unsigned char)c);

        unsigned int width = 0;
        if (!width_from_type(type, is_ptr, width)) width = width_from_prefix(prefix);
        if (width != 1 && width != 2 && width != 4) continue;

        out.push_back({ name, addr_hex, width });
    }
    return out;
}

static const char* read_fn_for_width(unsigned int w) {
    if (w == 1) return "mem_readb";
    if (w == 2) return "mem_readw";
    return "mem_readd";
}

static void write_dosbox(const std::vector<GlobalSym>& syms, const std::string& out_dir) {
    std::ofstream f(out_dir + "/trace_dosbox_symbols.gen.cpp");
    f << "/* AUTOGENEROVANO gen_watchtable.cpp - needituj rucne. */\n";
    f << "/* Prida se do engine.cpp (#include). Cte hodnoty z emulovane pameti DOSBoxu. */\n\n";
    for (auto& s : syms) {
        f << "static unsigned int read_" << s.name << "() { return (unsigned int)"
          << read_fn_for_width(s.width) << "(0x" << s.addr_hex << "); }\n";
    }
    f << "\nstatic const WatchEntry g_watch_symbols[] = {\n";
    for (auto& s : syms) {
        f << "    { \"" << s.name << "\", 0x" << s.addr_hex << ", read_" << s.name << " },\n";
    }
    f << "};\n";
    f << "static const int g_watch_symbols_count = " << syms.size() << ";\n";
}

static void write_native(const std::vector<GlobalSym>& syms, const std::string& out_dir) {
    std::ofstream f(out_dir + "/trace_native_symbols.gen.cpp");
    f << "/* AUTOGENEROVANO gen_watchtable.cpp - needituj rucne. */\n";
    f << "/* Prida se do reimplementace (reorion2) - vyzaduje #include \"orion_common.h\" */\n";
    f << "#include \"orion_common.h\"\n\n";
    for (auto& s : syms) {
        f << "static unsigned int read_" << s.name << "() { return (unsigned int)" << s.name << "; }\n";
    }
    f << "\nstatic const WatchEntry g_watch_symbols[] = {\n";
    for (auto& s : syms) {
        f << "    { \"" << s.name << "\", 0x" << s.addr_hex << ", read_" << s.name << " },\n";
    }
    f << "};\n";
    f << "static const int g_watch_symbols_count = " << syms.size() << ";\n";
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Pouziti: %s cesta/k/orion_common.h vystupni_slozka/\n", argv[0]);
        return 1;
    }
    std::string header_path = argv[1];
    std::string out_dir = argv[2];
    MKDIR(out_dir.c_str());

    auto syms = parse_header(header_path);
    write_dosbox(syms, out_dir);
    write_native(syms, out_dir);

    unsigned int w1 = 0, w2 = 0, w4 = 0;
    for (auto& s : syms) { if (s.width == 1) w1++; else if (s.width == 2) w2++; else w4++; }

    printf("Nalezeno %zu sledovatelnych skalarnich symbolu (1B: %u, 2B: %u, 4B: %u).\n",
           syms.size(), w1, w2, w4);
    printf("Vygenerovano:\n  %s/trace_dosbox_symbols.gen.cpp\n  %s/trace_native_symbols.gen.cpp\n",
           out_dir.c_str(), out_dir.c_str());
    return 0;
}
