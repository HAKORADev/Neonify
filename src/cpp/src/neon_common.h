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
#include <iterator>
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
inline const char* NEON_RED = "\033[38;5;203m";
inline const char* NEON_WHITE = "\033[38;5;255m";
inline const char* NEON_PINK = "\033[38;5;213m";
inline const char* NEON_DIM  = "\033[38;5;245m";
inline const char* NEON_BOLD = "\033[1m";
inline const char* NEON_RESET = "\033[0m";

inline constexpr int AUDIO_SR = 22050;
inline const char* APP_NAME = "NEONIFY";
inline const char* APP_VER = "v0.5.0";

// ---------------------------------------------------------------- banner
// 1:1 with the old python: pyfiglet "big" font, left half blue / right half
// red, white subtitle, then the 60-char rule. rows captured verbatim.
inline const char* const BANNER_ROWS[6] = {
    " _   _ ______ ____  _   _ _____ ________     __",
    "| \\ | |  ____/ __ \\| \\ | |_   _|  ____\\ \\   / /",
    "|  \\| | |__ | |  | |  \\| | | | | |__   \\ \\_/ / ",
    "| . ` |  __|| |  | | . ` | | | |  __|   \\   /  ",
    "| |\\  | |___| |__| | |\\  |_| |_| |       | |   ",
    "|_| \\_|______\\____/|_| \\_|_____|_|       |_|   ",
};

// env-gated stderr breadcrumbs: NEONIFY_TRACE=1 ./neonify ...
inline bool trace_enabled() {
    static bool t = (std::getenv("NEONIFY_TRACE") != nullptr);
    return t;
}

inline void trace(const char* tag) {
    if (trace_enabled()) {
        std::fprintf(stderr, "[trace] %s\n", tag);
        std::fflush(stderr);
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
    if (!stdout_is_tty()) {
        std::printf("%s\n", APP_NAME);
        return;
    }
    const std::string blank(47, ' ');
    const size_t mid = 23;
    std::printf("\n");
    for (int r = 0; r < 6; r++) {
        std::string line = BANNER_ROWS[r];
        line += blank.substr(line.size());
        std::printf("%s%s%s%s%s%s\n", NEON_BLUE, line.substr(0, mid).c_str(), NEON_RESET,
                    NEON_RED, line.substr(mid).c_str(), NEON_RESET);
    }
    for (int r = 0; r < 2; r++)
        std::printf("%s%s%s%s%s%s\n", NEON_BLUE, blank.substr(0, mid).c_str(), NEON_RESET,
                    NEON_RED, blank.substr(mid).c_str(), NEON_RESET);
    std::printf("%s              procedural neon engine %s%s\n", NEON_WHITE, APP_VER, NEON_RESET);
    std::printf("============================================================\n");
}

// ------------------------------------------------------- unicode-safe file io
// windows ansi fopen/imread/imwrite silently fail on non-ascii paths;
// every engine file access goes through here (utf-8 in, wide on win32)
inline std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::vector<uint8_t> data;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return data;
    std::wstring w((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], wlen);
    FILE* f = _wfopen(w.c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        data.resize((size_t)sz);
        size_t got = std::fread(data.data(), 1, (size_t)sz, f);
        data.resize(got);
    }
    std::fclose(f);
    return data;
}

inline bool write_file_bytes(const std::string& path, const uint8_t* data, size_t n) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring w((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], wlen);
    FILE* f = _wfopen(w.c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    size_t put = n ? std::fwrite(data, 1, n, f) : 1;
    std::fclose(f);
    return put == n;
}

inline bool make_dir(const std::string& path) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring w((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], wlen);
    return CreateDirectoryW(w.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

inline bool move_file(const std::string& from, const std::string& to) {
#ifdef _WIN32
    auto to_w = [](const std::string& s) {
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w((size_t)std::max(len, 1), L'\0');
        if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
        return w;
    };
    return MoveFileExW(to_w(from).c_str(), to_w(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

inline bool remove_file(const std::string& path) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring w((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], wlen);
    return DeleteFileW(w.c_str()) != 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

#ifdef _WIN32
inline std::wstring wide_from_utf8(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)std::max(len, 1), L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}
inline std::string utf8_from_wide(const std::wstring& w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)std::max(len, 1), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
#endif

#ifdef _WIN32
inline std::ifstream open_ifstream(const std::string& path, std::ios::openmode mode = std::ios::in) {
    return std::ifstream(wide_from_utf8(path), mode);
}
inline std::ofstream open_ofstream(const std::string& path, std::ios::openmode mode = std::ios::out) {
    return std::ofstream(wide_from_utf8(path), mode);
}
#else
inline std::ifstream open_ifstream(const std::string& path, std::ios::openmode mode = std::ios::in) {
    return std::ifstream(path, mode);
}
inline std::ofstream open_ofstream(const std::string& path, std::ios::openmode mode = std::ios::out) {
    return std::ofstream(path, mode);
}
#endif

inline bool file_exists(const std::string& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(wide_from_utf8(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
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
    if (!file_exists(path)) return path;
    std::string::size_type dot = path.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return path + timestamp_suffix();
    return path.substr(0, dot) + timestamp_suffix() + path.substr(dot);
}

inline std::string out_default(const std::string& inp, const std::string& tag, const std::string& ext) {
    std::string::size_type slash = inp.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? inp : inp.substr(slash + 1);
    std::string::size_type dot = base.find_last_of('.');
    std::string root = (dot == std::string::npos) ? base : base.substr(0, dot);
    return root + "_neonify_" + tag + timestamp_suffix() + ext;
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
    make_dir(d);
    return d;
}

inline std::string results_path(const std::string& name) {
    return results_dir() + "/" + name;
}

inline void remove_tree(const std::string& p) {
#ifdef _WIN32
    SHFILEOPSTRUCTW op{};
    std::wstring w = wide_from_utf8(p);
    std::vector<wchar_t> from(w.begin(), w.end());
    from.push_back(L'\0');
    from.push_back(L'\0');
    op.hwnd = nullptr;
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
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
        STARTUPINFOW si{};
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
        std::wstring wcmd = wide_from_utf8(cmd);
        std::vector<wchar_t> cmdv(wcmd.begin(), wcmd.end());
        cmdv.push_back(L'\0');
        if (!CreateProcessW(nullptr, cmdv.data(), nullptr, nullptr, TRUE,
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
        if (s.find(' ') == std::string::npos && !s.empty()) return s;
        return '"' + s + '"';
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
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = HANDLE(_get_osfhandle(_fileno(stdin)));
    si.hStdOutput = need ? owr : HANDLE(_get_osfhandle(_fileno(stdout)));
    si.hStdError = HANDLE(_get_osfhandle(_fileno(stderr)));
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) cmd += " ";
        std::string q = argv[i];
        if (q.find(' ') != std::string::npos || q.empty()) q = '"' + q + '"';
        cmd += q;
    }
    std::wstring wcmd = wide_from_utf8(cmd);
    std::vector<wchar_t> cmdv(wcmd.begin(), wcmd.end());
    cmdv.push_back(L'\0');
    PROCESS_INFORMATION pi{};
    BOOL okf = CreateProcessW(nullptr, cmdv.data(), nullptr, nullptr, TRUE,
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
    std::vector<uint8_t> wav;
    wav.reserve(size_t(data_bytes) + 44);
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) wav.push_back(uint8_t(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) wav.push_back(uint8_t(v >> (8 * i))); };
    auto str = [&](const char* s, size_t len) { for (size_t i = 0; i < len; i++) wav.push_back(uint8_t(s[i])); };
    str("RIFF", 4);
    u32(36 + data_bytes);
    str("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(sr));
    u32(uint32_t(sr * 4));
    u16(4);
    u16(16);
    str("data", 4);
    u32(data_bytes);
    for (size_t i = 0; i < n; i++) {
        int16_t a = int16_t(std::max(-32768.f, std::min(32767.f, l[i] * k * 32767.f)));
        int16_t b = int16_t(std::max(-32768.f, std::min(32767.f, r[i] * k * 32767.f)));
        u16(uint16_t(a));
        u16(uint16_t(b));
    }
    if (!write_file_bytes(path, wav.data(), wav.size()))
        throw std::runtime_error("cannot write audio: " + path);
}

// ---------------------------------------------------------------- misc

// windowed exe: re-attach to the cmd that launched `neonify.exe cli`
// double-click (no parent console) skips silently, GUI stays clean
inline void console_utf8() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

inline void attach_parent_console(int argc, char** argv) {
#ifdef _WIN32
    if (argc < 2) return;
    std::string a0 = argv[1];
    if (a0 == "gui" || a0 == "--help" || a0 == "-h") return;
    if (GetConsoleWindow() != nullptr) { console_utf8(); return; }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    console_utf8();
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

// ansi main() argv mangles non-ascii paths on windows; read the wide command line
inline std::vector<std::string> utf8_args(int argc, char** argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    std::vector<std::string> out;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return out;
    for (int i = 1; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) continue;
        std::string s((size_t)len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
        while (!s.empty() && s.back() == '\0') s.pop_back();
        out.push_back(s);
    }
    LocalFree(w);
    return out;
#else
    return std::vector<std::string>(argv + 1, argv + argc);
#endif
}

}  // namespace neon
