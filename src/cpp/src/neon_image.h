// NEONIFY native — image engine: v1 tube law (sobel ridge + 4-scale glow stack)
// ported 1:1 from the python reference engine. laws are laws.
#pragma once

#include "neon_common.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <random>

namespace neon {

// unicode-safe imdecode/imencode: opencv ansi paths fail on windows for non-ascii files
inline cv::Mat imread_robust(const std::string& path, int flags = cv::IMREAD_COLOR) {
    std::vector<uint8_t> bytes = read_file_bytes(path);
    if (bytes.empty()) return cv::Mat();
    return cv::imdecode(bytes, flags);
}

inline bool imwrite_robust(const std::string& path, const cv::Mat& img,
                           const std::vector<int>& params = {}) {
    std::string ext = lower_ext(path);
    if (ext.empty()) ext = ".png";
    std::vector<uint8_t> buf;
    if (!cv::imencode(ext, img, buf, params)) return false;
    return write_file_bytes(path, buf.data(), buf.size());
}

inline const char* PALETTE_NAMES[] = {"electric", "crimson", "ice", "toxic", "violet", "golden", "ghost"};
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
                       cv::Mat& edges, EdgeAux& aux) {
    (void)glow; (void)env;
    cv::Mat lum = luminance(img_bgr);
    float noise = noise_estimate(lum);
    float pre_sigma = std::min(2.4f, std::max(0.0f, (noise - 2.0f) * 0.16f));
    cv::Mat work = (pre_sigma > 0.05f) ? gaussian_blur(lum, pre_sigma) : lum;
    cv::Mat base = gaussian_blur(work, 1.0f);
    cv::Mat mag = sobel_mag(base);
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

inline void process_image_neon(const cv::Mat& img_bgr, const std::string& palette,
                               float glow, float threshold, float env,
                               cv::Mat& out, EdgeAux& aux, cv::Mat* field_out = nullptr,
                               cv::Mat* edges_out = nullptr) {
    cv::Mat edges;
    edge_field(img_bgr, glow, threshold, env, edges, aux);
    cv::Mat field;
    neon_glow_stack(img_bgr, edges, glow, env, threshold, field);
    out = colorize(field, palette);
    if (field_out) *field_out = field;
    if (edges_out) *edges_out = edges;
}

// keep-inside law: the regions enclosed by the detected edges keep their original
// look; the edges themselves and everything outside stay neon
inline cv::Mat keep_inside_mask(const cv::Mat& edges) {
    cv::Mat e8;
    edges.convertTo(e8, CV_8U, 255.0);
    cv::threshold(e8, e8, 80, 255, cv::THRESH_BINARY);
    cv::dilate(e8, e8, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    cv::Mat padded = cv::Mat::zeros(e8.rows + 2, e8.cols + 2, CV_8U);
    e8.copyTo(padded(cv::Rect(1, 1, e8.cols, e8.rows)));
    cv::Mat inv;
    cv::subtract(cv::Scalar::all(255), padded, inv);
    cv::floodFill(inv, cv::Point(0, 0), cv::Scalar(128), nullptr, cv::Scalar(), cv::Scalar(), 4);
    cv::Mat holes = (inv == 255);
    cv::Mat holef;
    holes.convertTo(holef, CV_32F, 1.0 / 255.0);
    cv::Mat hole_in = holef(cv::Rect(1, 1, e8.cols, e8.rows)).clone();
    cv::Mat soft = 1.0 - smoothstep(0.04f, 0.30f, edges);
    cv::Mat m = hole_in.mul(soft);
    return gaussian_blur(m, 1.2f);
}

inline void keep_inside_composite(cv::Mat& neon_out, const cv::Mat& orig_bgr, const cv::Mat& edges) {
    if (neon_out.empty() || orig_bgr.empty() || edges.empty()) return;
    cv::Mat m = keep_inside_mask(edges);
    cv::Mat m3;
    cv::Mat chs[3] = {m, m, m};
    cv::merge(chs, 3, m3);
    cv::Mat orig32, out32;
    orig_bgr.convertTo(orig32, CV_32F);
    neon_out.convertTo(out32, CV_32F);
    cv::Mat mixed = out32.mul(1.0 - m3) + orig32.mul(m3);
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
    if (tracker) tracker->step(0.5f);
    EdgeAux aux;
    cv::Mat out, field, edges;
    process_image_neon(bgr, palette, glow, threshold, env, out, aux, &field, &edges);
    if (keep_inside) keep_inside_composite(out, bgr, edges);
    if (!alpha8.empty()) {
        cv::Mat a32, f32;
        alpha8.convertTo(a32, CV_32F, 1.0 / 255.0);
        cv::min(cv::max(field * 1.25, 0.f), 1.f, f32);
        cv::Mat a8;
        cv::Mat aacc = a32.mul(f32) * 255.0 + 0.5;
        aacc.convertTo(a8, CV_8U);
        std::vector<cv::Mat> och, bch;
        cv::split(out, bch);
        och = {bch[0], bch[1], bch[2], a8};
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
