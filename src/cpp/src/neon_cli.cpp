// NEONIFY native — CLI entry. interactive mode is a 1:1 port of the python cli.
#include "neon_common.h"
#include "neon_image.h"
#include "neon_audio.h"
#include "neon_video.h"
#include "neon_mesh.h"
#include "neon_options.h"

#include <cstring>
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
    size_t a = s.find_first_not_of(" \t\r\n\"'");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(a, b - a + 1);
}

inline std::string base_name(const std::string& p) {
    auto s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

inline std::string ext_of(const std::string& p) {
    return lower_ext(p);
}

inline bool is_ext(const std::string& p, const std::vector<const char*>& exts) {
    std::string e = lower_ext(p);
    for (const char* x : exts)
        if (e == x) return true;
    return false;
}

inline bool is_dir(const std::string& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

inline std::vector<std::string> list_dir(const std::string& p) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((p + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (std::string(fd.cFileName) != "." && std::string(fd.cFileName) != "..")
                out.push_back(fd.cFileName);
        } while (FindNextFileA(h, &fd));
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

// python _ask_float: empty or out-of-range quietly keeps the default
inline float ask_float(const std::string& label, float dflt, float lo, float hi) {
    std::string raw = ask_line("  " + label + " [" + fmt_g(dflt) + "] > ");
    if (raw.empty()) return dflt;
    char* end = nullptr;
    float v = std::strtof(raw.c_str(), &end);
    if (end == raw.c_str()) return dflt;
    return (v >= lo && v <= hi) ? v : dflt;
}

inline std::string ask_palette() {
    std::printf("\n%spalette%s\n", NEON_BOLD, NEON_RESET);
    int count = int(sizeof(PALETTE_NAMES) / sizeof(PALETTE_NAMES[0]));
    for (int i = 0; i < count; i++) std::printf("  %d) %s\n", i + 1, PALETTE_NAMES[i]);
    std::string raw = ask_line("  pick [1] > ");
    if (!raw.empty()) {
        if (raw.size() == 1 && raw[0] >= '1' && raw[0] <= char('0' + count))
            return PALETTE_NAMES[raw[0] - '1'];
        for (const char* p : PALETTE_NAMES)
            if (raw == p) return raw;
    }
    return "electric";
}

inline std::string ask_audio_profile(const std::string& default_profile = "slash") {
    std::printf("\n%saudio profile%s\n", NEON_BOLD, NEON_RESET);
    int count = int(sizeof(AUDIO_PROFILES) / sizeof(AUDIO_PROFILES[0]));
    for (int i = 0; i < count; i++) {
        std::string mark = (AUDIO_PROFILES[i] == default_profile) ? " \xe2\x86\x90 default" : "";
        std::printf("  %d) %-8s %s%s%s%s\n", i + 1, AUDIO_PROFILES[i], NEON_DIM,
                    PROFILE_DESCRIPTIONS(AUDIO_PROFILES[i]), NEON_RESET, mark.c_str());
    }
    std::string raw = ask_line("  pick 1-" + std::to_string(count) + " or name [default: " +
                               default_profile + "] > ");
    for (char& c : raw) c = char(std::tolower((unsigned char)c));
    if (raw.empty()) return default_profile;
    for (const char* n : AUDIO_PROFILES)
        if (raw == n) return raw;
    if (raw.size() == 1 && raw[0] >= '1' && raw[0] <= char('0' + count))
        return AUDIO_PROFILES[raw[0] - '1'];
    std::printf("  %sunknown profile, using %s%s\n", NEON_DIM, default_profile.c_str(), NEON_RESET);
    return default_profile;
}

// python ask_advanced_audio: step-by-step, out-of-range and bad input keep defaults
inline bool ask_advanced_audio(const std::string& profile, Advanced& out) {
    bool any = false;
    std::printf("\n%sadvanced audio settings for '%s' (enter = keep default)%s\n",
                NEON_DIM, profile.c_str(), NEON_RESET);
    for (int i = 0; i < ADV_SCHEMA_ROWS; i++) {
        if (profile != ADV_SCHEMA[i][0]) continue;
        float lo = adv_lo(i), hi = adv_hi(i), dflt = adv_default(i);
        std::string raw = ask_line("  " + std::string(ADV_SCHEMA[i][2]) + " [" + fmt_g(dflt) + "] > ");
        if (raw.empty()) continue;
        char* end = nullptr;
        float v = std::strtof(raw.c_str(), &end);
        if (end == raw.c_str()) {
            std::printf("    %snot a number, keeping default%s\n", NEON_DIM, NEON_RESET);
            continue;
        }
        if (v < lo || v > hi) {
            std::printf("    %sout of range %s\xe2\x80\x93%s, keeping default%s\n",
                        NEON_DIM, fmt_g(lo).c_str(), fmt_g(hi).c_str(), NEON_RESET);
            continue;
        }
        out.params[ADV_SCHEMA[i][1]] = (std::string(ADV_SCHEMA[i][1]) == "bits") ? float(int(v)) : v;
        any = true;
    }
    return any;
}

inline void check_env() {
    std::printf("%senvironment%s\n", NEON_DIM, NEON_RESET);
    std::printf("  opencv      %s\n", cv::getVersionString().c_str());
    std::string ff = which_path("ffmpeg");
    std::printf("  ffmpeg      %s\n", ff.empty() ? "not found (video/audio disabled \xe2\x80\x94 install ffmpeg)"
                                                            : ff.c_str());
    unsigned cores = std::thread::hardware_concurrency();
    std::printf("  cpu cores   %u  \xc2\xb7  device: CPU (native \xe2\x80\x94 no GPU required)\n",
                cores ? cores : 1u);
    std::printf("\n");
}

inline void list_profiles() {
    print_banner();
    std::printf("%saudio profiles%s\n", NEON_BOLD, NEON_RESET);
    for (const char* n : AUDIO_PROFILES)
        std::printf("  %-8s %s\n", n, PROFILE_DESCRIPTIONS(n));
    std::printf("\n%severy profile responds to --glow differently \xe2\x80\x94 they are not one settings sheet%s\n",
                NEON_DIM, NEON_RESET);
}

// ---------------------------------------------------------------- flows

inline void flow_image(const std::vector<std::string>& inputs, const Options& o, StageTracker& tr) {
    for (const auto& inp : inputs) {
        std::printf("%s\xe2\x96\xb6%s %s\n", NEON_BLUE, NEON_RESET, base_name(inp).c_str());
        std::string out = unique_or_default(o.output,
            default_output_for(inp, o.palette, ext_of(inp), o.next_to_input));
        EdgeAux aux;
        std::string path = neonize_image_file(inp, out, o.palette, o.glow, o.threshold, o.env, &tr, &aux);
        std::printf("%s  \xe2\x86\x92 %s   (noise %.1f, pre-blur %.2f)%s\n", NEON_DIM, path.c_str(),
                    aux.noise, aux.pre_sigma, NEON_RESET);
    }
}

inline void flow_video(const std::vector<std::string>& inputs, const Options& o, StageTracker& tr) {
    for (const auto& inp : inputs) {
        std::printf("%s\xe2\x96\xb6%s %s\n", NEON_BLUE, NEON_RESET, base_name(inp).c_str());
        std::string out = unique_or_default(o.output,
            default_output_for(inp, o.palette, ".mp4", o.next_to_input));
        auto r = process_video_file(inp, out, o.palette, o.glow, o.threshold, o.env,
                                    o.profile, o.spatial, o.neon_audio, o.advanced, &tr);
        std::printf("%s  \xe2\x86\x92 %s  (%d frames)%s\n", NEON_DIM, r.first.c_str(), r.second, NEON_RESET);
    }
}

inline void flow_audio(const std::vector<std::string>& inputs, const Options& o, StageTracker& tr) {
    for (const auto& inp : inputs) {
        std::printf("%s\xe2\x96\xb6%s %s\n", NEON_BLUE, NEON_RESET, base_name(inp).c_str());
        std::string prof = o.profile.empty() ? "slash" : o.profile;
        std::string out = unique_or_default(o.output,
            default_output_for(inp, prof, ".wav", o.next_to_input));
        std::string path = neonize_audio_file(inp, out, prof, o.glow, o.advanced, &tr);
        std::printf("%s  \xe2\x86\x92 %s%s\n", NEON_DIM, path.c_str(), NEON_RESET);
    }
}

inline void flow_mesh(const std::vector<std::string>& inputs, const Options& o, StageTracker& tr) {
    for (const auto& inp : inputs) {
        std::printf("%s\xe2\x96\xb6%s %s\n", NEON_BLUE, NEON_RESET, base_name(inp).c_str());
        std::string ext = o.turntable > 0 ? ".mp4" : ".png";
        std::string tag = std::string(o.palette) + "3d";
        std::string out = unique_or_default(o.output,
            default_output_for(inp, tag, ext, o.next_to_input));
        bool relief = !is_ext(inp, {".obj", ".ply", ".stl"});
        std::string path;
        if (relief)
            path = neonize_relief_file(inp, out, o.palette, o.glow, o.depth,
                                       o.turntable > 0 ? o.turntable : 48, o.azimuth, o.elevation, &tr,
                                       o.export_mesh);
        else
            path = neonize_mesh_file(inp, out, o.palette, o.glow, o.turntable, o.azimuth, o.elevation, &tr);
        std::printf("%s  \xe2\x86\x92 %s%s\n", NEON_DIM, path.c_str(), NEON_RESET);
    }
}

// ---------------------------------------------------------------- interactive

struct KindSpec { const char* kind; std::vector<const char*> exts; };

inline void interactive_single(const KindSpec& spec, bool ffmpeg_ok) {
    std::string kind = spec.kind;
    if (!ffmpeg_ok && (kind == "video" || kind == "audio")) {
        std::printf("%sffmpeg missing \xe2\x80\x94 %s needs it. install ffmpeg first.%s\n",
                    NEON_DIM, kind.c_str(), NEON_RESET);
        return;
    }
    std::string raw = ask_line("  input " + kind + " file(s), comma separated > ");
    if (raw.empty()) return;
    std::vector<std::string> parts;
    std::string::size_type pos = 0;
    while (pos < raw.size()) {
        auto comma = raw.find(',', pos);
        if (comma == std::string::npos) comma = raw.size();
        std::string piece = trim(raw.substr(pos, comma - pos));
        if (!piece.empty()) parts.push_back(piece);
        pos = comma + 1;
    }
    std::vector<std::string> inputs = collect_inputs(parts, spec.exts);
    if (inputs.empty()) {
        std::printf("%sno valid %s files found%s\n", NEON_DIM, kind.c_str(), NEON_RESET);
        return;
    }
    Options o;
    o.palette = ask_palette();
    o.glow = ask_float("glow", 1.0f, 0.1f, 3.0f);
    o.threshold = ask_float("edge threshold", 0.12f, 0.02f, 0.5f);
    o.env = ask_float("ambient detail", 1.0f, 0.0f, 2.0f);
    StageTracker tracker;
    try {
        if (kind == "image") {
            flow_image(inputs, o, tracker);
        } else if (kind == "video") {
            o.spatial = trim(ask_line("  spatial glow (stereo \xe2\x86\x92 direction) [Y/n] > ")) != "n";
            o.neon_audio = trim(ask_line("  neonify the audio too? [y/N] > ")) == "y";
            if (o.neon_audio) {
                o.profile = ask_audio_profile();
                Advanced adv;
                if (trim(ask_line("  advanced audio settings? [y/N] > ")) == "y")
                    ask_advanced_audio(o.profile, adv);
                o.advanced = adv;
            }
            flow_video(inputs, o, tracker);
        } else if (kind == "audio") {
            o.profile = ask_audio_profile();
            Advanced adv;
            if (trim(ask_line("  advanced audio settings? [y/N] > ")) == "y")
                ask_advanced_audio(o.profile, adv);
            o.advanced = adv;
            flow_audio(inputs, o, tracker);
        } else {  // mesh/relief
            o.turntable = int(ask_float("turntable frames (0 = single image)", 48, 0, 600));
            o.azimuth = ask_float("azimuth", 30.0f, 0.0f, 360.0f);
            o.elevation = ask_float("elevation", 20.0f, -89.0f, 89.0f);
            o.depth = ask_float("relief depth", 0.85f, 0.1f, 3.0f);
            flow_mesh(inputs, o, tracker);
        }
    } catch (const std::exception& e) {
        std::printf("%serror: %s%s\n", NEON_PINK, e.what(), NEON_RESET);
    }
}

inline void interactive_batch(bool ffmpeg_ok) {
    std::string folder = ask_line("  folder > ");
    if (folder.empty()) return;
    if (!is_dir(folder)) {
        std::printf("%snot a folder%s\n", NEON_DIM, NEON_RESET);
        return;
    }
    const std::vector<const char*> media = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff",
                                            ".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif",
                                            ".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"};
    std::vector<std::string> files = collect_inputs({folder}, media);
    if (files.empty()) {
        std::printf("%sno media files in folder%s\n", NEON_DIM, NEON_RESET);
        return;
    }
    bool all_imgs = true;
    for (const auto& f : files)
        if (!is_ext(f, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"})) { all_imgs = false; break; }
    Options o;
    o.palette = ask_palette();
    o.glow = ask_float("glow", 1.0f, 0.1f, 3.0f);
    o.threshold = ask_float("edge threshold", 0.12f, 0.02f, 0.5f);
    o.env = ask_float("ambient detail", 1.0f, 0.0f, 2.0f);
    if (!all_imgs) {
        o.spatial = trim(ask_line("  spatial glow (stereo \xe2\x86\x92 direction) [Y/n] > ")) != "n";
        o.neon_audio = trim(ask_line("  neonify the audio too? [y/N] > ")) == "y";
        if (o.neon_audio) {
            o.profile = ask_audio_profile();
            Advanced adv;
            if (trim(ask_line("  advanced audio settings? [y/N] > ")) == "y")
                ask_advanced_audio(o.profile, adv);
            o.advanced = adv;
        }
    }
    StageTracker tracker;
    try {
        auto imgs = collect_inputs({folder}, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"});
        auto vids = collect_inputs({folder}, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"});
        auto auds = collect_inputs({folder}, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"});
        if (!imgs.empty()) flow_image(imgs, o, tracker);
        if (ffmpeg_ok && !vids.empty()) flow_video(vids, o, tracker);
        if (ffmpeg_ok && !auds.empty()) {
            if (o.profile.empty()) o.profile = "slash";
            flow_audio(auds, o, tracker);
        }
    } catch (const std::exception& e) {
        std::printf("%serror: %s%s\n", NEON_PINK, e.what(), NEON_RESET);
    }
}

inline int interactive_mode(bool ffmpeg_ok) {
    print_banner();
    check_env();
    const std::vector<const char*> img = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"};
    const std::vector<const char*> vid = {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"};
    const std::vector<const char*> aud = {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"};
    while (true) {
        std::printf("%swhat are we neonifying?%s\n", NEON_BOLD, NEON_RESET);
        std::printf("  %s1%s) image\n", NEON_BLUE, NEON_RESET);
        std::printf("  %s2%s) video\n", NEON_BLUE, NEON_RESET);
        std::printf("  %s3%s) audio\n", NEON_BLUE, NEON_RESET);
        std::printf("  %s4%s) 3d mesh / relief\n", NEON_BLUE, NEON_RESET);
        std::printf("  %s5%s) batch folder\n", NEON_BLUE, NEON_RESET);
        std::printf("  %sq%s) quit\n", NEON_BLUE, NEON_RESET);
        std::string choice = ask_line(std::string(NEON_PINK) + ">" + NEON_RESET + " ");
        if (choice == "q" || choice == "quit" || choice == "exit") return 0;
        if (choice == "1") interactive_single({"image", img}, ffmpeg_ok);
        else if (choice == "2") interactive_single({"video", vid}, ffmpeg_ok);
        else if (choice == "3") interactive_single({"audio", aud}, ffmpeg_ok);
        else if (choice == "4") {
            std::vector<const char*> mesh = img;
            mesh.push_back(".obj"); mesh.push_back(".ply"); mesh.push_back(".stl");
            interactive_single({"mesh/relief", mesh}, ffmpeg_ok);
        } else if (choice == "5") interactive_batch(ffmpeg_ok);
        else std::printf("%stry 1-5 or q%s\n", NEON_DIM, NEON_RESET);
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
        "  --palette <name>         electric crimson ice toxic violet golden ghost\n"
        "  --glow <0.1-3.0>         glow intensity (default 1)\n"
        "  --threshold <0.02-0.5>   edge sensitivity (default 0.12)\n"
        "  --env <0-2>              ambient detail (default 1)\n"
        "  -o, --output <path>      output path\n"
        "  --next-to-input          save outputs next to the input file instead of results/\n"
        "  --profile <name>         audio profile: fire ice robotic ghost void echo slash\n"
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
    std::vector<std::string> args(argv + 1, argv + argc);
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
        else if (a == "--profile") o.profile = next_str("");
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
        list_profiles();
        return 0;
    }
    if (command == "cli")
        return interactive_mode(which_ok("ffmpeg"));
#ifdef NEONIFY_WITH_GUI
    if ((command.empty() || command == "gui") && inputs.empty())
        return neon_gui::run_gui(argc, argv);
#endif
    if (inputs.empty()) {
        print_banner();
        std::printf("no input given \xe2\x80\x94 entering interactive mode\n\n");
        return interactive_mode(which_ok("ffmpeg"));
    }

    bool ffmpeg_ok = which_ok("ffmpeg");
    print_banner();
    check_env();
    if ((command == "video" || command == "audio") && !ffmpeg_ok) {
        std::printf("ffmpeg required for video/audio. install it, then retry.\n");
        return 1;
    }
    if (command == "video" && o.neon_audio && o.profile.empty()) o.profile = "slash";

    StageTracker tracker(true, json_progress);
    try {
        if (command == "batch") {
            auto imgs = collect_inputs(inputs, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"});
            auto vids = collect_inputs(inputs, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"});
            auto auds = collect_inputs(inputs, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"});
            if (!imgs.empty()) flow_image(imgs, o, tracker);
            if (!vids.empty() && ffmpeg_ok) flow_video(vids, o, tracker);
            if (!auds.empty() && ffmpeg_ok) {
                Options ao = o;
                if (ao.profile.empty()) ao.profile = "slash";
                flow_audio(auds, ao, tracker);
            }
        } else if (command == "image") {
            flow_image(collect_inputs(inputs, {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"}), o, tracker);
        } else if (command == "video") {
            flow_video(collect_inputs(inputs, {".mp4", ".avi", ".mkv", ".mov", ".webm", ".gif"}), o, tracker);
        } else if (command == "audio") {
            flow_audio(collect_inputs(inputs, {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"}), o, tracker);
        } else if (command == "mesh") {
            flow_mesh(collect_inputs(inputs, {".obj", ".ply", ".stl", ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"}), o, tracker);
        } else {
            std::printf("unknown command \xe2\x80\x94 use image / video / audio / mesh / batch / profiles / gui\n");
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
    neon::attach_parent_console(argc, argv);
    return neon::run_cli(argc, argv);
}
