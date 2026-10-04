// NEONIFY native — shared laws: banner, colors, progress, naming, subprocess, wav
#pragma once

#include <opencv2/core.hpp>
#include <algorithm>
#include <atomic>
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
#include <thread>
#include <mutex>
#undef min
#undef max
#include <io.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <signal.h>
#include <thread>
#include <mutex>
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
// 1:1 with the old python: the built-in 5x5 block font (no pyfiglet), left
// half blue / right half red, white subtitle, then the 60-char rule.
inline const char* const BANNER_ROWS[5] = {
    "█...█  █████  .███.  █...█  █████  █████  █...█",
    "██..█  █....  █...█  ██..█  ..█..  █....  █...█",
    "█.█.█  ████.  █...█  █.█.█  ..█..  ████.  .█.█.",
    "█..██  █....  █...█  █..██  ..█..  █....  ..█..",
    "█...█  █████  .███.  █...█  █████  █....  ..█..",
};

// env-gated stderr breadcrumbs: NEONIFY_TRACE=1 ./neonify ...
inline bool trace_enabled() {
    static bool t = (std::getenv("NEONIFY_TRACE") != nullptr);
    return t;
}

inline void trace(const char* tag) {
    if (!trace_enabled()) return;
    std::fprintf(stderr, "[trace] %s\n", tag);
    std::fflush(stderr);
    // stderr can vanish under gui hosts and redirected runners — the file
    // copy always lands next to the exe
    static FILE* tf = std::fopen("neonify_trace.log", "a");
    if (tf) {
        std::fprintf(tf, "[trace] %s\n", tag);
        std::fflush(tf);
    }
}

inline bool stdout_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(fileno(stdout)) != 0;
#endif
}

// the banner rows speak utf-8 blocks — split at a column boundary, never at
// a byte offset, or the color halves cut inside a block character
inline void banner_split(const std::string& s, size_t col, std::string& left, std::string& right) {
    size_t c = 0, i = 0;
    while (i < s.size() && c < col) {
        unsigned char b = (unsigned char)s[i];
        size_t w = (b < 0x80) ? 1 : ((b >> 5) == 0x6) ? 2 : ((b >> 4) == 0xE) ? 3 : 4;
        i += w;
        c++;
    }
    left = s.substr(0, i);
    right = s.substr(i);
}

inline void print_banner() {
    if (!stdout_is_tty()) {
        std::printf("%s\n", APP_NAME);
        return;
    }
    const size_t mid = 23;
    std::printf("\n");
    for (int r = 0; r < 5; r++) {
        std::string left, right;
        banner_split(BANNER_ROWS[r], mid, left, right);
        std::printf("%s%s%s%s%s%s\n", NEON_BLUE, left.c_str(), NEON_RESET,
                    NEON_RED, right.c_str(), NEON_RESET);
    }
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
    std::string base = (dot == std::string::npos || dot == 0) ? path : path.substr(0, dot);
    std::string ext = (dot == std::string::npos || dot == 0) ? "" : path.substr(dot);
    // the law is one timestamp per name: when the stem already carries one
    // (_yymmddhhmmss, 13 chars), it is replaced by the fresh draw instead of
    // stacking a second one
    if (base.size() > 13 && base[base.size() - 13] == '_') {
        bool digits = true;
        for (size_t i = base.size() - 12; i < base.size(); i++) {
            char c = base[i];
            if (c < '0' || c > '9') { digits = false; break; }
        }
        if (digits) base = base.substr(0, base.size() - 13);
    }
    // same-second runs collide on the timestamp alone, so keep drawing until free
    for (int i = 0; i < 1000; i++) {
        std::string cand = base + timestamp_suffix();
        if (i > 0) cand += "_" + std::to_string(i);
        cand += ext;
        if (!file_exists(cand)) return cand;
    }
    return path;
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
// cooperative cancel: engines poll this and unwind with Cancelled so the gui
// stop button kills a run between frames/steps instead of at file borders
class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("cancelled") {}
};

class StageTracker {
public:
    bool enabled = true;
    bool json_mode = false;
    bool quiet = false;
    std::atomic<bool>* cancel_ext = nullptr;
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

    void step(double frac) { step(frac, std::string()); }

    // label_override rides along so counters can show real work units
    // (frame 240/1200) instead of the bare stage name
    void step(double frac, const std::string& label_override) {
        if (!enabled || current < 0) return;
        frac = std::max(0.0, std::min(1.0, frac));
        int new_pct = int(frac * 100);
        if (new_pct != int(pct) || (!label_override.empty() && label_override != step_label)) {
            pct = frac * 100.0;
            step_label = label_override;
            draw(label_override);
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

    bool cancelled() const { return cancel_ext && cancel_ext->load(); }

    void check_cancel() const {
        if (cancelled()) throw Cancelled();
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
    std::string step_label;
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

inline void throw_if_cancelled(const StageTracker* tr) {
    if (tr && tr->cancelled()) throw Cancelled();
}

// ---------------------------------------------------------------- exe dir
// next to the binary: the ini, the trace log and the child registry live here
inline std::string exe_dir() {
    std::string dir;
#ifdef _WIN32
    wchar_t buf[MAX_PATH + 1] = {0};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH)) {
        std::string full = utf8_from_wide(buf);
        std::string::size_type s = full.find_last_of("\\/");
        if (s != std::string::npos) dir = full.substr(0, s);
    }
#else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        std::string full(buf);
        std::string::size_type s = full.find_last_of('/');
        if (s != std::string::npos) dir = full.substr(0, s);
    }
#endif
    if (dir.empty()) dir = ".";
    return dir;
}

// ---------------------------------------------------------------- children
// every ffmpeg/ffprobe child is written here next to the binary together
// with the owner pid; the next launch kills children whose owner died, so a
// crash or a freeze never leaves encoders stalled behind the app
inline std::string children_path() {
    return exe_dir() + "/neonify_children.txt";
}

inline long self_pid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

inline bool pid_alive(long pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD w = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return w == WAIT_TIMEOUT;
#else
    return ::kill((pid_t)pid, 0) == 0 || errno != ESRCH;
#endif
}

inline bool pid_image_is_ffmpeg(long pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    wchar_t name[MAX_PATH + 1] = {0};
    DWORD sz = MAX_PATH;
    BOOL okf = QueryFullProcessImageNameW(h, 0, name, &sz);
    CloseHandle(h);
    if (!okf) return false;
    std::string full = utf8_from_wide(name);
    std::string::size_type s = full.find_last_of("\\/");
    std::string base = (s == std::string::npos) ? full : full.substr(s + 1);
    for (char& c : base) c = char(std::tolower((unsigned char)c));
    return base.rfind("ffmpeg", 0) == 0 || base.rfind("ffprobe", 0) == 0;
#else
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    if (!f.good()) return false;
    std::string comm;
    std::getline(f, comm);
    return comm.rfind("ffmpeg", 0) == 0 || comm.rfind("ffprobe", 0) == 0;
#endif
}

inline void pid_kill(long pid) {
    if (pid <= 0) return;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (h) {
        TerminateProcess(h, (UINT)-1);
        CloseHandle(h);
    }
#else
    ::kill((pid_t)pid, SIGKILL);
#endif
}

// one lock for the whole registry: every op is a read-modify-write over the
// same file, and decoder threads plus the main thread hit it concurrently
// (compare mode runs two ffmpeg children at once)
inline std::mutex& children_mtx() {
    static std::mutex m;
    return m;
}

inline void child_register(long owner, long child, const std::string& name) {
    std::lock_guard<std::mutex> lk(children_mtx());
    std::ofstream f = open_ofstream(children_path(), std::ios::app);
    if (!f.good()) return;
    f << owner << " " << child << " " << name << "\n";
}

inline std::vector<std::string> child_lines() {
    std::lock_guard<std::mutex> lk(children_mtx());
    std::vector<std::string> lines;
    std::ifstream f = open_ifstream(children_path());
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

inline void child_write_lines(const std::vector<std::string>& lines) {
    std::lock_guard<std::mutex> lk(children_mtx());
    std::ofstream f = open_ofstream(children_path(), std::ios::binary | std::ios::trunc);
    if (!f.good()) return;
    for (const std::string& l : lines) f << l << "\n";
}

// the sweep: a child whose owner is gone is a stall left behind — kill it
// when it really is an ffmpeg/ffprobe, never touch live owners' children
inline void sweep_orphan_children() {
    std::vector<std::string> lines = child_lines();
    if (lines.empty()) return;
    std::vector<std::string> keep;
    for (const std::string& line : lines) {
        std::istringstream is(line);
        long owner = 0, child = 0;
        std::string name;
        if (!(is >> owner >> child >> name)) continue;
        if (pid_alive(owner)) {
            keep.push_back(line);
            continue;
        }
        if (pid_alive(child) && pid_image_is_ffmpeg(child)) pid_kill(child);
    }
    child_write_lines(keep);
}

inline void child_unregister(long child) {
    std::vector<std::string> lines = child_lines();
    std::vector<std::string> keep;
    for (const std::string& line : lines) {
        std::istringstream is(line);
        long owner = 0, c = 0;
        if ((is >> owner >> c) && c == child) continue;
        keep.push_back(line);
    }
    child_write_lines(keep);
}

// ---------------------------------------------------------------- process
class Proc {
public:
    FILE* out = nullptr;
    FILE* in = nullptr;
    int exit_code = -1;

    ~Proc() {
        if (in) {
            std::fclose(in);
            in = nullptr;
        }
        if (!reaped_) kill();
        if (out) {
            drain_out_discard();
            std::fclose(out);
            out = nullptr;
        }
        wait_close(500);
    }

    // capture_err pipes the child's stderr into a small tail buffer — the
    // child can never block on a full stderr pipe again and failures carry
    // the real ffmpeg message instead of silence
    bool spawn(const std::vector<std::string>& argv, bool capture_out, bool feed_in,
               bool capture_err = false) {
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE orr = nullptr, owr = nullptr, irr = nullptr, iwr = nullptr, errr = nullptr, errw = nullptr;
        if (capture_out && !CreatePipe(&orr, &owr, &sa, 0)) return false;
        if (feed_in && !CreatePipe(&irr, &iwr, &sa, 0)) return false;
        if (capture_err && !CreatePipe(&errr, &errw, &sa, 0)) return false;
        // the parent-side ends must not leak into the child: bInheritHandles
        // copies every inheritable handle, and a child holding its own stdin
        // WRITE end never sees EOF — the encoder waits for input forever
        if (orr) SetHandleInformation(orr, HANDLE_FLAG_INHERIT, 0);
        if (iwr) SetHandleInformation(iwr, HANDLE_FLAG_INHERIT, 0);
        if (errr) SetHandleInformation(errr, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = feed_in ? irr : HANDLE(_get_osfhandle(_fileno(stdin)));
        si.hStdOutput = capture_out ? owr : HANDLE(_get_osfhandle(_fileno(stdout)));
        si.hStdError = capture_err ? errw : HANDLE(_get_osfhandle(_fileno(stderr)));
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
            if (orr) CloseHandle(orr);
            if (irr) CloseHandle(irr);
            if (errr) CloseHandle(errr);
            if (errw) CloseHandle(errw);
            return false;
        }
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (job_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &li, sizeof(li));
            AssignProcessToJobObject(job_, pi.hProcess);
        }
        if (owr) CloseHandle(owr);
        if (irr) CloseHandle(irr);
        if (errw) CloseHandle(errw);
        if (capture_out) out = _fdopen(_open_osfhandle((intptr_t)orr, 0), "rb");
        if (feed_in) in = _fdopen(_open_osfhandle((intptr_t)iwr, 0), "wb");
        if (capture_err) start_err_thread((intptr_t)errr);
#else
        int op[2] = {-1, -1}, ip[2] = {-1, -1}, ep[2] = {-1, -1};
        if (capture_out && pipe(op) != 0) return false;
        if (feed_in && pipe(ip) != 0) return false;
        if (capture_err && pipe(ep) != 0) return false;
        pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (capture_out) { dup2(op[1], 1); close(op[0]); close(op[1]); }
            if (feed_in) { dup2(ip[0], 0); close(ip[0]); close(ip[1]); }
            if (capture_err) { dup2(ep[1], 2); close(ep[0]); close(ep[1]); }
            std::vector<char*> av;
            for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
            av.push_back(nullptr);
            execvp(av[0], av.data());
            _exit(127);
        }
        if (op[1] >= 0) close(op[1]);
        if (ip[0] >= 0) close(ip[0]);
        if (ep[1] >= 0) close(ep[1]);
        if (capture_out) out = fdopen(op[0], "rb");
        if (feed_in) in = fdopen(ip[1], "wb");
        if (capture_err) start_err_thread(ep[0]);
#endif
        registered_ = child_pid();
        child_register(self_pid(), registered_, base_tool_name(argv));
        return true;
    }

    // the hard stop: terminate the child now so readers at the other end of
    // its pipes wake up with EOF instead of waiting forever
    void kill() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lk(lifecycle_);
        if (pi.hProcess && !reaped_) TerminateProcess(pi.hProcess, (UINT)-1);
#else
        std::lock_guard<std::mutex> lk(lifecycle_);
        if (pid > 0 && !reaped_) ::kill(pid, SIGKILL);
#endif
    }

    // fclose out from the reader thread while a concurrent kill() may be in
    // flight — the FILE* must swap to nullptr under the same lock kill() uses,
    // or two threads can fclose one FILE* and take the heap down
    void abandon_out() {
        std::lock_guard<std::mutex> lk(lifecycle_);
        if (out) {
            std::fclose(out);
            out = nullptr;
        }
    }

    // wait for the child, drain whatever is left of stdout, reap. timeout in
    // milliseconds, negative waits forever — on timeout the child is killed
    // and the wait finishes on the corpse, so no caller can hang here. the
    // wait itself happens outside the lock so a concurrent kill() (the gui
    // stopping a player decoder) lands immediately.
    int wait_close(int timeout_ms = -1) {
        {
            std::lock_guard<std::mutex> lk(lifecycle_);
            if (in) {
                std::fclose(in);
                in = nullptr;
            }
            if (out) {
                drain_out_discard();
                std::fclose(out);
                out = nullptr;
            }
        }
#ifdef _WIN32
        HANDLE h = nullptr;
        {
            std::lock_guard<std::mutex> lk(lifecycle_);
            if (!reaped_ && pi.hProcess) h = pi.hProcess;
        }
        if (h) {
            DWORD w = WaitForSingleObject(h, timeout_ms < 0 ? INFINITE : DWORD(timeout_ms));
            if (w != WAIT_OBJECT_0) TerminateProcess(h, (UINT)-1);
            WaitForSingleObject(h, INFINITE);
            std::lock_guard<std::mutex> lk(lifecycle_);
            if (!reaped_) {
                DWORD code = 0;
                GetExitCodeProcess(h, &code);
                exit_code = (int)code;
                if (job_) CloseHandle(job_), job_ = nullptr;
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                pi = PROCESS_INFORMATION{};
                reaped_ = true;
            }
        }
#else
        pid_t target = -1;
        {
            std::lock_guard<std::mutex> lk(lifecycle_);
            if (!reaped_ && pid > 0) target = pid;
        }
        if (target > 0) {
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(timeout_ms < 0 ? 3600000 : timeout_ms);
            int st = 0;
            while (waitpid(target, &st, WNOHANG) == 0) {
                if (std::chrono::steady_clock::now() > deadline) {
                    ::kill(target, SIGKILL);
                    waitpid(target, &st, 0);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            std::lock_guard<std::mutex> lk(lifecycle_);
            if (!reaped_) {
                exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                pid = -1;
                reaped_ = true;
            }
        }
#endif
        join_err_thread();
        child_unregister(registered_);
        return exit_code;
    }

    std::string stderr_tail() {
        std::lock_guard<std::mutex> lk(err_mutex_);
        return err_tail_;
    }

private:
#ifdef _WIN32
    PROCESS_INFORMATION pi{};
    HANDLE job_ = nullptr;
    static std::string quote(const std::string& s) {
        if (s.find(' ') == std::string::npos && !s.empty()) return s;
        return '"' + s + '"';
    }
    long child_pid() const { return pi.hProcess ? (long)pi.dwProcessId : -1; }
#else
    pid_t pid = -1;
    long child_pid() const { return (long)pid; }
#endif
    std::mutex lifecycle_;
    std::mutex err_mutex_;
    std::thread err_th_;
    std::string err_tail_;
    bool reaped_ = false;
    long registered_ = -1;

    static std::string base_tool_name(const std::vector<std::string>& argv) {
        if (argv.empty()) return "child";
        const std::string& a = argv[0];
        std::string::size_type s = a.find_last_of("/\\");
        std::string base = (s == std::string::npos) ? a : a.substr(s + 1);
        for (char& c : base) c = char(std::tolower((unsigned char)c));
        return base;
    }

    void start_err_thread(intptr_t handle) {
        err_th_ = std::thread([this, handle] {
            std::string acc;
#ifdef _WIN32
            HANDLE h = (HANDLE)handle;
            char buf[4096];
            DWORD nread = 0;
            while (ReadFile(h, buf, sizeof(buf), &nread, nullptr) && nread) {
                acc.append(buf, nread);
                if (acc.size() > 8192) acc.erase(0, acc.size() - 8192);
            }
            CloseHandle(h);
#else
            int fd = (int)handle;
            char buf[4096];
            ssize_t n;
            while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
                acc.append(buf, size_t(n));
                if (acc.size() > 8192) acc.erase(0, acc.size() - 8192);
            }
            ::close(fd);
#endif
            std::lock_guard<std::mutex> lk(err_mutex_);
            err_tail_ = acc;
        });
    }

    void join_err_thread() {
        if (err_th_.joinable()) err_th_.join();
    }

    void drain_out_discard() {
        if (!out) return;
        char buf[16384];
        while (std::fread(buf, 1, sizeof(buf), out) > 0) {}
    }
};

inline bool run_ok(const std::vector<std::string>& argv, std::string* capture = nullptr,
                   bool quiet_stderr = false) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE orr = nullptr, owr = nullptr;
    bool need = capture != nullptr;
    if (need && !CreatePipe(&orr, &owr, &sa, 0)) return false;
    HANDLE nul = nullptr;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = HANDLE(_get_osfhandle(_fileno(stdin)));
    si.hStdOutput = need ? owr : HANDLE(_get_osfhandle(_fileno(stdout)));
    if (quiet_stderr) {
        nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        si.hStdError = nul ? nul : HANDLE(_get_osfhandle(_fileno(stderr)));
    } else {
        si.hStdError = HANDLE(_get_osfhandle(_fileno(stderr)));
    }
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
    if (nul) CloseHandle(nul);
    if (owr) CloseHandle(owr);
    if (!okf) return false;
    // the pipe is drained BEFORE the wait — a child that outgrew the buffer
    // must not deadlock against its own output
    if (need && orr) {
        char buf[4096];
        DWORD nread = 0;
        while (ReadFile(orr, buf, sizeof(buf), &nread, nullptr) && nread)
            capture->append(buf, nread);
        CloseHandle(orr);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
#else
    int op[2] = {-1, -1}, ep[2] = {-1, -1};
    if (capture && pipe(op) != 0) return false;
    if (quiet_stderr && pipe(ep) != 0) return false;
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
        if (capture) { dup2(op[1], 1); close(op[0]); close(op[1]); }
        if (quiet_stderr) { dup2(ep[1], 2); close(ep[0]); close(ep[1]); }
        std::vector<char*> av;
        for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        _exit(127);
    }
    if (op[1] >= 0) close(op[1]);
    if (ep[1] >= 0) close(ep[1]);
    if (capture) {
        char buf[4096];
        ssize_t n;
        while ((n = read(op[0], buf, sizeof(buf))) > 0) capture->append(buf, n);
        close(op[0]);
    }
    if (quiet_stderr) {
        char buf[4096];
        while (read(ep[0], buf, sizeof(buf)) > 0) {}
        close(ep[0]);
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
    // gui speaks through its redirected pipes — only the raw cli double-click
    // needs the parent console
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
