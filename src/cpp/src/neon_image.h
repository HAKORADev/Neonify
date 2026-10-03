// NEONIFY native — image engine: v1 tube law (sobel ridge + 4-scale glow stack)
// ported 1:1 from the python reference engine. laws are laws.
#pragma once

#include "neon_common.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <random>

// stb_image: public-domain jpg/png/bmp/gif decoder, vendored so the exe reads
// every common format even where the opencv build's own codecs misbehave
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
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
    std::vector<int> chs;
    cv::split(bgra, chs);
    double amin = 255.0;
    cv::minMaxIdx(chs[3], &amin, nullptr);
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
    cv::Mat img = cv::imdecode(bytes, flags);
    if (!img.empty()) return img;
    // second layer: stb speaks jpg/png/bmp/gif/psd/pic/pnm regardless of how
    // the opencv build turned out
    img = stb_decode(bytes, (flags == cv::IMREAD_UNCHANGED) ? 4 : 3);
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
    if (!cv::imencode(ext, img, buf, params)) {
        // encoder gap on some builds: fall back to png, the name keeps the law
        std::string::size_type dot = path.find_last_of('.');
        std::string png_path = (dot == std::string::npos) ? path + ".png"
                                                          : path.substr(0, dot) + ".png";
        if (!cv::imencode(".png", img, buf, params)) return false;
        return write_file_bytes(png_path, buf.data(), buf.size());
    }
    return write_file_bytes(path, buf.data(), buf.size());
}

inline const char* PALETTE_NAMES[] = {"electric", "crimson", "ice", "toxic", "violet", "golden", "ghost", "spectrum"};
inline constexpr float GLOW_SIGMAS[4] = {2.0f, 6.0f, 16.0f, 38.0f};
inline constexpr float GLOW_WEIGHTS[4] = {0.9f, 0.62f, 0.45f, 0.32f};

inline cv::Mat gaussian_blur(const cv::Mat& img, float sigma) {
    if (sigma <= 0.05f) return img;
    int k = int(std::ceil(sigma * 3.0f)) * 2 + 1;
    cv::Mat out;
    cv::GaussianBlur(img, out, cv::Size(k, k), sigma, sigma, cv::BORDER_REPLICATE);
    return out;
}

inline cv::Mat luminance(const cv::Mat& img) {
    if (img.channels() == 1) return img;
    cv::Mat f, out;
    img.convertTo(f, CV_32F);
    cv::transform(f, out, cv::Matx13f(0.114f, 0.587f, 0.299f));
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

inline cv::Mat build_palette_lut(const std::string& name, int n = 256) {
    cv::Mat lut(n, 1, CV_32FC3);
    for (int i = 0; i < n; i++) {
        float t = float(i) / float(n - 1);
        float r, g, b;
        if (name == "crimson") {
            r = 0.15f + 0.85f * t;
            g = 0.02f * std::pow(t, 2.2f);
            b = 0.08f + 0.25f * std::pow(t, 3.0f) * (1 - t) * 2;
        } else if (name == "ice") {
            r = 0.45f * std::pow(t, 2.5f);
            g = 0.55f + 0.45f * std::pow(t, 1.6f);
            b = 0.65f + 0.35f * t;
        } else if (name == "toxic") {
            r = 0.45f * std::pow(t, 3.2f);
            g = 0.25f + 0.75f * t;
            b = 0.06f * std::pow(t, 1.4f);
        } else if (name == "violet") {
            r = 0.35f + 0.65f * std::pow(t, 1.3f);
            g = 0.04f + 0.22f * std::pow(t, 2.8f);
            b = 0.55f + 0.45f * t;
        } else if (name == "golden") {
            r = 0.55f + 0.45f * t;
            g = 0.32f * std::pow(t, 0.7f) + 0.12f * std::pow(t, 2.0f);
            b = 0.02f * std::pow(t, 2.4f);
        } else if (name == "ghost") {
            r = 0.62f + 0.38f * std::pow(t, 1.8f);
            g = 0.68f + 0.32f * std::pow(t, 1.4f);
            b = 0.78f + 0.22f * t;
        } else if (name == "spectrum") {
            float h = t * 6.0f;
            int i = int(std::fmod(h, 6.0f));
            float f = h - std::floor(h);
            float p = 0.1f, q = 0.1f + 0.9f * (1.0f - f), tt = 0.1f + 0.9f * f;
            float rr, gg, bb;
            switch (i) {
                case 0: rr = 1.0f; gg = tt; bb = p; break;
                case 1: rr = q; gg = 1.0f; bb = p; break;
                case 2: rr = p; gg = 1.0f; bb = tt; break;
                case 3: rr = p; gg = q; bb = 1.0f; break;
                case 4: rr = tt; gg = p; bb = 1.0f; break;
                default: rr = 1.0f; gg = p; bb = q; break;
            }
            r = rr; g = gg; b = bb;
        } else {
            r = 0.85f * std::pow(t, 3.4f);
            g = 0.25f + 0.75f * std::pow(t, 1.25f);
            b = 0.10f + 0.90f * std::pow(t, 0.55f);
        }
        lut.at<cv::Vec3f>(i) = cv::Vec3f(b, g, r);
    }
    cv::Mat out;
    lut.convertTo(out, CV_8UC3, 255.0);
    return out;
}

inline const cv::Mat& palette_lut(const std::string& name) {
    static std::map<std::string, cv::Mat> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(name, build_palette_lut(name)).first;
    return it->second;
}

inline float noise_estimate(const cv::Mat& lum) {
    cv::Mat lap;
    cv::Laplacian(lum, lap, CV_32F, 3);
    std::vector<float> v;
    v.reserve((size_t)lap.total());
    for (int y = 0; y < lap.rows; y++) {
        const float* row = lap.ptr<float>(y);
        for (int x = 0; x < lap.cols; x++) v.push_back(std::fabs(row[x]));
    }
    if (v.empty()) return 0.f;
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid] / 0.6745f;
}

inline cv::Mat support_mask(const cv::Mat& lum, float radius) {
    cv::Mat g = gaussian_blur(lum, radius);
    cv::Mat gx, gy;
    cv::Sobel(g, gx, CV_32F, 1, 0, 3);
    cv::Sobel(g, gy, CV_32F, 0, 1, 3);
    cv::Mat grad;
    cv::sqrt(gx.mul(gx) + gy.mul(gy), grad);
    int k = int(radius * 2) * 2 + 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k));
    cv::Mat support;
    cv::dilate(grad, support, kernel);
    std::vector<float> v;
    v.reserve((size_t)support.total());
    for (int y = 0; y < support.rows; y++) {
        const float* row = support.ptr<float>(y);
        for (int x = 0; x < support.cols; x++) v.push_back(row[x]);
    }
    size_t idx = size_t(v.size() * 0.92);
    std::nth_element(v.begin(), v.begin() + std::min(idx, v.size() - 1), v.end());
    float hi = v[std::min(idx, v.size() - 1)];
    if (hi < 1e-5f) return cv::Mat::zeros(support.size(), CV_32F);
    return smoothstep(hi * 0.12f, hi * 0.55f, support);
}

inline cv::Mat sobel_mag(const cv::Mat& lum) {
    cv::Mat gx, gy;
    cv::Sobel(lum, gx, CV_32F, 1, 0, 3);
    cv::Sobel(lum, gy, CV_32F, 0, 1, 3);
    cv::Mat out;
    cv::sqrt(gx.mul(gx) + gy.mul(gy), out);
    return out / 255.0f;
}

struct EdgeAux {
    float noise = 0.f;
    float pre_sigma = 0.f;
};

inline void edge_field(const cv::Mat& img_bgr, float glow, float threshold, float env,
                       cv::Mat& edges, EdgeAux& aux, cv::Mat* ang_out = nullptr) {
    (void)glow; (void)env;
    cv::Mat lum = luminance(img_bgr);
    float noise = noise_estimate(lum);
    float pre_sigma = std::min(2.4f, std::max(0.0f, (noise - 2.0f) * 0.16f));
    cv::Mat work = (pre_sigma > 0.05f) ? gaussian_blur(lum, pre_sigma) : lum;
    cv::Mat base = gaussian_blur(work, 1.0f);
    cv::Mat gx, gy;
    cv::Sobel(base, gx, CV_32F, 1, 0, 3);
    cv::Sobel(base, gy, CV_32F, 0, 1, 3);
    cv::Mat mag;
    cv::sqrt(gx.mul(gx) + gy.mul(gy), mag);
    mag = mag / 255.0f;
    if (ang_out) {
        cv::Mat ang;
        cv::phase(gx, gy, ang, false);
        *ang_out = ang;
    }
    float thr = threshold * (1.0f + pre_sigma * 0.38f);
    cv::Mat edge = smoothstep(thr, thr + 0.22f, mag);
    cv::Mat sup = support_mask(work, 1.0f + pre_sigma);
    edge = edge.mul(smoothstep(0.04f, 0.30f, sup));
    edges = edge;
    aux.noise = noise;
    aux.pre_sigma = pre_sigma;
}

inline float edge_density(const cv::Mat& edges) {
    int64_t cnt = 0;
    for (int y = 0; y < edges.rows; y++) {
        const float* row = edges.ptr<float>(y);
        for (int x = 0; x < edges.cols; x++)
            if (row[x] > 0.35f) cnt++;
    }
    return float(cnt) / float(std::max<int64_t>(1, (int64_t)edges.total()));
}

inline void neon_glow_stack(const cv::Mat& img_bgr, const cv::Mat& edges,
                            float glow, float env, float threshold,
                            cv::Mat& energy) {
    (void)img_bgr; (void)threshold;
    float g = std::min(3.0f, std::max(0.1f, glow));
    float density = edge_density(edges);
    float calm = 1.0f / (1.0f + 3.5f * std::max(0.0f, density - 0.18f));
    cv::Mat hot;
    cv::min(edges * 1.25, 1.0, hot);
    cv::Mat src = edges.mul(1.0 - 0.55 * hot);
    cv::Mat glow_field = cv::Mat::zeros(edges.size(), CV_32F);
    for (int i = 0; i < 4; i++) {
        cv::Mat b = gaussian_blur(src, GLOW_SIGMAS[i]);
        glow_field += b * (GLOW_WEIGHTS[i] * g * env * calm);
    }
    float excite = 0.72f + 0.42f * env;
    energy = (edges * 1.15 + glow_field * 0.85) * excite;
    cv::min(cv::max(energy, 0.f), 1.f, energy);
    cv::Mat core;
    cv::pow(edges, 1.2, core);
    energy += core * (0.85 * 0.55);
    cv::min(cv::max(energy, 0.f), 1.f, energy);
}

inline cv::Mat colorize(const cv::Mat& field, const std::string& palette, float lift = 0.0f) {
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
    col.convertTo(out, CV_32F);
    if (lift > 0) out += lift * 255.0f * 0.045f;
    cv::Mat out8;
    out.convertTo(out8, CV_8U, 1.0);
    return out8;
}

// the rainbow law: edge direction paints the hue, energy paints the value
inline cv::Mat spectrum_colorize(const cv::Mat& field, const cv::Mat& ang) {
    cv::Mat hue = ang / (2.0f * float(M_PI));
    int rows = field.rows, cols = field.cols;
    cv::Mat out(rows, cols, CV_8UC3);
    for (int y = 0; y < rows; y++) {
        const float* fr = field.ptr<float>(y);
        const float* ar = hue.ptr<float>(y);
        uint8_t* orow = out.ptr<uint8_t>(y);
        for (int x = 0; x < cols; x++) {
            float e = std::min(1.0f, std::max(0.0f, fr[x]));
            float h = ar[x] - std::floor(ar[x]);
            float v = 0.15f + 0.85f * e;
            float s = 0.9f;
            float hf = h * 6.0f;
            int i = int(hf) % 6;
            float f = hf - std::floor(hf);
            float p = v * (1.0f - s);
            float q = v * (1.0f - s * f);
            float t = v * (1.0f - s * (1.0f - f));
            float r, g, b;
            switch (i) {
                case 0: r = v; g = t; b = p; break;
                case 1: r = q; g = v; b = p; break;
                case 2: r = p; g = v; b = t; break;
                case 3: r = p; g = q; b = v; break;
                case 4: r = t; g = p; b = v; break;
                default: r = v; g = p; b = q; break;
            }
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
    neon_glow_stack(img_bgr, edges, glow, env, threshold, field);
    out = (palette == "spectrum") ? spectrum_colorize(field, ang) : colorize(field, palette);
    if (field_out) *field_out = field;
    if (edges_out) *edges_out = edges;
}

// keep-inside law (owner's round-6 wording): nothing is wiped. the whole
// original look stays under the effect; wherever the glow is strong the neon
// takes over, everywhere else the original pixels survive untouched
inline void keep_inside_composite(cv::Mat& neon_out, const cv::Mat& orig_bgr,
                                  const cv::Mat& edges, const cv::Mat& field) {
    if (neon_out.empty() || orig_bgr.empty() || edges.empty() || field.empty()) return;
    cv::Mat m;
    cv::max(field, edges * 0.85, m);
    cv::min(cv::max(m, 0.f), 1.f, m);
    cv::Mat soft;
    cv::GaussianBlur(m, soft, cv::Size(0, 0), 0.8, 0.8, cv::BORDER_REPLICATE);
    cv::Mat m3;
    cv::Mat chs[3] = {soft, soft, soft};
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
                                      EdgeAux* aux_out = nullptr, bool keep_inside = false) {
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
    if (tracker) {
        tracker->set_stages({"edges", "bloom"});
        tracker->begin_stage(0);
    }
    EdgeAux aux;
    cv::Mat out, field, edges;
    process_image_neon(bgr, palette, glow, threshold, env, out, aux, &field, &edges);
    if (tracker) tracker->begin_stage(1);
    if (keep_inside) keep_inside_composite(out, bgr, edges, field);
    if (!alpha8.empty()) {
        // alpha law: transparency comes only from the source art. wiped pixels
        // stay (dark), transparent pixels stay transparent — nothing else.
        std::vector<cv::Mat> och, bch;
        cv::split(out, bch);
        och = {bch[0], bch[1], bch[2], alpha8};
        cv::merge(och, out);
    }
    if (tracker) tracker->step(0.9f);
    std::string finalp = unique_output_path(out_path);
    std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 6};
    if (!imwrite_robust(finalp, out, params))
        throw std::runtime_error("cannot write image: " + finalp);
    if (aux_out) *aux_out = aux;
    return finalp;
}

}  // namespace neon
