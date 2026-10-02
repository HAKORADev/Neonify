// NEONIFY native — shared laws: banner, colors, progress, naming, subprocess, wav
#pragma once

#include <opencv2/core.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#undef min
#undef max
#include <io.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace neon {

inline const char* NEON_BLUE = "\033[38;5;39m";
inline const char* NEON_PINK = "\033[38;5;213m";
inline const char* NEON_DIM  = "\033[38;5;245m";
inline const char* NEON_BOLD = "\033[1m";
inline const char* NEON_RESET = "\033[0m";

inline constexpr int AUDIO_SR = 22050;
inline const char* APP_NAME = "NEONIFY";
inline const char* APP_TAG = "dark&white neon media neonifier";
inline const char* APP_VER = "v0.5.0";

// ---------------------------------------------------------------- banner
// built-in 5x5 font — the banner the owner liked (ported verbatim)
inline const char* const BANNER_GLYPHS[][5] = {
    {"10001", "11001", "10101", "10011", "10001"},  // N
    {"11111", "10000", "11110", "10000", "11111"},  // E
    {"01110", "10001", "10001", "10001", "01110"},  // O
    {"11111", "00100", "00100", "00100", "11111"},  // I
    {"11111", "10000", "11110", "10000", "10000"},  // F
    {"10001", "10001", "01010", "00100", "00100"},  // Y
};

inline std::string banner_rows[5];

inline void build_banner() {
    const std::string word = "NEONIFY";
    for (int r = 0; r < 5; r++) banner_rows[r].clear();
    for (char ch : word) {
        int g = -1;
        switch (ch) {
            case 'N': g = 0; break; case 'E': g = 1; break; case 'O': g = 2; break;
            case 'I': g = 3; break; case 'F': g = 4; break; case 'Y': g = 5; break;
            default: g = -1; break;
        }
        for (int r = 0; r < 5; r++) {
            std::string row = (g >= 0) ? BANNER_GLYPHS[g][r] : "00000";
            for (char c : row) banner_rows[r] += (c == '1') ? "\xe2\x96\x88" : " ";
            banner_rows[r] += " ";
        }
    }
}

inline bool stdout_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(fileno(stdout)) != 0;
#endif
}

inline void print_banner() {
    build_banner();
    if (!stdout_is_tty()) {
        std::printf("%s\n", APP_NAME);
        return;
    }
    std::printf("%s%s%s\n", NEON_BLUE, NEON_BOLD, banner_rows[0].c_str());
    std::printf("%s%s%s%s  %s%s\n", NEON_BLUE, banner_rows[1].c_str(), NEON_RESET, NEON_DIM, APP_TAG, NEON_RESET);
    std::printf("%s%s%s\n", NEON_PINK, banner_rows[2].c_str(), NEON_RESET);
    std::printf("%s%s%s%s  %s  \xc2\xb7  cpu-native \xc2\xb7 no gpu needed%s\n",
                NEON_PINK, banner_rows[3].c_str(), NEON_RESET, NEON_DIM, APP_VER, NEON_RESET);
    std::printf("%s%s%s\n\n", NEON_BLUE, banner_rows[4].c_str(), NEON_RESET);
}

// ---------------------------------------------------------------- naming
inline std::string timestamp_suffix() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "_%y%m%d%H%M%S", &tm);
    return buf;
}

inline std::string unique_output_path(const std::string& path) {
    std::ifstream f(path.c_str());
    if (!f.good()) return path;
    f.close();
    std::string::size_type dot = path.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return path + timestamp_suffix();
    return path.substr(0, dot) + timestamp_suffix() + path.substr(dot);
}

inline std::string out_default(const std::string& inp, const std::string& tag, const std::string& ext) {
    std::string::size_type slash = inp.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? inp : inp.substr(slash + 1);
    std::string::size_type dot = base.find_last_of('.');
    std::string root = (dot == std::string::npos) ? base : base.substr(0, dot);
    return root + "_" + tag + timestamp_suffix() + ext;
}

inline std::string unique_or_default(const std::string& explicit_path, const std::string& fallback) {
    return explicit_path.empty() ? fallback : unique_output_path(explicit_path);
}

inline std::string cwd() {
    char buf[4096];
#ifdef _WIN32
    if (!GetCurrentDirectoryA(sizeof(buf), buf)) return ".";
#else
    if (!getcwd(buf, sizeof(buf))) return ".";
#endif
    return buf;
}

inline std::string results_dir() {
    std::string d = cwd() + "/results";
#ifdef _WIN32
    CreateDirectoryA(d.c_str(), nullptr);
#else
    mkdir(d.c_str(), 0755);
#endif
    return d;
}

inline std::string results_path(const std::string& name) {
    return results_dir() + "/" + name;
}

inline void remove_tree(const std::string& p) {
#ifdef _WIN32
    SHFILEOPSTRUCTA op{};
    std::vector<char> from(p.begin(), p.end());
    from.push_back('\0');
    from.push_back('\0');
    op.hwnd = nullptr;
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationA(&op);
#else
    pid_t p2 = fork();
    if (p2 == 0) {
        execlp("rm", "rm", "-rf", p.c_str(), (char*)nullptr);
        _exit(127);
    }
    int st = 0;
    waitpid(p2, &st, 0);
#endif
}

inline std::string lower_ext(const std::string& p) {
    std::string::size_type dot = p.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = p.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

// ---------------------------------------------------------------- progress
// THE LAW: n/n counts COMPLETED steps; % moves inside the current step.
class StageTracker {
public:
    bool enabled = true;
    bool json_mode = false;
    bool quiet = false;
    std::function<void(const std::string&, double)> hook;
    std::vector<std::string> stages;
    int current = -1;
    double pct = 0.0;

    StageTracker(bool en = true, bool json = false) : enabled(en), json_mode(json) {
        t0 = std::chrono::steady_clock::now();
    }

    void set_stages(const std::vector<std::string>& names) {
        stages = names;
        current = -1;
        pct = 0.0;
    }

    void begin_stage(int idx, const std::string& name = "") {
        if (idx >= (int)stages.size()) return;
        if (current != idx) {
            current = idx;
            pct = 0.0;
            draw(name.empty() ? stages[idx] : name);
        }
    }

    void step(double frac) {
        if (!enabled || current < 0) return;
        frac = std::max(0.0, std::min(1.0, frac));
        int new_pct = int(frac * 100);
        if (new_pct != int(pct)) {
            pct = frac * 100.0;
            draw();
        }
    }

    void complete_stage(int idx = -1) {
        int i = (idx < 0) ? current : idx;
        if (i >= 0 && i < (int)stages.size()) {
            current = i;
            pct = 100.0;
            draw();
        }
        if (enabled && !json_mode) {
            std::fputs("\n", stdout);
            std::fflush(stdout);
        }
        if (!stages.empty() && current < (int)stages.size() - 1) current++;
        if (current >= 0 && idx >= 0 && idx + 1 < (int)stages.size()) pct = 0.0;
        last_line.clear();
    }

    void finish() {
        if (enabled && !json_mode) {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            char b[32];
            if (s < 60) std::snprintf(b, sizeof(b), "%.0fs", s);
            else if (s < 3600) std::snprintf(b, sizeof(b), "%.1fm", s / 60.0);
            else std::snprintf(b, sizeof(b), "%.1fh", s / 3600.0);
            std::printf("%s  done in %s%s\n", NEON_DIM, b, NEON_RESET);
            std::fflush(stdout);
        }
    }

private:
    std::string last_line;
    std::chrono::steady_clock::time_point t0;

    void draw(const std::string& name = "") {
        if (!enabled) return;
        if (hook) {
            int total = (int)stages.size();
            std::string label = name.empty() ? (current >= 0 && current < total ? stages[current] : "") : name;
            hook(label, current < 0 ? 0.0 : (double(current) + pct / 100.0) / std::max(1, total));
        }
        if (quiet) return;
        int n = current + 1;
        int total = (int)stages.size();
        std::string label = name.empty() ? (current >= 0 && current < total ? stages[current] : "") : name;
        if (json_mode) {
            int overall = int((double(n - 1) + pct / 100.0) / std::max(1, total) * 100.0);
            std::printf("{\"percent\": %d, \"step\": \"%s\"}\n", std::min(overall, 100), label.c_str());
            std::fflush(stdout);
            return;
        }
        char line[512];
        std::snprintf(line, sizeof(line), "\r%s[%d/%d]%s %3d%%  %s%s%s",
                      NEON_BLUE, n, std::max(1, total), NEON_RESET, int(pct), NEON_DIM,
                      label.c_str(), NEON_RESET);
        std::string s(line);
        if (s.size() < last_line.size()) s += std::string(last_line.size() - s.size(), ' ');
        std::fputs(s.c_str(), stdout);
        std::fflush(stdout);
        last_line = s;
    }
};

// ---------------------------------------------------------------- process
class Proc {
public:
    FILE* out = nullptr;
    FILE* in = nullptr;
    int exit_code = -1;

    bool spawn(const std::vector<std::string>& argv, bool capture_out, bool feed_in) {
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE orr = nullptr, owr = nullptr, irr = nullptr, iwr = nullptr;
        if (capture_out && !CreatePipe(&orr, &owr, &sa, 0)) return false;
        if (feed_in && !CreatePipe(&irr, &iwr, &sa, 0)) return false;
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = feed_in ? irr : HANDLE(_get_osfhandle(_fileno(stdin)));
        si.hStdOutput = capture_out ? owr : HANDLE(_get_osfhandle(_fileno(stdout)));
        si.hStdError = HANDLE(_get_osfhandle(_fileno(stderr)));
        std::string cmd;
        for (size_t i = 0; i < argv.size(); i++) {
            if (i) cmd += " ";
            cmd += quote(argv[i]);
        }
        std::vector<char> cmdv(cmd.begin(), cmd.end());
        cmdv.push_back('\0');
        if (!CreateProcessA(nullptr, cmdv.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return false;
        }
        if (owr) CloseHandle(owr);
        if (irr) CloseHandle(irr);
        if (capture_out) out = _fdopen(_open_osfhandle((intptr_t)orr, 0), "rb");
        if (feed_in) in = _fdopen(_open_osfhandle((intptr_t)iwr, 0), "wb");
        return true;
#else
        int op[2] = {-1, -1}, ip[2] = {-1, -1};
        if (capture_out && pipe(op) != 0) return false;
        if (feed_in && pipe(ip) != 0) return false;
        pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            if (capture_out) { dup2(op[1], 1); close(op[0]); close(op[1]); }
            if (feed_in) { dup2(ip[0], 0); close(ip[0]); close(ip[1]); }
            std::vector<char*> av;
            for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
            av.push_back(nullptr);
            execvp(av[0], av.data());
            _exit(127);
        }
        if (op[1] >= 0) close(op[1]);
        if (ip[0] >= 0) close(ip[0]);
        if (capture_out) out = fdopen(op[0], "rb");
        if (feed_in) in = fdopen(ip[1], "wb");
        return true;
#endif
    }

    int wait_close() {
        if (in) { std::fclose(in); in = nullptr; }
#ifdef _WIN32
        if (pi.hProcess) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            exit_code = (int)code;
            if (out) { std::fclose(out); out = nullptr; }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            pi = PROCESS_INFORMATION{};
        }
#else
        if (pid > 0) {
            int st = 0;
            waitpid(pid, &st, 0);
            exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            pid = -1;
        }
        if (out) { std::fclose(out); out = nullptr; }
#endif
        return exit_code;
    }

private:
#ifdef _WIN32
    PROCESS_INFORMATION pi{};
    static std::string quote(const std::string& s) {
        if (s.find(' ') == std::string::npos) return s;
        return "\"" + s + "\"";
    }
#else
    pid_t pid = -1;
#endif
};

inline bool run_ok(const std::vector<std::string>& argv, std::string* capture = nullptr) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE orr = nullptr, owr = nullptr;
    bool need = capture != nullptr;
    if (need && !CreatePipe(&orr, &owr, &sa, 0)) return false;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = HANDLE(_get_osfhandle(_fileno(stdin)));
    si.hStdOutput = need ? owr : HANDLE(_get_osfhandle(_fileno(stdout)));
    si.hStdError = HANDLE(_get_osfhandle(_fileno(stderr)));
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) cmd += " ";
        std::string q = argv[i];
        if (q.find(' ') != std::string::npos) q = "\"" + q + "\"";
        cmd += q;
    }
    std::vector<char> cmdv(cmd.begin(), cmd.end());
    cmdv.push_back('\0');
    PROCESS_INFORMATION pi{};
    BOOL okf = CreateProcessA(nullptr, cmdv.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (owr) CloseHandle(owr);
    if (!okf) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (need && orr) {
        char buf[4096];
        DWORD nread = 0;
        while (ReadFile(orr, buf, sizeof(buf), &nread, nullptr) && nread)
            capture->append(buf, nread);
        CloseHandle(orr);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
#else
    int op[2] = {-1, -1};
    if (capture && pipe(op) != 0) return false;
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
        if (capture) { dup2(op[1], 1); close(op[0]); close(op[1]); }
        std::vector<char*> av;
        for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        _exit(127);
    }
    if (op[1] >= 0) close(op[1]);
    if (capture) {
        char buf[4096];
        ssize_t n;
        while ((n = read(op[0], buf, sizeof(buf))) > 0) capture->append(buf, n);
        close(op[0]);
    }
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
#endif
}

// ---------------------------------------------------------------- wav
inline void write_wav_stereo(const std::string& path, const std::vector<float>& l,
                             const std::vector<float>& r, int sr = AUDIO_SR) {
    float pl = 0.f, pr = 0.f;
    for (float v : l) pl = std::max(pl, std::fabs(v));
    for (float v : r) pr = std::max(pr, std::fabs(v));
    float p = std::max(pl, pr);
    float k = p > 1e-9f ? 0.89f / p : 1.0f;
    size_t n = std::min(l.size(), r.size());
    uint32_t data_bytes = uint32_t(n * 4);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((char*)&v, 4); };
    auto u16 = [&](uint16_t v) { f.write((char*)&v, 2); };
    f.write("RIFF", 4);
    u32(36 + data_bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(sr));
    u32(uint32_t(sr * 4));
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(data_bytes);
    for (size_t i = 0; i < n; i++) {
        int16_t a = int16_t(std::max(-32768.f, std::min(32767.f, l[i] * k * 32767.f)));
        int16_t b = int16_t(std::max(-32768.f, std::min(32767.f, r[i] * k * 32767.f)));
        f.write((char*)&a, 2);
        f.write((char*)&b, 2);
    }
}

// ---------------------------------------------------------------- misc
inline bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// windowed exe: re-attach to the cmd that launched `neonify.exe cli`
// double-click (no parent console) skips silently, GUI stays clean
inline void attach_parent_console(int argc, char** argv) {
#ifdef _WIN32
    if (argc < 2) return;
    std::string a0 = argv[1];
    if (a0 == "gui" || a0 == "--help" || a0 == "-h") return;
    if (GetConsoleWindow() != nullptr) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#endif
}

inline bool which_ok(const std::string& tool) {
#ifdef _WIN32
    std::string cap;
    return run_ok({"where", tool}, &cap);
#else
    std::string cmd = "command -v " + tool + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
#endif
}

}  // namespace neon
