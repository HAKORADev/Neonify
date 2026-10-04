// NEONIFY native — hardware truth. dynamic first-run detection: opencl probe
// and a real cpu/gpu benchmark of the neon ops, ffmpeg hardware encoder probe,
// desktop capture backend. everything lands in the ini as detected — no
// invented numbers, no claimed devices.
#pragma once

#include "neon_common.h"
#include "neon_ini.h"
#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>

namespace neon {

struct HardwareInfo {
    std::string gpu_name = "none";
    std::string gpu_backend = "none";   // opencl (engine), dxgi (desktop overlay), none
    bool opencl = false;
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
    std::string video_encoder = "libx264";
    std::string desktop_backend = "none";  // dxgi | x11 | portal | none
};

// the whole ini schema, shared by every binary so the layout stays identical
inline std::vector<IniField> ini_schema() {
    return {
        {"hardware", "gpu_name", "the detected graphics device, 'none' when every run is on the cpu", "text", "none"},
        {"hardware", "gpu_backend", "what the gpu is actually used through", "none|opencl|dxgi", "none"},
        {"hardware", "opencl", "opencl was detected and a real gpu op succeeded", "0|1", "0"},
        {"hardware", "cpu_ms", "bench: neon edge+glow ops on a 1080p frame, cpu, milliseconds", "0-100000", "0"},
        {"hardware", "gpu_ms", "bench: same ops on the gpu, milliseconds (0 when no gpu)", "0-100000", "0"},
        {"hardware", "video_encoder", "ffmpeg video encoder verified on this machine", "auto|libx264|h264_nvenc|h264_qsv|h264_amf|h264_vaapi", "auto"},
        {"hardware", "desktop_backend", "how the desktop neonifier captures the screen here", "none|dxgi|x11|portal", "none"},

        {"engine", "device", "where the neon math runs for images and videos", "auto|gpu|cpu", "auto"},
        {"engine", "gpu_min_pixels", "frames smaller than this stay on the cpu even in gpu mode", "int 0-16777216", "307200"},

        {"video", "hwdecode", "let ffmpeg decode with the gpu when the encoder probe says it can", "0|1", "0"},

        {"desktop", "hotkeys", "ctrl+alt+n toggle and ctrl+alt+n+w focused window are active", "0|1", "1"},
        {"desktop", "mode", "what gets neonified", "desktop|window", "desktop"},
        {"desktop", "monitor", "which monitor when mode is desktop (primary or 1-6)", "primary|1|2|3|4|5|6", "primary"},
        {"desktop", "fps", "overlay refresh cap, frames per second", "int 5-60", "30"},
        {"desktop", "scale", "process at this fraction of the capture resolution (lower is faster)", "0.25-1", "1"},
        {"desktop", "device", "where the overlay math runs", "auto|gpu|cpu", "auto"},
        {"desktop", "palette", "neon palette for the overlay", "electric|synthwave|toxic|ice|fire|ghost|spectrum", "electric"},
        {"desktop", "glow", "glow intensity multiplier", "0.2-3", "1"},
        {"desktop", "threshold", "edge sensitivity", "0.02-0.5", "0.12"},
        {"desktop", "env", "ambient detail", "0-2", "1"},
        {"desktop", "inside", "how the original frame sits under the effect", "wipe|keep|glow", "wipe"},
    };
}

// real bench of the engine's hot ops (sobel + wide gaussian stack) on one
// 1080p frame; returns milliseconds, doubles are wall time, not guesses
inline double bench_device_ms(bool gpu) {
    cv::Mat src(1080, 1920, CV_32F);
    cv::randu(src, 0.f, 255.f);
    double best = 1e9;
    for (int rep = 0; rep < 3; rep++) {
        auto t0 = std::chrono::steady_clock::now();
        if (gpu) {
            cv::UMat u = src.getUMat(cv::ACCESS_READ);
            cv::UMat gx, gy, mag, blur;
            cv::Sobel(u, gx, CV_32F, 1, 0, 3);
            cv::Sobel(u, gy, CV_32F, 0, 1, 3);
            cv::multiply(gx, gx, gx);
            cv::multiply(gy, gy, gy);
            cv::add(gx, gy, mag);
            cv::sqrt(mag, mag);
            for (float sigma : {2.0f, 6.0f, 16.0f})
                cv::GaussianBlur(mag, blur, cv::Size(0, 0), sigma, sigma);
            mag.getMat(cv::ACCESS_READ);
        } else {
            cv::Mat gx, gy, mag, blur;
            cv::Sobel(src, gx, CV_32F, 1, 0, 3);
            cv::Sobel(src, gy, CV_32F, 0, 1, 3);
            cv::multiply(gx, gx, gx);
            cv::multiply(gy, gy, gy);
            cv::add(gx, gy, mag);
            cv::sqrt(mag, mag);
            for (float sigma : {2.0f, 6.0f, 16.0f})
                cv::GaussianBlur(mag, blur, cv::Size(0, 0), sigma, sigma);
        }
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, ms);
    }
    return best;
}

// opencl is real only when a device answers and a gpu op actually completes
inline bool probe_opencl(std::string& name_out) {
    try {
        if (!cv::ocl::haveOpenCL()) return false;
        cv::ocl::setUseOpenCL(true);
        if (!cv::ocl::useOpenCL()) return false;
        cv::UMat u(64, 64, CV_32F, cv::Scalar::all(1.f));
        cv::UMat out;
        cv::GaussianBlur(u, out, cv::Size(5, 5), 1.0, 1.0);
        cv::Mat check = out.getMat(cv::ACCESS_READ);
        if (check.empty()) return false;
        const std::vector<cv::ocl::PlatformInfo>& platforms = [] {
            static std::vector<cv::ocl::PlatformInfo> list;
            cv::ocl::getPlatfomsInfo(list);
            return list;
        }();
        for (const auto& p : platforms) {
            for (int d = 0; d < p.deviceNumber(); d++) {
                cv::ocl::Device dev;
                p.getDevice(dev, d);
                if (dev.type() == cv::ocl::Device::TYPE_GPU || dev.type() == cv::ocl::Device::TYPE_ACCELERATOR) {
                    name_out = dev.name();
                    return true;
                }
            }
        }
        if (!platforms.empty() && platforms[0].deviceNumber() > 0) {
            cv::ocl::Device dev;
            platforms[0].getDevice(dev, 0);
            name_out = dev.name();
            return true;
        }
        return false;
    } catch (const cv::Exception&) {
        return false;
    }
}

// every candidate must actually encode two frames — a name in -encoders is
// not proof the driver works
inline std::string probe_video_encoder() {
    if (!which_ok("ffmpeg")) return "libx264";
    static const struct { const char* name; const char* arg; const char* val; } candidates[] = {
        {"h264_nvenc", "-cq", "19"},
        {"h264_qsv", "-global_quality", "20"},
        {"h264_amf", "-quality", "balanced"},
        {"h264_vaapi", "-qp", "22"},
    };
    for (const auto& c : candidates) {
        std::vector<std::string> cmd;
        if (std::string(c.name) == "h264_vaapi") {
            cmd = {"ffmpeg", "-hide_banner", "-loglevel", "error",
                   "-vaapi_device", "/dev/dri/renderD128",
                   "-f", "lavfi", "-i", "color=c=black:s=256x256:r=25:d=0.08",
                   "-frames:v", "2", "-vf", "format=nv12,hwupload"};
        } else {
            cmd = {"ffmpeg", "-hide_banner", "-loglevel", "error",
                   "-f", "lavfi", "-i", "color=c=black:s=256x256:r=25:d=0.08",
                   "-frames:v", "2"};
        }
        cmd.push_back("-c:v");
        cmd.push_back(c.name);
        cmd.push_back(c.arg);
        cmd.push_back(c.val);
        cmd.push_back("-f");
        cmd.push_back("null");
        cmd.push_back("-");
        if (run_ok(cmd, nullptr, true)) return c.name;
    }
    return "libx264";
}

inline std::string detect_desktop_backend() {
#ifdef _WIN32
    return "dxgi";
#elif defined(__linux__)
    if (std::getenv("DISPLAY")) return "x11";
    const char* sess = std::getenv("XDG_SESSION_TYPE");
    if (std::getenv("WAYLAND_DISPLAY") || (sess && std::string(sess) == "wayland"))
        return "portal";
    return "none";
#else
    return "none";
#endif
}

inline HardwareInfo detect_hardware() {
    HardwareInfo hw;
    std::string cl_name;
    trace("detect: opencl probe");
    hw.opencl = probe_opencl(cl_name);
    trace("detect: cpu bench");
    hw.cpu_ms = bench_device_ms(false);
    trace("detect: opencl bench");
    if (hw.opencl) {
        hw.gpu_ms = bench_device_ms(true);
        if (hw.gpu_ms > 0 && hw.gpu_ms < hw.cpu_ms) {
            hw.gpu_name = cl_name;
            hw.gpu_backend = "opencl";
        } else {
            hw.opencl = false;
            hw.gpu_ms = 0.0;
        }
    }
    trace("detect: encoder probe");
    hw.video_encoder = probe_video_encoder();
    trace("detect: desktop backend");
    hw.desktop_backend = detect_desktop_backend();
    trace("detect: done");
    return hw;
}

// loads (or freshly creates) the shared ini with this machine's real facts
inline IniFile load_app_ini() {
    IniFile ini(ini_path());
    std::vector<IniField> schema = ini_schema();
    auto detect = [&]() {
        HardwareInfo hw = detect_hardware();
        ini.set("hardware", "gpu_name", hw.gpu_name);
        ini.set("hardware", "gpu_backend", hw.gpu_backend);
        ini.set("hardware", "opencl", hw.opencl ? "1" : "0");
        ini.set("hardware", "cpu_ms", std::to_string(int(hw.cpu_ms * 10.0 + 0.5) / 10.0));
        ini.set("hardware", "gpu_ms", std::to_string(int(hw.gpu_ms * 10.0 + 0.5) / 10.0));
        // the probe result is stored by name so later runs do not re-probe;
        // 'auto' stays a valid user choice that forces a fresh probe
        ini.set("hardware", "video_encoder", hw.video_encoder);
        ini.set("hardware", "desktop_backend", hw.desktop_backend);
    };
    bool created = false;
    ini.load(schema, &created);
    trace(created ? "ini: fresh — detecting hardware" : "ini: existing — keeping values");
    if (created) {
        try {
            detect();
        } catch (const std::exception& e) {
            // detection must never take the app down with it — the stored
            // facts stay honest defaults and the user can delete the ini to
            // retry fresh
            trace("ini: detection failed");
            std::fprintf(stderr, "[ini] hardware detection failed: %s\n", e.what());
        } catch (...) {
            trace("ini: detection failed");
        }
        trace("ini: detection done — saving");
        ini.save(schema);  // the detection must survive this process
    }
    return ini;
}

// resolved engine device law: auto picks the faster device from the bench,
// gpu only above the pixel floor, cpu always honest
inline bool engine_use_gpu(const IniFile& ini, int64_t pixels) {
    std::string dev = ini.get("engine", "device", "auto");
    if (dev == "cpu") return false;
    if (dev == "gpu") {
        if (ini.get_bool("hardware", "opencl", false)) return true;
        return false;
    }
    if (!ini.get_bool("hardware", "opencl", false)) return false;
    if (pixels < ini.get_int("engine", "gpu_min_pixels", 307200)) return false;
    double cpu_ms = ini.get_float("hardware", "cpu_ms", 0.f);
    double gpu_ms = ini.get_float("hardware", "gpu_ms", 0.f);
    if (gpu_ms <= 0.f) return false;
    return gpu_ms < cpu_ms;
}

// the process-wide ini: loads (and repairs) once, then serves values
inline IniFile& app_ini() {
    static IniFile ini = load_app_ini();
    return ini;
}

// resolved video encoder: auto re-probes (machines change), a chosen name is
// used as-is — the probe validated it when the ini was born
inline std::string resolved_video_encoder(const IniFile& ini) {
    std::string pick = ini.get("hardware", "video_encoder", "auto");
    if (pick == "auto") return probe_video_encoder();
    return pick;
}

// per-encoder ffmpeg output args (quality matched roughly to crf 18 x264)
inline void video_encoder_args(const std::string& enc, std::vector<std::string>& cmd) {
    if (enc == "h264_nvenc") {
        cmd.push_back("-c:v"); cmd.push_back(enc);
        cmd.push_back("-preset"); cmd.push_back("p4");
        cmd.push_back("-rc"); cmd.push_back("vbr");
        cmd.push_back("-cq"); cmd.push_back("19");
        cmd.push_back("-b:v"); cmd.push_back("0");
    } else if (enc == "h264_qsv") {
        cmd.push_back("-c:v"); cmd.push_back(enc);
        cmd.push_back("-global_quality"); cmd.push_back("20");
        cmd.push_back("-preset"); cmd.push_back("medium");
    } else if (enc == "h264_amf") {
        cmd.push_back("-c:v"); cmd.push_back(enc);
        cmd.push_back("-quality"); cmd.push_back("balanced");
        cmd.push_back("-rc"); cmd.push_back("vbr_peak");
        cmd.push_back("-b:v"); cmd.push_back("6M");
    } else if (enc == "h264_vaapi") {
        cmd.push_back("-vaapi_device"); cmd.push_back("/dev/dri/renderD128");
        cmd.push_back("-vf"); cmd.push_back("format=nv12,hwupload");
        cmd.push_back("-c:v"); cmd.push_back(enc);
        cmd.push_back("-qp"); cmd.push_back("22");
    } else {
        cmd.push_back("-c:v"); cmd.push_back("libx264");
        cmd.push_back("-preset"); cmd.push_back("medium");
        cmd.push_back("-crf"); cmd.push_back("18");
    }
}

}  // namespace neon
