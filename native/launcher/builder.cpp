// Builds the game on the player's own PC. The toolkit holds no part of the game: the player's own main.dol is
// translated to C here with DolRecomp, the runtime's hooks are added, and the toolkit's compiler builds it.
//
//   builder/dolrecomp.exe      the translator (DolRecomp, GPL-3.0), and zlib's DLL it uses
//   builder/src/               this project's runtime, as source, and replacements.txt (the hooked functions)
//   builder/dolrecomp/         DolRecomp's CPU header and helpers, compiled with the game
//   builder/lib/               the graphics library (aurora and what it uses), prebuilt, and link.rsp
//   builder/toolchain/         GCC and binutils (MSYS2's MinGW-w64 UCRT64 packages), and mingw32-make
//   builder/game.mk            the build
// Work goes to build/ beside the launcher, the game to Party9Recomp-Game.exe, a note of what was built to
// game-build.txt, and the compiler's output to build-log.txt.
#include "builder.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace builder {
namespace {

std::wstring quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Runs a program in cwd with the toolkit's compiler first on PATH, its output appended to dir's log. tick() is
// called about once a second while it runs. Returns the exit code, or -1 if it couldn't start.
int run(const fs::path& dir, const fs::path& cwd, const std::wstring& command, const std::function<void()>& tick) {
    std::wstring path = (dir / "builder" / "toolchain" / "bin").wstring() + L";" + (dir / "builder").wstring() + L";";
    wchar_t old_path[32768];
    DWORD got = GetEnvironmentVariableW(L"PATH", old_path, 32768);
    std::wstring new_path = path + (got && got < 32768 ? std::wstring(old_path) : L"");
    // The child gets this environment block: everything as it is, with PATH changed.
    std::wstring block;
    LPWCH env = GetEnvironmentStringsW();
    for (LPWCH p = env; *p; p += wcslen(p) + 1) {
        if (_wcsnicmp(p, L"PATH=", 5) == 0) continue;
        block += p;
        block.push_back(L'\0');
    }
    FreeEnvironmentStringsW(env);
    block += L"PATH=" + new_path;
    block.push_back(L'\0');
    block.push_back(L'\0');

    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE log = CreateFileW((dir / "build-log.txt").wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = command;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             block.data(), cwd.wstring().c_str(), &si, &pi);
    if (!ok) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        return -1;
    }
    while (WaitForSingleObject(pi.hProcess, 1000) == WAIT_TIMEOUT)
        if (tick) tick();
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    return int(code);
}

void note(const fs::path& dir, const std::string& line) {
    std::ofstream f(dir / "build-log.txt", std::ios::app);
    f << "== " << line << "\n";
}

// The runtime replaces some of the game's functions with its own. A call inside one chunk jumps straight to its
// target, so each hook goes right after the function's entry label (what scripts/44_patch_replacements.py does
// for this project's own build). Every chunk is copied to build/chunks with the include made local.
std::string add_hooks(const fs::path& dir) {
    std::vector<std::string> addresses;
    std::istringstream list(read_text(dir / "builder" / "src" / "replacements.txt"));
    std::string line;
    while (std::getline(list, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        std::string address;
        if (words >> address) {
            for (char& c : address) c = char(toupper((unsigned char)c));
            addresses.push_back(address);
        }
    }
    fs::path from = dir / "build" / "recomp" / "generated" / "chunks", to = dir / "build" / "chunks";
    std::error_code ec;
    fs::create_directories(to, ec);
    struct Chunk {
        uint32_t start;
        fs::path path;
    };
    std::vector<Chunk> chunks;
    static const std::regex start_of(R"(_([0-9A-F]{8})\.c$)");
    for (auto& e : fs::directory_iterator(from, ec)) {
        std::smatch m;
        std::string name = e.path().filename().string();
        if (std::regex_search(name, m, start_of)) chunks.push_back({uint32_t(std::stoul(m[1].str(), nullptr, 16)), e.path()});
    }
    if (chunks.empty()) return "The translation has no code in it. See build-log.txt.";
    std::sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) { return a.start < b.start; });
    std::vector<std::vector<std::string>> hooks(chunks.size());
    for (const std::string& a : addresses) {
        uint32_t value = uint32_t(std::stoul(a, nullptr, 16));
        size_t owner = 0;
        for (size_t i = 0; i < chunks.size() && chunks[i].start <= value; i++) owner = i;
        hooks[owner].push_back(a);
    }
    for (size_t i = 0; i < chunks.size(); i++) {
        std::string text = read_text(chunks[i].path);
        const std::string include = "#include \"../generated.h\"";
        size_t at = text.find(include);
        if (at != std::string::npos)
            text.replace(at, include.size(), "#include \"generated.h\"\nint native_replaced(CPUState* ctx, u32 pc);");
        for (const std::string& a : hooks[i]) {
            // The translator writes Windows line endings; the hook goes after the label's line, whichever ending.
            std::string label = "label_" + a + ":";
            size_t pos = text.find(label);
            size_t end = pos == std::string::npos ? pos : text.find('\n', pos);
            if (pos == std::string::npos || end == std::string::npos || text.find(label, pos + 1) != std::string::npos)
                return "The translation doesn't have the function at " + a + " where the runtime expects it.";
            text.insert(end + 1, "    if (native_replaced(ctx, 0x" + a + "u)) return;\n");
        }
        std::ofstream out(to / chunks[i].path.filename(), std::ios::binary);
        out << text;
        if (!out) return "Couldn't write " + (to / chunks[i].path.filename()).u8string() + ".";
    }
    return "";
}

// ---- Mario Party 9's REL modules (what scripts/rel_build.py does for this project's own build) ----
// Nearly all of the game's code is in modules under files/modules, loaded while it runs. Each is translated for its
// own fixed address, its slot; the runtime's linker (src/rel.c) puts every module the game loads in its slot. A
// module's kind is its name without the trailing number (bd03 is a "bd"); modules of the kinds in one slot never
// run together, so they share it, and the runtime stops with a report if two ever overlap. The boards, menu,
// results and party modes each carry the whole board engine (1.2 to 1.7 MB), so they get four slots of their own.
// Slots sit above the game's 24 MB, below 0x82000000 so main.dol stays in reach of the modules' branches. Keep in
// step with scripts/rel_build.py.
struct Slot {
    const char* kinds; // space-separated
    uint32_t base;
};
const Slot kSlots[] = {{"strap", 0x81800000u},          {"Introduction", 0x81801000u}, {"inst", 0x81802000u},
                       {"menu_title", 0x81814000u},     {"advpoint", 0x8182C000u},     {"constellation", 0x81848000u},
                       {"extra", 0x8186D000u},          {"gift", 0x818A1000u},         {"sme_op", 0x818EC000u},
                       {"sme_ed", 0x81925000u},         {"sme_stage", 0x8195B000u},    {"mg", 0x81989000u},
                       {"menu staff", 0x81A19000u},     {"bd bdtutorial", 0x81B89000u},
                       {"mgresult bdresults", 0x81D2A000u}, {"mgm", 0x81E65000u}};
constexpr uint32_t kSlotTop = 0x82000000u;
constexpr int kSlotCount = int(sizeof kSlots / sizeof *kSlots);

int slot_of(const std::string& name) {
    std::string kind = name;
    while (!kind.empty() && kind.back() >= '0' && kind.back() <= '9') kind.pop_back();
    for (int i = 0; i < kSlotCount; i++) {
        std::istringstream kinds(kSlots[i].kinds);
        std::string k;
        while (kinds >> k)
            if (k == kind) return i;
    }
    return -1;
}

uint32_t be32(const std::string& b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return uint32_t((unsigned char)b[o]) << 24 | uint32_t((unsigned char)b[o + 1]) << 16 |
           uint32_t((unsigned char)b[o + 2]) << 8 | uint32_t((unsigned char)b[o + 3]);
}

// Nintendo's LZ11, the modules' compression (a public format).
bool unlz11(const std::string& in, std::string& out) {
    if (in.size() < 4 || (unsigned char)in[0] != 0x11) return false;
    size_t size = (unsigned char)in[1] | (unsigned char)in[2] << 8 | (unsigned char)in[3] << 16, pos = 4;
    if (size == 0) {
        if (in.size() < 8) return false;
        size = (unsigned char)in[4] | (unsigned char)in[5] << 8 | (unsigned char)in[6] << 16 | size_t((unsigned char)in[7]) << 24;
        pos = 8;
    }
    out.clear();
    out.reserve(size);
    auto at = [&](size_t i) -> unsigned { return i < in.size() ? (unsigned char)in[i] : 0u; };
    while (out.size() < size) {
        if (pos >= in.size()) return false;
        unsigned flags = at(pos++);
        for (int bit = 7; bit >= 0 && out.size() < size; bit--) {
            if (!(flags & (1u << bit))) {
                if (pos >= in.size()) return false;
                out.push_back(in[pos++]);
                continue;
            }
            unsigned b0 = at(pos), ind = b0 >> 4;
            size_t length, disp;
            if (ind == 0) {
                length = (((b0 & 0xF) << 4) | (at(pos + 1) >> 4)) + 0x11;
                disp = (((at(pos + 1) & 0xF) << 8) | at(pos + 2)) + 1;
                pos += 3;
            } else if (ind == 1) {
                length = (((b0 & 0xF) << 12) | (at(pos + 1) << 4) | (at(pos + 2) >> 4)) + 0x111;
                disp = (((at(pos + 2) & 0xF) << 8) | at(pos + 3)) + 1;
                pos += 4;
            } else {
                length = ind + 1;
                disp = (((b0 & 0xF) << 8) | at(pos + 1)) + 1;
                pos += 2;
            }
            if (disp > out.size()) return false;
            size_t from = out.size() - disp;
            for (size_t i = 0; i < length && out.size() < size; i++) out.push_back(out[from + i]);
        }
    }
    return true;
}

bool is_ident(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// func_XXXXXXXX (upper-case hex) becomes rel<id>_XXXXXXXX, so modules sharing a slot don't collide.
std::string rename_functions(const std::string& text, uint32_t id) {
    const std::string prefix = "rel" + std::to_string(id) + "_";
    std::string out;
    out.reserve(text.size() + text.size() / 32);
    size_t i = 0;
    while (true) {
        size_t f = text.find("func_", i);
        if (f == std::string::npos) {
            out.append(text, i, std::string::npos);
            return out;
        }
        out.append(text, i, f - i);
        bool ok = (f == 0 || !is_ident(text[f - 1])) && f + 13 <= text.size() && (f + 13 == text.size() || !is_ident(text[f + 13]));
        for (size_t k = 0; ok && k < 8; k++) {
            char c = text[f + 5 + k];
            ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        }
        if (ok) {
            out += prefix;
            out.append(text, f + 5, 8);
            i = f + 13;
        } else {
            out += "func_";
            i = f + 5;
        }
    }
}

std::vector<std::string> function_names(const std::string& text) {
    std::vector<std::string> names;
    for (size_t f = text.find("void func_"); f != std::string::npos; f = text.find("void func_", f + 1))
        if (f + 18 <= text.size() && text[f + 18] == '(') names.push_back(text.substr(f + 10, 8));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

struct Module {
    uint32_t id = 0, base = 0, bss = 0, bss_size = 0, end = 0;
    std::string name;
};

std::string build_modules(const fs::path& dir, const fs::path& game, unsigned jobs,
                          const std::function<void(const std::string&)>& say) {
    std::error_code ec;
    fs::path work = dir / "build", rels = work / "rels", gen = work / "relgen", out = work / "rel" / "chunks";
    fs::create_directories(rels, ec);
    fs::create_directories(out, ec);
    say("Unpacking the game's modules...");
    std::map<uint32_t, Module> by_id;
    for (auto& e : fs::directory_iterator(game / "files" / "modules", ec)) {
        std::string file = e.path().filename().string();
        if (file.size() < 8 || file.compare(file.size() - 7, 7, ".rel.lz") != 0) continue;
        std::string data;
        if (!unlz11(read_text(e.path()), data) || data.size() < 0x4C) return "Couldn't unpack the module " + file + ".";
        Module m;
        m.name = file.substr(0, file.size() - 7);
        m.id = be32(data, 0);
        auto prior = by_id.find(m.id);
        if (prior != by_id.end()) {  // a few modules are on the disc twice, byte for byte, under two names
            if (read_text(rels / (prior->second.name + ".rel")) != data)
                return "Modules " + prior->second.name + " and " + m.name + " share a number but differ.";
            continue;
        }
        int slot = slot_of(m.name);
        if (slot < 0) return "The module " + m.name + " isn't one this build knows.";
        uint32_t fix = be32(data, 0x48), bss_align = std::max(be32(data, 0x44), 32u);
        m.bss_size = be32(data, 0x20);
        m.base = kSlots[slot].base;
        m.bss = (m.base + fix + bss_align - 1) / bss_align * bss_align;
        m.end = m.bss + m.bss_size;
        uint32_t limit = slot + 1 < kSlotCount ? kSlots[slot + 1].base : kSlotTop;
        if (m.end > limit) return "The module " + m.name + " doesn't fit in its slot.";
        std::ofstream f(rels / (m.name + ".rel"), std::ios::binary);
        f << data;
        if (!f) return "Couldn't write " + (rels / (m.name + ".rel")).u8string() + ".";
        by_id[m.id] = m;
    }
    if (by_id.empty()) return "The game has no modules in files/modules. Is this Mario Party 9?";
    std::vector<Module> modules;
    for (int s = 0; s < kSlotCount; s++)
        for (auto& [id, m] : by_id)
            if (m.base == kSlots[s].base) modules.push_back(m);

    // Translate, several at a time; each in its own output folder.
    std::atomic<size_t> next{0}, finished{0};
    std::atomic<int> failed_code{0};
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < std::max(1u, jobs); t++)
        workers.emplace_back([&]() {
            for (size_t i; (i = next++) < modules.size();) {
                const Module& m = modules[i];
                fs::path to = gen / (m.name + "_" + std::to_string(m.id));
                std::error_code e2;
                fs::create_directories(to, e2);
                char base[16];
                std::snprintf(base, sizeof base, "0x%08X", m.base);
                int code = run(dir, to, quote(dir / "builder" / "dolrecomp.exe") + L" --cpu broadway --rel-base " +
                                            std::wstring(base, base + std::strlen(base)) + L" " + quote(rels / (m.name + ".rel")) +
                                            L" SSQE01 " + quote(to), nullptr);
                if (code != 0) failed_code = code;
                finished++;
            }
        });
    while (finished < modules.size()) {
        say("Translating the game's modules: " + std::to_string(finished.load()) + " of " + std::to_string(modules.size()) + "...");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    for (auto& w : workers) w.join();
    if (failed_code) return "The translator stopped on a module (exit code " + std::to_string(failed_code.load()) + "). See build-log.txt.";

    say("Preparing the modules for the compiler...");
    std::string table = "/* Every translated REL module, its slot and its lookup (src/rel.h). Made by the launcher. */\n"
                        "#include \"generated.h\"\n#include \"rel.h\"\n\n";
    std::string list = "const RelModule rel_modules[] = {\n";
    for (const Module& m : modules) {
        fs::path g = gen / (m.name + "_" + std::to_string(m.id)) / "generated";
        std::string id = std::to_string(m.id);
        for (auto& e : fs::directory_iterator(g / "chunks", ec)) {
            std::string text = read_text(e.path());
            const std::string include = "#include \"../generated.h\"";
            size_t at = text.find(include);
            if (at != std::string::npos) text.replace(at, include.size(), "/* helpers: fastmem.h includes main.dol's generated.h */");
            std::ofstream f(out / ("rel" + id + "_" + e.path().filename().string()), std::ios::binary);
            f << rename_functions(text, m.id);
            if (!f) return "Couldn't write the modules' code: is the disk full?";
        }
        std::string header = read_text(g / "generated.h");
        const std::string head = "static inline DolRecompFunction dolrecomp_find_original(u32 address) {";
        size_t start = header.find(head);
        if (start == std::string::npos) return "The translation of " + m.name + " has no function table.";
        start = header.find('\n', start) + 1;
        size_t stop = start;
        while (stop < header.size()) {  // the body ends at the first line that is just "}"
            size_t eol = header.find('\n', stop);
            if (eol == std::string::npos) break;
            std::string line = header.substr(stop, eol - stop);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "}") break;
            stop = eol + 1;
        }
        for (const std::string& name : function_names(header)) table += "void rel" + id + "_" + name + "(CPUState* ctx);\n";
        table += "DolRecompFunction rel_find_" + id + "(u32 address) {\n" + rename_functions(header.substr(start, stop - start), m.id) + "}\n\n";
        char row[200];
        std::snprintf(row, sizeof row, "    {%u, \"%s\", 0x%08Xu, 0x%08Xu, 0x%08Xu, %uu, rel_find_%u},\n", m.id, m.name.c_str(), m.base,
                      m.end, m.bss, m.bss_size, m.id);
        list += row;
        fs::remove_all(gen / (m.name + "_" + std::to_string(m.id)), ec);  // the copies are made; this saves 3 GB
    }
    list += "};\nconst unsigned rel_module_count = sizeof rel_modules / sizeof *rel_modules;\n";
    std::ofstream t(work / "rel" / "rel_table.c", std::ios::binary);
    t << table << list;
    if (!t) return "Couldn't write the module table.";
    fs::remove_all(rels, ec);
    return "";
}

int count_files(const fs::path& folder, const std::string& extension) {
    int n = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(folder, ec))
        if (e.path().extension() == extension) n++;
    return n;
}

std::string mode_name(Mode m) { return m == Mode::Measuring ? "measuring" : m == Mode::Fast ? "fast" : "plain"; }

}  // namespace

bool available(const fs::path& dir) { return fs::exists(dir / "builder" / "game.mk"); }

std::string version(const fs::path& dir) { return trim(read_text(dir / "builder" / "version.txt")); }

fs::path game_exe(const fs::path& dir) { return dir / "Party9Recomp-Game.exe"; }

bool built(const fs::path& dir, const std::string& dol_hash, Mode* mode) {
    if (!fs::exists(game_exe(dir))) return false;
    std::istringstream s(read_text(dir / "game-build.txt"));
    std::string v, h, m;
    std::getline(s, v);
    std::getline(s, h);
    std::getline(s, m);
    if (trim(v) != version(dir) || trim(h) != dol_hash) return false;
    if (mode) *mode = trim(m) == "measuring" ? Mode::Measuring : trim(m) == "fast" ? Mode::Fast : Mode::Plain;
    return true;
}

int measured_files(const fs::path& dir) { return count_files(dir / "build" / "obj", ".gcda"); }

std::string build(const fs::path& dir, const fs::path& main_dol, const std::string& dol_hash, Mode mode,
                  const std::function<void(const std::string&)>& say) {
    std::error_code ec;
    fs::path work = dir / "build", obj = work / "obj";
    unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    note(dir, "building (" + mode_name(mode) + ") with toolkit " + version(dir));

    if (mode != Mode::Fast) {
        // A fresh translation each time: it takes seconds, and old objects or counts can't leak in.
        fs::remove_all(work, ec);
        if (fs::exists(work)) return "Couldn't clear the old build folder: is the game still running?";
        fs::create_directories(work / "recomp", ec);
        say("Translating your game's code (this part takes seconds)...");
        // It runs in its own output folder, as it may write beside where it runs.
        int code = run(dir, work / "recomp", quote(dir / "builder" / "dolrecomp.exe") + L" --cpu broadway -j" + std::to_wstring(jobs) + L" " +
                                quote(main_dol) + L" SSQE01 " + quote(work / "recomp"), nullptr);
        if (code != 0) return "The translator stopped (exit code " + std::to_string(code) + "). See build-log.txt.";
        say("Adding the runtime's hooks...");
        std::string problem = add_hooks(dir);
        if (!problem.empty()) return problem;
        fs::remove_all(work / "recomp" / "generated" / "chunks", ec);  // build/chunks has the copies now
        problem = build_modules(dir, main_dol.parent_path().parent_path(), jobs, say);
        if (!problem.empty()) return problem;
    } else {
        if (measured_files(dir) == 0) return "There are no measurements yet: play the game for a while first, then close it.";
        // The counts stay; the objects go, so everything is compiled again with them.
        for (auto& e : fs::directory_iterator(obj, ec))
            if (e.path().extension() == ".o") fs::remove(e.path(), ec);
    }

    int total = count_files(work / "chunks", ".c") + count_files(work / "rel" / "chunks", ".c") +
                count_files(dir / "builder" / "src", ".c") + 2;
    std::atomic<bool> done{false};
    auto progress = [&]() {
        int made = count_files(obj, ".o");
        const char* what = mode == Mode::Fast ? "Building the faster version" : mode == Mode::Measuring ? "Building the measuring version"
                                                                                                         : "Building the game";
        say(std::string(what) + ": " + std::to_string(std::min(made, total)) + " of " + std::to_string(total) +
            " parts compiled. This takes a while; the launcher can stay open.");
    };
    progress();
    std::wstring pgo = mode == Mode::Measuring ? L" PGO=gen" : mode == Mode::Fast ? L" PGO=use" : L"";
    fs::remove(game_exe(dir), ec);
    if (fs::exists(game_exe(dir))) return "Couldn't replace the game: close it first.";
    std::string v = version(dir);  // one copy: a range needs both ends from the same string
    int code = run(dir, dir, quote(dir / "builder" / "toolchain" / "bin" / "mingw32-make.exe") + L" -f builder/game.mk -j" +
                            std::to_wstring(jobs) + L" VERSION=" + std::wstring(v.begin(), v.end()) + pgo,
                   progress);
    if (code != 0 || !fs::exists(game_exe(dir)))
        return "The compiler stopped (exit code " + std::to_string(code) + "). The end of build-log.txt says why.";

    std::ofstream stamp(dir / "game-build.txt", std::ios::trunc);
    stamp << version(dir) << "\n" << dol_hash << "\n" << mode_name(mode) << "\n";
    if (mode == Mode::Plain) fs::remove_all(work, ec);  // nothing more is needed from it
    note(dir, "built (" + mode_name(mode) + ")");
    return "";
}

}  // namespace builder
