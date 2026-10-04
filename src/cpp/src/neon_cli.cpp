// NEONIFY native — CLI entry. interactive mode is a 1:1 port of the old
// python cli (sections, wording, ranges, order, messages).
#define STB_IMAGE_IMPLEMENTATION_HERE
#include "neon_common.h"
#include "neon_image.h"
#include "neon_audio.h"
#include "neon_video.h"
#include "neon_mesh.h"
#include "neon_options.h"

#include <cstring>
#include <csignal>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#else
#include <dirent.h>
#endif

#ifdef NEONIFY_WITH_GUI
namespace neon_gui { int run_gui(int argc, char** argv); }
#endif

namespace neon {

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::string unquote(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

inline std::string base_name(const std::string& p) {
    auto s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

inline bool is_ext(const std::string& p, const std::vector<const char*>& exts) {
    std::string e = lower_ext(p);
    for (const char* x : exts)
        if (e == x) return true;
    return false;
}

inline bool is_dir(const std::string& p) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring w((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], wlen);
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

inline std::vector<std::string> list_dir(const std::string& p) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    std::wstring pat = wide_from_utf8(p + "\\*");
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string nm = utf8_from_wide(fd.cFileName);
            if (nm != "." && nm != "..") out.push_back(nm);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(p.c_str());
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            std::string nm = e->d_name;
            if (nm != "." && nm != "..") out.push_back(nm);
        }
        closedir(d);
    }
#endif
    return out;
}

inline std::vector<std::string> collect_inputs(const std::vector<std::string>& in,
                                               const std::vector<const char*>& exts) {
    std::vector<std::string> out;
    for (const auto& p : in) {
        if (is_dir(p)) {
            std::vector<std::string> names = list_dir(p);
            std::sort(names.begin(), names.end());
            for (const auto& nm : names) {
                std::string fp = p + "/" + nm;
                if (file_exists(fp) && is_ext(fp, exts)) out.push_back(fp);
            }
        } else if (file_exists(p) && is_ext(p, exts)) {
            out.push_back(p);
        }
    }
    return out;
}

inline std::string which_path(const std::string& tool) {
#ifdef _WIN32
    std::string cap;
    if (!run_ok({"where", tool}, &cap)) return "";
    auto nl = cap.find('\r');
    if (nl == std::string::npos) nl = cap.find('\n');
    return (nl == std::string::npos) ? std::string() : cap.substr(0, nl);
#else
    std::string cmd = "command -v " + tool + " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    char buf[1024] = {0};
    const char* got = std::fgets(buf, sizeof(buf), f);
    pclose(f);
    if (!got) return "";
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
#endif
}

// ---------------------------------------------------------------- prompts

inline std::string fmt_g(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

inline std::string ask_line(const std::string& label) {
    std::printf("%s", label.c_str());
    std::flush(std::cout);
    std::string raw;
    if (!std::getline(std::cin, raw)) return "";
    return trim(raw);
}

// python select_float: typed ranges, enter keeps the default
inline float ask_float(const std::string& prompt, float dflt, float lo, float hi) {
    while (true) {
        std::string raw = ask_line(prompt + " [" + fmt_g(lo) + "-" + fmt_g(hi) +
                                   "] (Enter for " + fmt_g(dflt) + "): ");
        if (raw.empty()) return dflt;
        char* end = nullptr;
        float v = std::strtof(raw.c_str(), &end);
        if (end == raw.c_str()) {
            std::printf("Invalid number '%s'.\n", raw.c_str());
            continue;
        }
        if (v >= lo && v <= hi) return v;
        std::printf("Value must be between %s and %s.\n", fmt_g(lo).c_str(), fmt_g(hi).c_str());
    }
}

inline std::string cap_name(const char* p) {
    std::string s = p;
    if (!s.empty()) s[0] = char(std::toupper((unsigned char)s[0]));
    return s;
}

// python select_palette: numbered, capitalized, enter = 1
inline std::string ask_palette() {
    int count = int(sizeof(PALETTE_NAMES) / sizeof(PALETTE_NAMES[0]));
    std::printf("\nSelect palette:\n");
    for (int i = 0; i < count; i++)
        std::printf("  %d. %s\n", i + 1, cap_name(PALETTE_NAMES[i]).c_str());
    while (true) {
        std::string raw = ask_line("\nSelect palette (1-" + std::to_string(count) + ", default 1): ");
        if (raw.empty() || raw == "1") return "electric";
        if (raw.size() == 1 && raw[0] >= '1' && raw[0] <= char('0' + count))
            return PALETTE_NAMES[raw[0] - '1'];
        for (const char* p : PALETTE_NAMES)
            if (raw == p) return raw;
        std::printf("Invalid choice '%s'. Please enter a number from 1 to %d.\n", raw.c_str(), count);
    }
}

// python select_glow
inline float ask_glow() {
    std::printf("\nGlow intensity options:\n");
    std::printf("  1. Subtle (0.6)\n");
    std::printf("  2. Normal (1.0, default)\n");
    std::printf("  3. Strong (1.6)\n");
    std::printf("  4. Extreme (2.4)\n");
    while (true) {
        std::string raw = ask_line("\nSelect glow (1-4, default 2): ");
        if (raw.empty() || raw == "2") return 1.0f;
        if (raw == "1") return 0.6f;
        if (raw == "3") return 1.6f;
        if (raw == "4") return 2.4f;
        std::printf("Invalid choice '%s'. Please enter 1, 2, 3, or 4.\n", raw.c_str());
    }
}

// python select_threshold
inline float ask_threshold() {
    std::printf("\nEdge threshold options:\n");
    std::printf("  1. Sensitive - more edges, busier art (0.06)\n");
    std::printf("  2. Balanced (0.12, default)\n");
    std::printf("  3. Strict - only strong edges (0.22)\n");
    while (true) {
        std::string raw = ask_line("\nSelect threshold (1-3, default 2): ");
        if (raw.empty() || raw == "2") return 0.12f;
        if (raw == "1") return 0.06f;
        if (raw == "3") return 0.22f;
        std::printf("Invalid choice '%s'. Please enter 1, 2, or 3.\n", raw.c_str());
    }
}

inline std::string ask_audio_profile(const std::string& default_profile = "slash") {
    std::printf("\nNeon audio profile:\n");
    int count = int(sizeof(AUDIO_PROFILES) / sizeof(AUDIO_PROFILES[0]));
    for (int i = 0; i < count; i++) {
        std::string mark = (AUDIO_PROFILES[i] == default_profile) ? " (default)" : "";
        std::printf("  %d. %-8s %s%s\n", i + 1, AUDIO_PROFILES[i],
                    PROFILE_DESCRIPTIONS(AUDIO_PROFILES[i]), mark.c_str());
    }
    while (true) {
        std::string raw = ask_line("\nSelect profile (1-" + std::to_string(count) +
                                   ", default " + default_profile + "): ");
        for (char& c : raw) c = char(std::tolower((unsigned char)c));
        if (raw.empty()) return default_profile;
        for (const char* n : AUDIO_PROFILES)
            if (raw == n) return raw;
        if (raw.size() == 1 && raw[0] >= '1' && raw[0] <= char('0' + count))
            return AUDIO_PROFILES[raw[0] - '1'];
        std::printf("Invalid choice '%s'. Please enter a number from 1 to %d or a profile name.\n",
                    raw.c_str(), count);
    }
}

// python ask_advanced_audio: step-by-step, out-of-range and bad input keep defaults
inline bool ask_advanced_audio(const std::string& profile, Advanced& out) {
    bool any = false;
    std::printf("\nadvanced audio settings for '%s' (enter = keep default)\n", profile.c_str());
    for (int i = 0; i < ADV_SCHEMA_ROWS; i++) {
        if (profile != ADV_SCHEMA[i][0]) continue;
        float lo = adv_lo(i), hi = adv_hi(i), dflt = adv_default(i);
        std::string raw = ask_line("  " + std::string(ADV_SCHEMA[i][2]) + " [" + fmt_g(dflt) + "] > ");
        if (raw.empty()) continue;
        char* end = nullptr;
        float v = std::strtof(raw.c_str(), &end);
        if (end == raw.c_str()) {
            std::printf("    not a number, keeping default\n");
            continue;
        }
        if (v < lo || v > hi) {
            std::printf("    out of range %s-%s, keeping default\n",
                        fmt_g(lo).c_str(), fmt_g(hi).c_str());
            continue;
        }
        out.params[ADV_SCHEMA[i][1]] = (std::string(ADV_SCHEMA[i][1]) == "bits") ? float(int(v)) : v;
        any = true;
    }
    return any;
}

// ---------------------------------------------------------------- old-look progress

inline std::string fmt_mmss(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = int(seconds);
    char buf[16];
    if (total < 3600) std::snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    else std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    return buf;
}

inline std::string bar30(double frac) {
    frac = std::max(0.0, std::min(1.0, frac));
    int filled = int(30 * frac);
    std::string s;
    for (int i = 0; i < filled; i++) s += "\xe2\x96\x88";
    for (int i = filled; i < 30; i++) s += "\xe2\x96\x91";
    return s;
}

inline void rline(const std::string& s, size_t pad_to) {
    std::string out = s;
    if (out.size() < pad_to) out += std::string(pad_to - out.size(), ' ');
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}

class BatchRunner {
public:
    std::vector<std::string> outputs;
    int processed = 0;
    int errors = 0;

    BatchRunner(std::string file_type, const Options& o)
        : type(std::move(file_type)), opts(o) {
        t0 = std::chrono::steady_clock::now();
    }

    void header(int total) {
        n_total = total;
        std::printf("\n============================================================\n");
        std::printf("Processing %d %s%s - Mode: %s\n", total, type.c_str(), total == 1 ? "" : "s",
                    mode.c_str());
        std::printf("Palette: %s | Glow: %s | Threshold: %s\n", opts.palette.c_str(),
                    fmt_g(opts.glow).c_str(), fmt_g(opts.threshold).c_str());
        std::printf("============================================================\n\n");
    }

    // one file: engine stages drive the old-style bar; errors are inline and counted.
    // the tracker runs enabled + quiet — its hook draws the line here; a disabled
    // tracker never draws at all, which is exactly the silence this replaced
    template <typename F>
    void run_file(int idx, const std::string& inp, const std::string& out_path, F&& f) {
        std::string disp = base_name(inp);
        if (disp.size() > 25) disp = disp.substr(0, 22) + "...";
        StageTracker tr(true);
        tr.quiet = true;
        auto t_start = std::chrono::steady_clock::now();
        bool is_image = is_ext(inp, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"});
        try {
            if (is_image && mode == "neon") {
                tr.set_stages({"edges", "bloom"});
                tr.hook = [&](const std::string& label, double frac) {
                    (void)label;
                    int step = (frac >= 0.5) ? 2 : 1;
                    double fill = std::max(0.0, std::min(1.0, frac));
                    char line[256];
                    std::snprintf(line, sizeof(line), "\r  %s [%s] %d/2 (%5.1f%%) - %s",
                                  disp.c_str(), bar30(fill).c_str(), step,
                                  fill * 100.0, step == 2 ? "Blooming..." : "Detecting edges...");
                    std::string s(line);
                    if (s.size() < last_len) s += std::string(last_len - s.size(), ' ');
                    last_len = s.size();
                    std::fputs(s.c_str(), stdout);
                    std::fflush(stdout);
                };
            } else {
                tr.hook = [&](const std::string& label, double frac) {
                    double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
                    double eta = (frac > 0.02) ? now * (1.0 - frac) / frac : 0.0;
                    char line[256];
                    std::snprintf(line, sizeof(line), "\r  %s [%s] %5.1f%% - %s (%s<%s)",
                                  disp.c_str(), bar30(frac).c_str(), frac * 100.0, label.c_str(),
                                  fmt_mmss(now).c_str(), fmt_mmss(eta).c_str());
                    std::string s(line);
                    if (s.size() < last_len) s += std::string(last_len - s.size(), ' ');
                    last_len = s.size();
                    std::fputs(s.c_str(), stdout);
                    std::fflush(stdout);
                };
            }
            std::string result = f(inp, out_path, &tr);
            std::string done = "\r  " + disp + " [" + bar30(1.0) + "] 100.0% - Done!";
            rline(done, last_len);
            std::fputs("\n", stdout);
            last_len = 0;
            outputs.push_back(result);
            processed++;
        } catch (const std::exception& e) {
            if (last_len) {
                std::fputs("\n", stdout);
                last_len = 0;
            }
            std::string msg = e.what();
            if (msg.size() > 160) msg = msg.substr(0, 160);
            std::printf("%s  Error: %s%s\n", NEON_DIM, msg.c_str(), NEON_RESET);
            errors++;
        } catch (...) {
            if (last_len) {
                std::fputs("\n", stdout);
                last_len = 0;
            }
            std::printf("%s  Error: unknown failure%s\n", NEON_DIM, NEON_RESET);
            errors++;
        }
    }

    void footer() {
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\n============================================================\n");
        std::printf("Completed: %d/%d %s%s\n", processed, n_total, type.c_str(), n_total == 1 ? "" : "s");
        std::printf("Total time: %s\n", fmt_mmss(s).c_str());
        if (errors > 0) std::printf("Errors: %d\n", errors);
        std::printf("============================================================\n");
    }

    std::string mode = "neon";

private:
    std::string type;
    Options opts;
    int n_total = 0;
    size_t last_len = 0;
    std::chrono::steady_clock::time_point t0;
};

// ---------------------------------------------------------------- outputs

// python output_for_arg: folder (trailing slash, existing dir, or extensionless
// and missing) collects the auto name; a file path is used exactly
inline std::string output_for_arg(const std::string& inp, const std::string& out_arg,
                                  const std::string& tag, const std::string& ext,
                                  bool next_to_input) {
    if (out_arg.empty()) return default_output_for(inp, tag, ext, next_to_input);
    bool is_folder = (!out_arg.empty() && (out_arg.back() == '/' || out_arg.back() == '\\')) ||
                     is_dir(out_arg);
    if (!is_folder) {
        auto dot = out_arg.find_last_of('.');
        auto slash = out_arg.find_last_of("/\\");
        bool has_ext = dot != std::string::npos && (slash == std::string::npos || dot > slash);
        if (!has_ext && !file_exists(out_arg)) is_folder = true;
    }
    if (is_folder) {
        std::string dir = out_arg;
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += "/";
        make_dir(dir);  // a custom folder the user named must come to life
        return dir + out_default(inp, tag, ext);
    }
    return out_arg;
}

inline std::pair<std::string, std::string> tag_and_ext_for(const std::string& command,
                                                           const std::string& inp,
                                                           const Options& o) {
    std::string tag, ext;
    if (command == "audio") {
        std::string prof = o.profile.empty() ? "slash" : o.profile;
        tag = audio_tag(prof, o.audio_intensity);
        ext = ".wav";
    } else if (command == "mesh") {
        tag = std::string(o.palette) + "3d";
        ext = o.turntable > 0 ? ".mp4" : ".png";
    } else if (is_ext(inp, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"})) {
        tag = o.palette;
        ext = ".mp4";
    } else {
        tag = o.palette;
        ext = ".png";
    }
    return {tag, ext};
}

inline std::vector<std::pair<std::string, std::string>> resolve_pairs(
        const std::vector<std::string>& inputs, const std::string& command, const Options& o) {
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& inp : inputs) {
        auto te = tag_and_ext_for(command, inp, o);
        pairs.emplace_back(inp, output_for_arg(inp, o.output, te.first, te.second, o.next_to_input));
    }
    return pairs;
}

// ---------------------------------------------------------------- engines

inline std::string run_one(const std::string& inp, const std::string& out_path,
                           const std::string& command, const Options& o, StageTracker* tr) {
    if (command == "neon") {
        if (is_ext(inp, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"})) {
            auto r = process_video_file(inp, out_path, o.palette, o.glow, o.threshold, o.env,
                                        o.profile, o.spatial, o.neon_audio, o.advanced, tr,
                                        o.inside_mode, o.hwaccel, o.audio_intensity);
            return r.first;
        }
        EdgeAux aux;
        return neonize_image_file(inp, out_path, o.palette, o.glow, o.threshold, o.env, tr, &aux,
                                  o.inside_mode);
    }
    if (command == "audio") {
        std::string prof = o.profile.empty() ? "slash" : o.profile;
        return neonize_audio_file(inp, out_path, prof, o.glow, o.advanced, tr, o.audio_intensity);
    }
    if (command == "mesh") {
        if (!is_ext(inp, {".obj", ".ply", ".stl"}))
            return neonize_relief_file(inp, out_path, o.palette, o.glow, o.depth, o.turntable,
                                       o.azimuth, o.elevation, tr, o.export_mesh);
        return neonize_mesh_file(inp, out_path, o.palette, o.glow, o.turntable, o.azimuth,
                                 o.elevation, tr);
    }
    throw std::runtime_error("unknown command: " + command);
}

inline std::vector<std::string> process_pairs(const std::vector<std::pair<std::string, std::string>>& pairs,
                                              const std::string& command, const std::string& file_type,
                                              const Options& o) {
    if (pairs.empty()) return {};
    BatchRunner batch(file_type, o);
    batch.mode = command;
    batch.header(int(pairs.size()));
    int i = 1;
    for (const auto& pr : pairs) {
        batch.run_file(i++, pr.first, pr.second, [&](const std::string& in, const std::string& out,
                                                     StageTracker* tr) {
            return run_one(in, out, command, o, tr);
        });
    }
    batch.footer();
    if (!batch.outputs.empty()) {
        std::printf("\nGenerated files:\n");
        for (const auto& outp : batch.outputs) std::printf("  %s\n", outp.c_str());
    }
    return batch.outputs;
}

// ---------------------------------------------------------------- interactive
// 1:1 port of the old python interactive flow. sections, wording and order
// follow the original; the settings lists carry the current engine's options.

inline std::vector<std::string> parse_multi_paths(const std::string& raw) {
    std::vector<std::string> paths;
    std::string cur;
    bool in_q = false;
    char qc = 0;
    for (char c : raw) {
        if (c == '"' || c == '\'') {
            if (!in_q) {
                in_q = true;
                qc = c;
            } else if (c == qc) {
                in_q = false;
                qc = 0;
            } else {
                cur += c;
            }
        } else if ((c == ' ' || c == '\t') && !in_q) {
            if (!cur.empty()) {
                paths.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) paths.push_back(cur);
    return paths;
}

struct Categorized {
    std::vector<std::pair<std::string, std::vector<std::string>>> valid;
    std::vector<std::string> not_exist;
    std::vector<std::string> not_supported;
    std::vector<std::string> invalid;
    std::vector<std::string> all_files;
};

inline Categorized categorize(const std::vector<std::string>& paths) {
    static const std::vector<const char*> img = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"};
    static const std::vector<const char*> vid = {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"};
    static const std::vector<const char*> aud = {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"};
    static const std::vector<const char*> mesh = {".obj", ".ply", ".stl"};
    Categorized c;
    for (const auto& p : paths) {
        if (!file_exists(p) && !is_dir(p)) {
            bool has_alnum = false;
            for (char ch : p)
                if (std::isalnum((unsigned char)ch)) { has_alnum = true; break; }
            if (has_alnum) c.not_exist.push_back(p);
            else c.invalid.push_back(p);
            continue;
        }
        if (is_dir(p)) {
            std::vector<std::string> files;
            for (const auto* exts : {&img, &vid, &aud, &mesh}) {
                auto got = collect_inputs({p}, *exts);
                files.insert(files.end(), got.begin(), got.end());
            }
            std::sort(files.begin(), files.end());
            if (!files.empty()) {
                c.valid.emplace_back(p, files);
                c.all_files.insert(c.all_files.end(), files.begin(), files.end());
            } else {
                c.not_supported.push_back(p);
            }
            continue;
        }
        bool sup = false;
        for (const auto* exts : {&img, &vid, &aud, &mesh})
            if (is_ext(p, *exts)) { sup = true; break; }
        if (sup) {
            c.valid.emplace_back(p, std::vector<std::string>{p});
            c.all_files.push_back(p);
        } else {
            c.not_supported.push_back(p);
        }
    }
    return c;
}

inline void display_path_summary(const Categorized& c, int max_display = 5) {
    size_t total = c.valid.size() + c.not_exist.size() + c.not_supported.size() + c.invalid.size();
    if (!total) return;
    std::printf("\n------------------------------------------------------------\n");
    std::printf("INPUT PATH SUMMARY\n");
    std::printf("------------------------------------------------------------\n");
    auto show = [&](const std::vector<std::string>& v, const char* mark, const char* title) {
        if (v.empty()) return;
        std::printf("\n%s %s (%d):\n", mark, title, int(v.size()));
        int n = std::min<int>(int(v.size()), max_display);
        for (int i = 0; i < n; i++) std::printf("   %s\n", v[i].c_str());
        if (int(v.size()) > max_display)
            std::printf("   ... +%d more %s\n", int(v.size()) - max_display, title);
    };
    if (!c.valid.empty()) {
        std::printf("\n\xe2\x9c\x93 VALID (%d):\n", int(c.valid.size()));
        int n = std::min<int>(int(c.valid.size()), max_display);
        for (int i = 0; i < n; i++) {
            if (c.valid[i].second.size() == 1) std::printf("   %s\n", c.valid[i].first.c_str());
            else std::printf("   %s/ (%d files)\n", c.valid[i].first.c_str(),
                             int(c.valid[i].second.size()));
        }
        if (int(c.valid.size()) > max_display)
            std::printf("   ... +%d more valid\n", int(c.valid.size()) - max_display);
    }
    show(c.not_exist, "\xe2\x9c\x97", "NOT FOUND");
    show(c.not_supported, "\xe2\x9a\xa0", "NOT SUPPORTED");
    show(c.invalid, "?", "INVALID INPUT");
    if (!c.all_files.empty())
        std::printf("\n\xe2\x86\x92 %d file(s) ready to process from %d valid path(s)\n",
                    int(c.all_files.size()), int(c.valid.size()));
    else
        std::printf("\n\xe2\x86\x92 No valid files found to process\n");
}

inline void section(const char* title) {
    std::printf("\n------------------------------------------------------------\n");
    std::printf("%s\n", title);
    std::printf("------------------------------------------------------------\n");
}

inline int interactive_mode(bool ffmpeg_ok) {
    (void)ffmpeg_ok;
    print_banner();
    std::string ff = which_path("ffmpeg");
    std::printf("%sffmpeg: %s%s\n", NEON_DIM,
                ff.empty() ? "NOT FOUND (video and audio need it \xe2\x80\x94 install ffmpeg)"
                           : ff.c_str(),
                NEON_RESET);
    while (true) {
        section("INPUT SELECTION");
        std::printf("\nEnter input path(s) - separate multiple paths with spaces\n");
        std::printf("Images, videos, audio and meshes can be mixed freely\n");
        std::printf("Use quotes for paths with spaces, or 'q' to quit:\n");
        std::string raw = ask_line("> ");
        if (raw.empty()) {
            std::printf("Error: No input provided. Please enter at least one path or 'q' to quit.\n");
            continue;
        }
        std::string low = raw;
        for (char& c : low) c = char(std::tolower((unsigned char)c));
        if (low == "q" || low == "quit" || low == "exit") {
            std::printf("\nExiting Neonify. Stay glowing!\n");
            return 0;
        }
        std::vector<std::string> paths = parse_multi_paths(raw);
        if (paths.empty()) {
            std::printf("Error: Could not parse any paths from input. Please try again.\n");
            continue;
        }
        Categorized cat = categorize(paths);
        display_path_summary(cat);
        if (cat.all_files.empty()) {
            std::printf("\n------------------------------------------------------------\n");
            bool has_errors = !cat.not_exist.empty() || !cat.not_supported.empty() || !cat.invalid.empty();
            if (has_errors) {
                std::printf("Options:\n");
                std::printf("  1. Enter different paths\n");
                std::printf("  2. Exit\n");
                std::string pick = ask_line("\nSelect option (1 or 2): ");
                if (pick != "1") {
                    std::printf("\nExiting Neonify. Stay glowing!\n");
                    return 0;
                }
                continue;
            }
            std::printf("Please enter valid paths.\n");
            continue;
        }

        const auto& files = cat.all_files;
        std::vector<std::string> images, videos, audios, meshes;
        for (const auto& f : files) {
            if (is_ext(f, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"})) images.push_back(f);
            else if (is_ext(f, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"})) videos.push_back(f);
            else if (is_ext(f, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"})) audios.push_back(f);
            else meshes.push_back(f);
        }
        section("FILES TO PROCESS");
        std::printf("  Images: %d\n", int(images.size()));
        std::printf("  Videos: %d\n", int(videos.size()));
        std::printf("  Audio:  %d\n", int(audios.size()));
        std::printf("  Meshes: %d\n", int(meshes.size()));

        section("SHARED SETTINGS");
        Options o;
        o.palette = ask_palette();
        o.glow = ask_glow();
        o.threshold = ask_threshold();
        o.env = ask_float("Ambient detail", 1.0f, 0.0f, 2.0f);
        if (!images.empty() || !videos.empty()) {
            std::printf("\nKeep the original under the effect (instead of wiping to black):\n");
            std::printf("  1. no - wipe, neon on black (default)\n");
            std::printf("  2. keep original, neon only on the edges\n");
            std::printf("  3. keep original + global neon glow\n");
            while (true) {
                std::string pick = ask_line("\nSelect keep mode (1-3, default 1): ");
                if (pick.empty() || pick == "1") { o.inside_mode = 0; break; }
                if (pick == "2") { o.inside_mode = 1; break; }
                if (pick == "3") { o.inside_mode = 2; break; }
                std::printf("Invalid choice '%s'. Please enter 1, 2, or 3.\n", pick.c_str());
            }
        }
        if (!videos.empty()) {
            section("VIDEO SETTINGS");
            o.spatial = trim(ask_line("\nSpatial glow (stereo \xe2\x86\x92 direction) [Y/n]: ")) != "n";
            o.neon_audio = trim(ask_line("\nNeonify the audio too? [y/N]: ")) == "y";
            if (o.neon_audio) {
                o.profile = ask_audio_profile();
                if (trim(ask_line("\nAdvanced audio settings? [y/N]: ")) == "y")
                    ask_advanced_audio(o.profile, o.advanced);
            }
        }
        if (!audios.empty()) {
            section("AUDIO SETTINGS");
            std::printf("\nThe neon audio FX rework the track through the selected profile:\n");
            std::printf("  drive, echoes, reverb, pulse and shimmer, wide stage\n");
            std::printf("  intensity follows the Glow setting\n");
            std::printf("  output: <name>_neonify_<profile>_<timestamp>.wav (stereo)\n");
            o.profile = ask_audio_profile(o.profile.empty() ? "slash" : o.profile);
            if (trim(ask_line("\nAdvanced audio settings? [y/N]: ")) == "y")
                ask_advanced_audio(o.profile, o.advanced);
        }
        if (!meshes.empty()) {
            section("MESH SETTINGS");
            std::printf("\nMesh output type:\n");
            std::printf("  1. Single neon wireframe view (PNG, default)\n");
            std::printf("  2. Turntable orbit video (MP4)\n");
            while (true) {
                std::string pick = ask_line("\nSelect output type (1-2, default 1): ");
                if (pick.empty() || pick == "1") { o.turntable = 0; break; }
                if (pick == "2") {
                    std::printf("\nTurntable frames options:\n");
                    std::printf("  1. 60 frames (2s loop)\n");
                    std::printf("  2. 120 frames (4s loop, default)\n");
                    std::printf("  3. 240 frames (8s loop)\n");
                    while (true) {
                        std::string fpick = ask_line("\nSelect frames (1-3, default 2): ");
                        if (fpick.empty() || fpick == "2") { o.turntable = 120; break; }
                        if (fpick == "1") { o.turntable = 60; break; }
                        if (fpick == "3") { o.turntable = 240; break; }
                        std::printf("Invalid choice '%s'. Please enter 1, 2, or 3.\n", fpick.c_str());
                    }
                    break;
                }
                std::printf("Invalid choice '%s'. Please enter 1 or 2.\n", pick.c_str());
            }
            if (o.turntable == 0) {
                o.azimuth = ask_float("\nAzimuth angle", 30.0f, -180.0f, 180.0f);
                o.elevation = ask_float("Elevation angle", 20.0f, -89.0f, 89.0f);
            }
        }

        section("OUTPUT SETTINGS");
        std::printf("\n  1. results folder (default)\n");
        std::printf("  2. next to each input\n");
        std::printf("  3. custom path\n");
        int loc = 1;
        while (true) {
            std::string pick = ask_line("\nSelect output location (1-3, default 1): ");
            if (pick.empty() || pick == "1") { loc = 1; break; }
            if (pick == "2") { loc = 2; break; }
            if (pick == "3") { loc = 3; break; }
            std::printf("Invalid choice '%s'. Please enter 1, 2, or 3.\n", pick.c_str());
        }
        std::string custom_arg;
        if (loc == 3) {
            std::printf("\nEnter output path.\n");
            std::printf("For folders: outputs to that folder with the auto name.\n");
            std::printf("For files: outputs exactly to that path (single input).\n");
            while (true) {
                custom_arg = unquote(ask_line("\nOutput path: "));
                if (!custom_arg.empty()) break;
                std::printf("A path is required for the custom location.\n");
            }
        }
        o.next_to_input = (loc == 2);
        o.output = (loc == 3) ? custom_arg : std::string();

        // a file path with several inputs cannot hold every result exactly —
        // fall back to folder semantics so nothing lands on top of anything
        if (loc == 3 && files.size() > 1 && !is_dir(custom_arg)) {
            auto dot = custom_arg.find_last_of('.');
            auto slash = custom_arg.find_last_of("/\\");
            bool has_ext = dot != std::string::npos && (slash == std::string::npos || dot > slash);
            if (has_ext) {
                std::string parent = (slash == std::string::npos) ? std::string(".")
                                                                  : custom_arg.substr(0, slash);
                make_dir(parent);
                custom_arg = parent;
                o.output = parent;
                std::printf("\nseveral inputs — '%s' acts as the output folder\n", parent.c_str());
            }
        }

        std::vector<std::pair<std::string, std::string>> neon_pairs, audio_pairs, mesh_pairs;
        std::printf("\nOutput mapping:\n");
        for (const auto& f : files) {
            std::string command = is_ext(f, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"})
                                      ? "audio"
                                      : (is_ext(f, {".obj", ".ply", ".stl"}) ? "mesh" : "neon");
            auto te = tag_and_ext_for(command, f, o);
            std::string outp = output_for_arg(f, o.output, te.first, te.second, o.next_to_input);
            std::printf("  %s\n    -> %s\n", f.c_str(), outp.c_str());
            if (command == "neon") neon_pairs.emplace_back(f, outp);
            else if (command == "audio") audio_pairs.emplace_back(f, outp);
            else mesh_pairs.emplace_back(f, outp);
        }

        std::vector<std::string> all_outputs;
        try {
            if (!neon_pairs.empty()) {
                auto got = process_pairs(neon_pairs, "neon", "image/video", o);
                all_outputs.insert(all_outputs.end(), got.begin(), got.end());
            }
            if (!audio_pairs.empty()) {
                auto got = process_pairs(audio_pairs, "audio", "audio", o);
                all_outputs.insert(all_outputs.end(), got.begin(), got.end());
            }
            if (!mesh_pairs.empty()) {
                auto got = process_pairs(mesh_pairs, "mesh", "mesh", o);
                all_outputs.insert(all_outputs.end(), got.begin(), got.end());
            }
        } catch (const std::exception& e) {
            std::printf("%serror: %s%s\n", NEON_PINK, e.what(), NEON_RESET);
        }

        std::printf("\n============================================================\n");
        std::printf("ALL PROCESSING COMPLETE!\n");
        std::printf("============================================================\n");
        if (!all_outputs.empty()) {
            std::printf("\nGenerated files:\n");
            for (const auto& outp : all_outputs) std::printf("  %s\n", outp.c_str());
        }
        std::printf("\nWhat would you like to do next?\n");
        std::printf("  1. Process again (start fresh)\n");
        std::printf("  2. Exit\n");
        std::string pick = ask_line("\nSelect option (1 or 2): ");
        if (pick == "1") {
            std::printf("\nStarting fresh session...\n");
            continue;
        }
        std::printf("\nExiting Neonify. Stay glowing!\n");
        return 0;
    }
}

// ---------------------------------------------------------------- usage + dispatch

inline void print_usage() {
    print_banner();
    std::printf(
        "usage: neonify [command] [inputs...] [options]\n"
        "\n"
        "modes:\n"
        "  (no args)       gui\n"
        "  cli             interactive cli\n"
        "  gui             gui\n"
        "  image | video | audio | mesh | batch | profiles\n"
        "\n"
        "options:\n"
        "  --palette <name>         electric synthwave toxic ice fire ghost spectrum\n"
        "  --glow <0.1-3.0>         glow intensity (default 1)\n"
        "  --threshold <0.02-0.5>   edge sensitivity (default 0.12)\n"
        "  --env <0-2>              ambient detail (default 1)\n"
        "  -o, --output <path>      output file, or a folder to collect the auto names\n"
        "  --next-to-input          save outputs next to the input file instead of results/\n"
        "  --keep-inside            keep the original, neon only on the edges (images/videos)\n"
        "  --global-glow            keep the original under the full global glow field\n"
        "  --profile <name>         audio profile: fire ice robotic ghost void echo slash\n"
        "  --intensity <level>      audio profile strength: normal, high (x2), extreme (x4)\n"
        "  --neon-audio             neonify the audio with the video\n"
        "  --no-spatial             disable spatial glow (stereo pan)\n"
        "  --advanced-audio <json>  override profile params, e.g. '{\"time\":0.4,\"fb\":0.5}'\n"
        "  --turntable <frames>     3d turntable frames (0 = single image)\n"
        "  --azimuth <deg>          3d view azimuth (default 30)\n"
        "  --elevation <deg>        3d view elevation (default 20)\n"
        "  --depth <0.1-3.0>        relief depth (default 0.85)\n"
        "  --export-mesh            relief runs also export the remeshed geometry as .obj\n"
        "  --hwaccel                let ffmpeg use hardware decoding if present\n"
        "  --json-progress          progress as json lines (for the gui)\n"
        "  -h, --help               this help\n"
        "\n"
        "inputs can be files or folders. default outputs land in results/ next to where\n"
        "you run from, named name_neonify_effect_timestamp.\n\n");
}

inline int run_cli(int argc, char** argv) {
#ifndef _WIN32
    console_utf8();
#endif
    std::vector<std::string> args = utf8_args(argc, argv);
    std::string command;
    std::vector<std::string> inputs;
    Options o;
    bool json_progress = false;

    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        auto next_float = [&](float dflt) { return (i + 1 < args.size()) ? std::atof(args[++i].c_str()) : dflt; };
        auto next_str = [&](const char* dflt) { return (i + 1 < args.size()) ? args[++i] : std::string(dflt); };
        auto next_int = [&](int dflt) { return (i + 1 < args.size()) ? std::atoi(args[++i].c_str()) : dflt; };
        if (a == "gui" || a == "cli" || a == "image" || a == "video" || a == "audio" || a == "mesh" ||
            a == "batch" || a == "profiles")
            command = a;
        else if (a == "--palette") o.palette = next_str("electric");
        else if (a == "--glow") o.glow = next_float(1.0f);
        else if (a == "--threshold") o.threshold = next_float(0.12f);
        else if (a == "--env") o.env = next_float(1.0f);
        else if (a == "-o" || a == "--output") o.output = next_str("");
        else if (a == "--next-to-input") o.next_to_input = true;
        else if (a == "--keep-inside") o.inside_mode = 1;
        else if (a == "--global-glow") o.inside_mode = 2;
        else if (a == "--profile") o.profile = next_str("");
        else if (a == "--intensity") o.audio_intensity = intensity_from_name(next_str("normal"));
        else if (a == "--neon-audio") o.neon_audio = true;
        else if (a == "--no-spatial") o.spatial = false;
        else if (a == "--hwaccel") o.hwaccel = true;
        else if (a == "--turntable") o.turntable = next_int(0);
        else if (a == "--azimuth") o.azimuth = next_float(30.f);
        else if (a == "--elevation") o.elevation = next_float(20.f);
        else if (a == "--depth") o.depth = next_float(0.85f);
        else if (a == "--export-mesh") o.export_mesh = true;
        else if (a == "--json-progress") json_progress = true;
        else if (a == "--advanced-audio") {
            std::string js = next_str("");
            Advanced adv;
            size_t pos = 0;
            while (pos < js.size()) {
                auto k1 = js.find('"', pos);
                if (k1 == std::string::npos) break;
                auto k2 = js.find('"', k1 + 1);
                if (k2 == std::string::npos) break;
                auto colon = js.find(':', k2);
                if (colon == std::string::npos) break;
                adv.params[js.substr(k1 + 1, k2 - k1 - 1)] = std::atof(js.c_str() + colon + 1);
                pos = colon + 1;
                auto comma = js.find(',', pos);
                pos = (comma == std::string::npos) ? js.size() : comma;
            }
            o.advanced = adv;
        } else if (!a.empty() && a[0] != '-') {
            inputs.push_back(a);
        }
    }

    for (const std::string& a : args)
        if (a == "-h" || a == "--help") { print_usage(); return 0; }

    if (command == "profiles") {
        print_banner();
        std::printf("\naudio profiles\n");
        for (const char* n : AUDIO_PROFILES)
            std::printf("  %-8s %s\n", n, PROFILE_DESCRIPTIONS(n));
        return 0;
    }
    if (command == "cli")
        return interactive_mode(which_ok("ffmpeg"));
#ifdef NEONIFY_WITH_GUI
    if ((command.empty() || command == "gui") && inputs.empty())
        return neon_gui::run_gui(argc, argv);
#endif

    if (o.glow < 0.1f || o.glow > 3.0f) {
        std::printf("Error: --glow must be between 0.1 and 3.0\n");
        return 1;
    }
    if (o.threshold < 0.02f || o.threshold > 0.5f) {
        std::printf("Error: --threshold must be between 0.02 and 0.5\n");
        return 1;
    }
    if (o.turntable < 0) {
        std::printf("Error: --turntable must be 0 (single view) or a positive frame count\n");
        return 1;
    }

    if (inputs.empty()) {
        if (command == "image" || command == "video" || command == "audio" ||
            command == "mesh" || command == "batch") {
            std::printf("Error: Input path required\n");
            std::printf("Usage: neonify <command> <input> [-o output]\n");
            std::printf("\nCommands: image (images/videos), audio, mesh\n");
            return 1;
        }
        return interactive_mode(which_ok("ffmpeg"));
    }

    for (const auto& p : inputs) {
        if (!file_exists(p) && !is_dir(p)) {
            std::printf("Error: Input not found: %s\n", p.c_str());
            return 1;
        }
    }

    bool ffmpeg_ok = which_ok("ffmpeg");
    if ((command == "video" || command == "audio") && !ffmpeg_ok) {
        std::printf("ffmpeg required for video/audio. install it, then retry.\n");
        return 1;
    }
    if (command == "video" && o.neon_audio && o.profile.empty()) o.profile = "slash";

    try {
        if (command == "batch") {
            auto imgs = collect_inputs(inputs, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"});
            auto vids = collect_inputs(inputs, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"});
            auto auds = collect_inputs(inputs, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"});
            if (imgs.empty() && vids.empty() && auds.empty()) {
                std::printf("no valid input files found\n");
                return 1;
            }
            Options vo = o;
            if (vo.profile.empty()) vo.profile = "slash";
            if (!imgs.empty()) process_pairs(resolve_pairs(imgs, "neon", o), "neon", "image/video", o);
            if (!vids.empty() && ffmpeg_ok)
                process_pairs(resolve_pairs(vids, "neon", vo), "neon", "image/video", vo);
            if (!auds.empty() && ffmpeg_ok)
                process_pairs(resolve_pairs(auds, "audio", vo), "audio", "audio", vo);
        } else if (command == "image") {
            auto files = collect_inputs(inputs, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"});
            if (files.empty()) { std::printf("no valid input files found\n"); return 1; }
            process_pairs(resolve_pairs(files, "neon", o), "neon", "image/video", o);
        } else if (command == "video") {
            auto files = collect_inputs(inputs, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"});
            if (files.empty()) { std::printf("no valid input files found\n"); return 1; }
            process_pairs(resolve_pairs(files, "neon", o), "neon", "image/video", o);
        } else if (command == "audio") {
            auto files = collect_inputs(inputs, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"});
            if (files.empty()) { std::printf("no valid input files found\n"); return 1; }
            process_pairs(resolve_pairs(files, "audio", o), "audio", "audio", o);
        } else if (command == "mesh") {
            auto files = collect_inputs(inputs, {".obj", ".ply", ".stl", ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"});
            if (files.empty()) { std::printf("no valid input files found\n"); return 1; }
            process_pairs(resolve_pairs(files, "mesh", o), "mesh", "mesh", o);
        } else {
            std::printf("Error: Unknown command '%s'\n", command.c_str());
            std::printf("Commands: gui (default), cli, image, video, audio, mesh, batch, profiles\n");
            return 2;
        }
    } catch (const std::exception& e) {
        std::printf("%serror: %s%s\n", NEON_PINK, e.what(), NEON_RESET);
        return 1;
    }
    return 0;
}

}  // namespace neon

int main(int argc, char** argv) {
#ifndef _WIN32
    // a child dying mid-write must surface as an error, never kill the app
    std::signal(SIGPIPE, SIG_IGN);
#endif
    // ffmpeg/ffprobe left stalled by a past crash or freeze get their kill now
    neon::sweep_orphan_children();
    neon::attach_parent_console(argc, argv);
    return neon::run_cli(argc, argv);
}
