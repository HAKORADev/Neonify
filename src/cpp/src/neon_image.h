// NEONIFY native — image engine: v1 tube law, ported 1:1 from the old python
// engine (the pre-cpp reference): /4 sobel, raw-edge 4-scale glow stack,
// stop-interpolated palette luts, core added in rgb after the lut. laws are laws.
#pragma once

#include "neon_common.h"
#include "neon_hw.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>

// stb_image: public-domain jpg/png/bmp/gif decoder, vendored so the exe reads
// every common format even where the opencv build's own codecs misbehave.
// the implementation expands in the one TU that sets STB_IMAGE_IMPLEMENTATION_HERE
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#ifdef STB_IMAGE_IMPLEMENTATION_HERE
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

namespace neon {

// stb layer: decodes the bytes into a BGR(A) mat; returns an empty mat when
// the format is not one stb speaks
inline cv::Mat stb_decode(const std::vector<uint8_t>& bytes, int want_channels) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h,
                                          &channels, want_channels);
    if (!data) return cv::Mat();
    int type = (want_channels == 4) ? CV_8UC4 : CV_8UC3;
    cv::Mat rgba(h, w, type, data);
    cv::Mat own = rgba.clone();
    stbi_image_free(data);
    if (want_channels == 3) {
        cv::Mat bgr;
        cv::cvtColor(own, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }
    cv::Mat bgra;
    cv::cvtColor(own, bgra, cv::COLOR_RGBA2BGRA);
    // stb fills alpha=255 for formats without one; collapse to 3ch when the
    // art is fully opaque so downstream encoders see a plain BGR image
    cv::Mat alpha;
    cv::extractChannel(bgra, alpha, 3);
    double amin = 255.0;
    cv::minMaxIdx(alpha, &amin, nullptr);
    if (amin >= 255.0) {
        cv::Mat bgr;
        cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    return bgra;
}

// unicode-safe imdecode/imencode: opencv ansi paths fail on windows for non-ascii files
inline cv::Mat imread_robust(const std::string& path, int flags = cv::IMREAD_COLOR) {
    std::vector<uint8_t> bytes = read_file_bytes(path);
    if (bytes.empty()) return cv::Mat();
    // some opencv builds throw instead of returning empty on formats their
    // codec layer rejects — catch so the fallback layers get their turn
    trace("read: bytes ok");
    cv::Mat img;
    try {
        img = cv::imdecode(bytes, flags);
    } catch (const cv::Exception&) {
        img = cv::Mat();
    }
    if (!img.empty()) return img;
    trace("read: cv layer empty");
    // second layer: stb speaks jpg/png/bmp/gif/psd/pic/pnm regardless of how
    // the opencv build turned out
    trace("read: stb enter");
    img = stb_decode(bytes, (flags == cv::IMREAD_UNCHANGED) ? 4 : 3);
    trace("read: stb done");
    if (!img.empty()) return img;
    // third layer: anything ffmpeg decodes but the built-in codecs do not
    std::string cap;
    if (!run_ok({"ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=width,height", "-of", "csv=p=0", path}, &cap) || cap.empty())
        return cv::Mat();
    int w = 0, h = 0;
    if (std::sscanf(cap.c_str(), "%d,%d", &w, &h) != 2 || w <= 0 || h <= 0 ||
        (int64_t)w * h > 4000ll * 4000ll)
        return cv::Mat();
    trace("read: ffmpeg fallback");
    Proc p;
    if (!p.spawn({"ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
                  "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgr24", "-"},
                 true, false))
        return cv::Mat();
    size_t need = size_t(w) * size_t(h) * 3;
    std::vector<uint8_t> raw(need);
    size_t got = 0;
    while (got < need) {
        size_t n = std::fread(raw.data() + got, 1, need - got, p.out);
        if (n == 0) break;
        got += n;
    }
    p.wait_close();
    if (got < need) return cv::Mat();
    cv::Mat out(h, w, CV_8UC3);
    std::memcpy(out.data, raw.data(), need);
    return out;
}

inline bool imwrite_robust(const std::string& path, const cv::Mat& img,
                           const std::vector<int>& params = {}) {
    std::string ext = lower_ext(path);
    if (ext.empty()) ext = ".png";
    std::vector<uint8_t> buf;
    bool encoded = false;
    try {
        encoded = cv::imencode(ext, img, buf, params);
    } catch (const cv::Exception&) {
        encoded = false;
    }
    if (!encoded) {
        // encoder gap on some builds: fall back to png, the name keeps the law
        std::string::size_type dot = path.find_last_of('.');
        std::string png_path = (dot == std::string::npos) ? path + ".png"
                                                          : path.substr(0, dot) + ".png";
        try {
            encoded = cv::imencode(".png", img, buf, params);
        } catch (const cv::Exception&) {
            encoded = false;
        }
        if (!encoded) return false;
        return write_file_bytes(png_path, buf.data(), buf.size());
    }
    return write_file_bytes(path, buf.data(), buf.size());
}

// composite law for the original-look family (the owner's round-7 wording):
//   INSIDE_WIPE  — classic wiped neon, art lands on near-black (palette floor)
//   INSIDE_KEEP  — non-edges stay the original pixels, neon + a local halo sit
//                  on the edges; no global wash over the media
//   INSIDE_GLOW  — the whole original stays under the full global glow field
inline const int INSIDE_WIPE = 0;
inline const int INSIDE_KEEP = 1;
inline const int INSIDE_GLOW = 2;

inline const char* PALETTE_NAMES[] = {"electric", "synthwave", "toxic", "ice", "fire", "ghost", "spectrum"};
inline constexpr float GLOW_SIGMAS[4] = {2.0f, 6.0f, 16.0f, 38.0f};
inline constexpr float GLOW_WEIGHTS[4] = {0.9f, 0.62f, 0.45f, 0.32f};

// gaussian with zero padding — the reference conv2d pads with zeros, replicate
// would brighten frame borders and change the feel
// the ini + bench decide where the math runs; any gpu failure falls back to
// the cpu path in the same call — the output law never changes, only the device
inline bool engine_gpu_ready(int64_t pixels) {
    try {
        return engine_use_gpu(app_ini(), pixels) && cv::ocl::haveOpenCL() && cv::ocl::useOpenCL();
    } catch (const cv::Exception&) {
        return false;
    }
}

inline cv::Mat gaussian_blur(const cv::Mat& img, float sigma) {
    if (sigma <= 0.05f) return img;
    int k = int(std::ceil(sigma * 3.0f)) * 2 + 1;
    cv::Mat out;
    cv::GaussianBlur(img, out, cv::Size(k, k), sigma, sigma, cv::BORDER_CONSTANT);
    return out;
}

// wide_blur law: sigma >= 12 goes through a quarter-scale round trip
// (the temp is not named 'small' — windows.h defines small as a char macro)
inline cv::Mat wide_blur(const cv::Mat& t, float sigma) {
    if (sigma >= 12.0f) {
        cv::Mat ds, out;
        cv::resize(t, ds, cv::Size(), 0.25, 0.25, cv::INTER_LINEAR);
        ds = gaussian_blur(ds, sigma * 0.25f);
        cv::resize(ds, out, t.size(), 0, 0, cv::INTER_LINEAR);
        return out;
    }
    return gaussian_blur(t, sigma);
}

inline cv::UMat wide_blur_gpu(const cv::UMat& t, float sigma) {
    if (sigma >= 12.0f) {
        cv::UMat ds, out;
        cv::resize(t, ds, cv::Size(), 0.25, 0.25, cv::INTER_LINEAR);
        int k = int(std::ceil(sigma * 0.25f * 3.0f)) * 2 + 1;
        cv::GaussianBlur(ds, ds, cv::Size(k, k), sigma * 0.25f, sigma * 0.25f, cv::BORDER_CONSTANT);
        cv::resize(ds, out, t.size(), 0, 0, cv::INTER_LINEAR);
        return out;
    }
    int k = int(std::ceil(sigma * 3.0f)) * 2 + 1;
    cv::UMat out;
    cv::GaussianBlur(t, out, cv::Size(k, k), sigma, sigma, cv::BORDER_CONSTANT);
    return out;
}

// the gpu paths use only the explicit (InputArray, InputArray, OutputArray)
// forms — the umat expression operators are not part of the stable surface
inline cv::UMat const_like(const cv::UMat& x, double v) {
    return cv::UMat(x.size(), x.type(), cv::Scalar::all(v));
}

inline cv::UMat smoothstep_gpu(const cv::UMat& x, float lo, float hi) {
    float denom = std::max(1e-6f, hi - lo);
    cv::UMat t, a, t2, three, two;
    cv::subtract(x, const_like(x, lo), t);
    cv::multiply(t, const_like(t, 1.0 / denom), t);
    cv::min(t, const_like(t, 1.0), a);
    cv::max(a, const_like(a, 0.0), a);
    cv::multiply(a, a, t2);
    cv::multiply(t2, const_like(t2, 3.0), three);
    cv::multiply(t2, a, two);
    cv::multiply(two, const_like(two, 2.0), two);
    cv::subtract(three, two, t);
    return t;
}

inline cv::UMat clamp01_gpu(const cv::UMat& x) {
    cv::UMat a;
    cv::min(x, const_like(x, 1.0), a);
    cv::max(a, const_like(a, 0.0), a);
    return a;
}

inline cv::Mat luminance(const cv::Mat& img) {
    if (img.channels() == 1) return img;
    cv::Mat f, out;
    img.convertTo(f, CV_32F);
    cv::transform(f, out, cv::Matx13f(0.0722f, 0.7152f, 0.2126f));
    return out;
}

inline cv::Mat smoothstep(float lo, float hi, const cv::Mat& x) {
    float denom = std::max(1e-6f, hi - lo);
    cv::Mat t = (x - lo) / denom;
    cv::min(cv::max(t, 0.f), 1.f, t);
    cv::Mat tt2 = t.mul(t);
    cv::Mat out = 3 * tt2 - 2 * tt2.mul(t);
    return out;
}

// palette luts 1:1 with the old python stop tables (rgb, np.interp law)
struct PaletteStop { float p; float r, g, b; };

inline const std::vector<PaletteStop>& palette_stops(const std::string& name) {
    static const std::vector<PaletteStop> electric = {
        {0.00f, 0.004f, 0.010f, 0.045f}, {0.28f, 0.05f, 0.25f, 0.85f},
        {0.58f, 0.15f, 0.72f, 1.00f}, {0.84f, 0.60f, 0.95f, 1.00f},
        {1.00f, 1.00f, 1.00f, 1.00f}};
    static const std::vector<PaletteStop> synthwave = {
        {0.00f, 0.040f, 0.004f, 0.060f}, {0.30f, 0.45f, 0.08f, 0.75f},
        {0.60f, 0.98f, 0.20f, 0.60f}, {0.85f, 1.00f, 0.50f, 0.45f},
        {1.00f, 1.00f, 0.96f, 0.88f}};
    static const std::vector<PaletteStop> toxic = {
        {0.00f, 0.004f, 0.035f, 0.012f}, {0.30f, 0.05f, 0.50f, 0.12f},
        {0.64f, 0.35f, 0.95f, 0.20f}, {1.00f, 0.90f, 1.00f, 0.80f}};
    static const std::vector<PaletteStop> ice = {
        {0.00f, 0.004f, 0.012f, 0.024f}, {0.35f, 0.10f, 0.30f, 0.55f},
        {0.70f, 0.55f, 0.80f, 0.95f}, {1.00f, 1.00f, 1.00f, 1.00f}};
    static const std::vector<PaletteStop> fire = {
        {0.00f, 0.035f, 0.005f, 0.002f}, {0.30f, 0.55f, 0.08f, 0.02f},
        {0.64f, 1.00f, 0.45f, 0.05f}, {0.88f, 1.00f, 0.80f, 0.30f},
        {1.00f, 1.00f, 1.00f, 0.92f}};
    static const std::vector<PaletteStop> ghost = {
        {0.00f, 0.012f, 0.012f, 0.014f}, {0.40f, 0.35f, 0.35f, 0.38f},
        {0.75f, 0.75f, 0.78f, 0.82f}, {1.00f, 1.00f, 1.00f, 1.00f}};
    static const std::vector<PaletteStop> empty;
    if (name == "electric") return electric;
    if (name == "synthwave") return synthwave;
    if (name == "toxic") return toxic;
    if (name == "ice") return ice;
    if (name == "fire") return fire;
    if (name == "ghost") return ghost;
    return empty;
}

inline void hsv_to_rgb_bgr(float h, float s, float v, float& b, float& g, float& r) {
    h = h - std::floor(h);
    float h6 = h * 6.0f;
    int i = int(h6) % 6;
    float f = h6 - std::floor(h6);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float rr, gg, bb;
    switch (i) {
        case 0: rr = v; gg = t; bb = p; break;
        case 1: rr = q; gg = v; bb = p; break;
        case 2: rr = p; gg = v; bb = t; break;
        case 3: rr = p; gg = q; bb = v; break;
        case 4: rr = t; gg = p; bb = v; break;
        default: rr = v; gg = p; bb = q; break;
    }
    b = bb; g = gg; r = rr;
}

inline cv::Mat build_palette_lut(const std::string& name, int n = 256) {
    cv::Mat lut(n, 1, CV_8UC3);
    if (name == "spectrum") {
        for (int i = 0; i < n; i++) {
            float b, g, r;
            hsv_to_rgb_bgr(float(i) / float(n - 1), 0.9f, 1.0f, b, g, r);
            lut.at<cv::Vec3b>(i) = cv::Vec3b(
                uint8_t(b * 255.0f + 0.5f), uint8_t(g * 255.0f + 0.5f), uint8_t(r * 255.0f + 0.5f));
        }
        return lut;
    }
    const std::vector<PaletteStop>& stops = palette_stops(name);
    for (int i = 0; i < n; i++) {
        float t = float(i) / float(n - 1);
        float r = stops.back().r, g = stops.back().g, b = stops.back().b;
        if (t <= stops.front().p) {
            r = stops.front().r; g = stops.front().g; b = stops.front().b;
        } else {
            for (size_t s = 1; s < stops.size(); s++) {
                if (t <= stops[s].p) {
                    float u = (t - stops[s - 1].p) / std::max(1e-6f, stops[s].p - stops[s - 1].p);
                    r = stops[s - 1].r + (stops[s].r - stops[s - 1].r) * u;
                    g = stops[s - 1].g + (stops[s].g - stops[s - 1].g) * u;
                    b = stops[s - 1].b + (stops[s].b - stops[s - 1].b) * u;
                    break;
                }
            }
        }
        lut.at<cv::Vec3b>(i) = cv::Vec3b(
            uint8_t(b * 255.0f + 0.5f), uint8_t(g * 255.0f + 0.5f), uint8_t(r * 255.0f + 0.5f));
    }
    return lut;
}

inline const cv::Mat& palette_lut(const std::string& name) {
    static std::map<std::string, cv::Mat> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(name, build_palette_lut(name)).first;
    return it->second;
}

struct EdgeAux {
    float noise = 0.f;
    float pre_sigma = 0.f;
};

// edge law 1:1 with the reference: luminance, one gaussian (sigma 1), sobel
// normalized by /4 like the torch kernel, smoothstep(threshold, threshold+0.22).
// no noise adaptation, no support mask — the old feel comes from exactly this.
inline void edge_field(const cv::Mat& img_bgr, float glow, float threshold, float env,
                       cv::Mat& edges, EdgeAux& aux, cv::Mat* ang_out = nullptr) {
    (void)glow; (void)env;
    cv::Mat lum = luminance(img_bgr);
    if (engine_gpu_ready(int64_t(lum.total()))) {
        try {
            cv::UMat u = lum.getUMat(cv::ACCESS_READ);
            cv::UMat base, gx, gy, mag, ang;
            cv::GaussianBlur(u, base, cv::Size(7, 7), 1.0, 1.0, cv::BORDER_CONSTANT);
            cv::Sobel(base, gx, CV_32F, 1, 0, 3, 1, 0, cv::BORDER_CONSTANT);
            cv::Sobel(base, gy, CV_32F, 0, 1, 3, 1, 0, cv::BORDER_CONSTANT);
            cv::UMat gx2, gy2;
            cv::multiply(gx, gx, gx2);
            cv::multiply(gy, gy, gy2);
            cv::add(gx2, gy2, mag);
            cv::sqrt(mag, mag);
            cv::multiply(mag, const_like(mag, 1.0 / (4.0 * 255.0)), mag);
            cv::phase(gx, gy, ang, false);
            edges = smoothstep_gpu(mag, threshold, threshold + 0.22f).getMat(cv::ACCESS_READ);
            if (ang_out) *ang_out = ang.getMat(cv::ACCESS_READ);
            aux.noise = 0.f;
            aux.pre_sigma = 0.f;
            return;
        } catch (const cv::Exception&) {
            cv::ocl::setUseOpenCL(false);
        }
    }
    cv::Mat base = gaussian_blur(lum, 1.0f);
    cv::Mat gx, gy;
    cv::Sobel(base, gx, CV_32F, 1, 0, 3, 1, 0, cv::BORDER_CONSTANT);
    cv::Sobel(base, gy, CV_32F, 0, 1, 3, 1, 0, cv::BORDER_CONSTANT);
    cv::Mat mag;
    cv::sqrt(gx.mul(gx) + gy.mul(gy), mag);
    mag = mag / (4.0f * 255.0f);
    if (ang_out) {
        cv::Mat ang;
        cv::phase(gx, gy, ang, false);
        *ang_out = ang;
    }
    edges = smoothstep(threshold, threshold + 0.22f, mag);
    aux.noise = 0.f;
    aux.pre_sigma = 0.f;
}

// glow stack 1:1 with the reference: raw edge blurred at the four sigmas,
// energy = (edge*1.15 + glow*0.85) * excite. the core is not folded in here —
// the reference adds it in rgb after the lut (see process_image_neon).
inline void neon_glow_stack(const cv::Mat& edges, float glow, float env, cv::Mat& energy) {
    float g = std::min(3.0f, std::max(0.1f, glow));
    if (engine_gpu_ready(int64_t(edges.total()))) {
        try {
            cv::UMat e = edges.getUMat(cv::ACCESS_READ);
            cv::UMat glow_field = cv::UMat::zeros(e.size(), CV_32F);
            for (int i = 0; i < 4; i++) {
                cv::UMat b, term;
                b = wide_blur_gpu(e, GLOW_SIGMAS[i]);
                cv::multiply(b, const_like(b, GLOW_WEIGHTS[i] * g * env), term);
                cv::add(glow_field, term, glow_field);
            }
            float excite = 0.72f + 0.42f * env;
            cv::UMat e2, f2, sum;
            cv::multiply(e, const_like(e, 1.15f * excite), e2);
            cv::multiply(glow_field, const_like(glow_field, 0.85f * excite), f2);
            cv::add(e2, f2, sum);
            energy = clamp01_gpu(sum).getMat(cv::ACCESS_READ);
            return;
        } catch (const cv::Exception&) {
            cv::ocl::setUseOpenCL(false);
        }
    }
    cv::Mat glow_field = cv::Mat::zeros(edges.size(), CV_32F);
    for (int i = 0; i < 4; i++)
        glow_field += wide_blur(edges, GLOW_SIGMAS[i]) * (GLOW_WEIGHTS[i] * g * env);
    float excite = 0.72f + 0.42f * env;
    energy = (edges * 1.15 + glow_field * 0.85) * excite;
    cv::min(cv::max(energy, 0.f), 1.f, energy);
}

// rgb-space core law: pow(edge, 1.2) * 0.85 * 0.55, added after colorizing —
// three channels so it lands on the bgr art directly
inline cv::Mat neon_core_u8(const cv::Mat& edges) {
    cv::Mat core;
    cv::pow(edges, 1.2, core);
    core = core * (0.85f * 0.55f * 255.0f);
    cv::min(cv::max(core, 0.f), 255.f, core);
    cv::Mat core8;
    core.convertTo(core8, CV_8U);
    cv::Mat chs[3] = {core8, core8, core8}, out;
    cv::merge(chs, 3, out);
    return out;
}

inline cv::Mat colorize(const cv::Mat& field, const std::string& palette) {
    const cv::Mat& lut = palette_lut(palette);
    cv::Mat x;
    if (field.channels() == 1) x = field;
    else {
        std::vector<cv::Mat> ch;
        cv::split(field, ch);
        x = (ch[0] + ch[1] + ch[2]) / 3.0f;
    }
    cv::min(cv::max(x, 0.f), 1.f, x);
    cv::Mat idx8;
    x.convertTo(idx8, CV_8U, 255.0);
    cv::Mat idx3, col, out;
    cv::Mat idx_chs[3] = {idx8, idx8, idx8};
    cv::merge(idx_chs, 3, idx3);
    cv::LUT(idx3, lut, col);
    return col;
}

// the rainbow law 1:1 with the reference: hue from the edge angle — the
// reference computes ((atan2 + pi) / 2pi) mod 1, and cv::phase already hands
// back atan2 wrapped into [0, 2pi), so the shift is +0.5 of a turn — value
// from the raw energy (no 0.15 floor — energy 0 stays black), then the core add
inline cv::Mat spectrum_colorize(const cv::Mat& field, const cv::Mat& ang) {
    cv::Mat hue = ang / (2.0f * float(M_PI));
    hue += 0.5f;
    int rows = field.rows, cols = field.cols;
    cv::Mat out(rows, cols, CV_8UC3);
    for (int y = 0; y < rows; y++) {
        const float* fr = field.ptr<float>(y);
        const float* ar = hue.ptr<float>(y);
        uint8_t* orow = out.ptr<uint8_t>(y);
        for (int x = 0; x < cols; x++) {
            float e = std::min(1.0f, std::max(0.0f, fr[x]));
            float b, g, r;
            hsv_to_rgb_bgr(ar[x], 0.9f, e, b, g, r);
            orow[x * 3 + 0] = uint8_t(b * 255.0f + 0.5f);
            orow[x * 3 + 1] = uint8_t(g * 255.0f + 0.5f);
            orow[x * 3 + 2] = uint8_t(r * 255.0f + 0.5f);
        }
    }
    return out;
}

inline void process_image_neon(const cv::Mat& img_bgr, const std::string& palette,
                               float glow, float threshold, float env,
                               cv::Mat& out, EdgeAux& aux, cv::Mat* field_out = nullptr,
                               cv::Mat* edges_out = nullptr) {
    cv::Mat edges, ang;
    edge_field(img_bgr, glow, threshold, env, edges, aux, &ang);
    cv::Mat field;
    neon_glow_stack(edges, glow, env, field);
    out = (palette == "spectrum") ? spectrum_colorize(field, ang) : colorize(field, palette);
    cv::Mat core = neon_core_u8(edges);
    cv::add(out, core, out);
    if (field_out) *field_out = field;
    if (edges_out) *edges_out = edges;
}

// keep law: the original pixels stay wherever the mask is dark; the neon art
// takes over where the edges (plus a tight local halo) live. no global wash.
inline cv::Mat keep_mask(const cv::Mat& edges) {
    cv::Mat halo = wide_blur(edges, 6.0f) * 0.55f;
    cv::Mat m = edges * 1.15f + halo;
    cv::min(cv::max(m, 0.f), 1.f, m);
    cv::Mat soft;
    cv::GaussianBlur(m, soft, cv::Size(0, 0), 0.8, 0.8, cv::BORDER_REPLICATE);
    return soft;
}

inline void keep_inside_composite(cv::Mat& neon_out, const cv::Mat& orig_bgr,
                                  const cv::Mat& edges, const cv::Mat& field, int mode) {
    if (neon_out.empty() || orig_bgr.empty() || edges.empty()) return;
    if (mode == INSIDE_WIPE) return;
    cv::Mat m;
    if (mode == INSIDE_GLOW) {
        if (field.empty()) return;
        cv::max(field, edges * 0.85f, m);
        cv::min(cv::max(m, 0.f), 1.f, m);
        cv::GaussianBlur(m, m, cv::Size(0, 0), 0.8, 0.8, cv::BORDER_REPLICATE);
    } else {
        m = keep_mask(edges);
    }
    cv::Mat m3;
    cv::Mat chs[3] = {m, m, m};
    cv::merge(chs, 3, m3);
    cv::Mat orig32, out32;
    orig_bgr.convertTo(orig32, CV_32F);
    neon_out.convertTo(out32, CV_32F);
    cv::Mat mixed = orig32.mul(1.0 - m3) + out32.mul(m3);
    mixed.convertTo(neon_out, CV_8U);
}

inline std::string neonize_image_file(const std::string& inp, const std::string& out_path,
                                      const std::string& palette, float glow,
                                      float threshold, float env, StageTracker* tracker,
                                      EdgeAux* aux_out = nullptr, int inside_mode = INSIDE_WIPE) {
    cv::Mat img = imread_robust(inp, cv::IMREAD_UNCHANGED);
    if (img.empty()) img = imread_robust(inp, cv::IMREAD_COLOR);
    if (img.empty()) throw std::runtime_error("cannot read image: " + inp);
    if (img.depth() != CV_8U) {
        cv::Mat c8;
        double mn, mx;
        cv::minMaxIdx(img, &mn, &mx);
        double scale = (mx > 255.0) ? 255.0 / mx : 1.0;
        img.convertTo(c8, CV_8U, scale);
        img = c8;
    }
    cv::Mat alpha8;
    cv::Mat bgr;
    if (img.channels() == 4) {
        std::vector<cv::Mat> chs;
        cv::split(img, chs);
        alpha8 = chs[3].clone();
        std::vector<cv::Mat> bgr3 = {chs[0], chs[1], chs[2]};
        cv::merge(bgr3, bgr);
    } else if (img.channels() == 1) {
        cv::cvtColor(img, bgr, cv::COLOR_GRAY2BGR);
    } else {
        bgr = img;
    }
    trace("engine: start");
    if (tracker) {
        tracker->set_stages({"edges", "bloom"});
        tracker->begin_stage(0);
    }
    trace("engine: neon pass");
    EdgeAux aux;
    cv::Mat out, field, edges;
    process_image_neon(bgr, palette, glow, threshold, env, out, aux, &field, &edges);
    trace("engine: neon done");
    if (std::getenv("NEONIFY_DEBUG_DUMP")) {
        cv::Mat e16, f16;
        edges.convertTo(e16, CV_16U, 65535.0);
        field.convertTo(f16, CV_16U, 65535.0);
        cv::imwrite("/tmp/neonify_debug_edges.png", e16);
        cv::imwrite("/tmp/neonify_debug_field.png", f16);
        cv::imwrite("/tmp/neonify_debug_out.png", out);
    }
    if (tracker) tracker->begin_stage(1);
    if (inside_mode != INSIDE_WIPE) keep_inside_composite(out, bgr, edges, field, inside_mode);
    if (!alpha8.empty()) {
        // alpha law: transparency comes only from the source art. wiped pixels
        // stay (dark), transparent pixels stay transparent — nothing else.
        std::vector<cv::Mat> och, bch;
        cv::split(out, bch);
        och = {bch[0], bch[1], bch[2], alpha8};
        cv::merge(och, out);
    }
    if (tracker) tracker->step(0.9f);
    trace("engine: write");
    std::string finalp = unique_output_path(out_path);
    std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 6};
    if (!imwrite_robust(finalp, out, params))
        throw std::runtime_error("cannot write image: " + finalp);
    if (aux_out) *aux_out = aux;
    return finalp;
}

}  // namespace neon
