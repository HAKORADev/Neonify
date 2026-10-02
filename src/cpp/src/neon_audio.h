// NEONIFY native — audio engine: fft tools, fx primitives, 7 profile chains.
// ported 1:1 from the python reference engine (np.hanning / np.sinc laws kept).
#pragma once

#include "neon_common.h"
#include <cmath>
#include <complex>
#include <random>
#include <cstring>

namespace neon {

using cplx = std::complex<float>;

// ---------------------------------------------------------------- fft
static void fft_inplace(std::vector<cplx>& a, bool inverse) {
    size_t n = a.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = float((inverse ? 2.0 : -2.0) * M_PI / double(len));
        cplx wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cplx w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; k++) {
                cplx u = a[i + k];
                cplx v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= float(n);
}

// rfft: real input -> n/2+1 complex bins (complex fft of full size, simple + correct)
static void rfft(const std::vector<float>& x, std::vector<cplx>& out, size_t nfft) {
    std::vector<cplx> a(nfft, cplx(0.f, 0.f));
    for (size_t i = 0; i < x.size() && i < nfft; i++) a[i] = cplx(x[i], 0.f);
    fft_inplace(a, false);
    out.assign(a.begin(), a.begin() + nfft / 2 + 1);
}

static void irfft(const std::vector<cplx>& S, std::vector<float>& out, size_t nfft) {
    std::vector<cplx> a(nfft);
    for (size_t k = 0; k <= nfft / 2 && k < S.size(); k++) a[k] = S[k];
    for (size_t k = nfft / 2 + 1; k < nfft; k++) a[k] = std::conj(S[nfft - k]);
    fft_inplace(a, true);
    out.resize(nfft);
    for (size_t i = 0; i < nfft; i++) out[i] = a[i].real();
}

inline std::vector<float> hanning(int n) {
    std::vector<float> w(std::max(1, n));
    if (n == 1) { w[0] = 1.f; return w; }
    for (int i = 0; i < n; i++)
        w[i] = 0.5f - 0.5f * std::cos(2.f * float(M_PI) * float(i) / float(n - 1));
    return w;
}

inline float nsinc(float x) {
    if (std::fabs(x) < 1e-8f) return 1.f;
    return std::sin(float(M_PI) * x) / (float(M_PI) * x);
}

// ---------------------------------------------------------------- filters
inline std::vector<float> fftconvolve(const std::vector<float>& x, const std::vector<float>& h) {
    size_t n = x.size() + h.size() - 1;
    size_t nfft = 1;
    while (nfft < n) nfft <<= 1;
    std::vector<cplx> X, H;
    rfft(x, X, nfft);
    rfft(h, H, nfft);
    for (size_t i = 0; i < X.size(); i++) X[i] *= H[i];
    std::vector<float> y;
    irfft(X, y, nfft);
    y.resize(std::min(x.size(), y.size()));
    return y;
}

inline std::vector<float> lowpass_ir(float cutoff, int sr, float tail = 1.2f) {
    int n = int(sr * tail) | 1;
    std::vector<float> h(n);
    float sum = 0.f;
    for (int i = 0; i < n; i++) {
        float t = (float(i) - float(n / 2)) / float(sr);
        float win = hanning(n)[i];
        h[i] = 2.f * float(M_PI) * cutoff * nsinc(2.f * float(M_PI) * cutoff * t) * win;
        sum += std::fabs(h[i]);
    }
    if (sum > 0) for (auto& v : h) v /= sum;
    return h;
}

inline std::vector<float> lowpass(const std::vector<float>& x, float cutoff, int sr) {
    if (cutoff >= sr * 0.48f) return x;
    return fftconvolve(x, lowpass_ir(cutoff, sr));
}

inline std::vector<float> highpass(const std::vector<float>& x, float cutoff, int sr) {
    auto lp = lowpass(x, cutoff, sr);
    std::vector<float> out(x.size());
    for (size_t i = 0; i < x.size(); i++) out[i] = x[i] - lp[i];
    return out;
}

inline std::vector<float> soft_clip(const std::vector<float>& x, float drive) {
    std::vector<float> out(x.size());
    for (size_t i = 0; i < x.size(); i++) out[i] = std::tanh(x[i] * drive);
    return out;
}

inline void normalize_pair(std::vector<float>& l, std::vector<float>& r, float target = 0.89f) {
    float p = 1e-9f;
    for (float v : l) p = std::max(p, std::fabs(v));
    for (float v : r) p = std::max(p, std::fabs(v));
    if (p <= 1e-9f) return;
    float k = target / p;
    for (auto& v : l) v *= k;
    for (auto& v : r) v *= k;
}

// ---------------------------------------------------------------- fx primitives
using Stereo = std::pair<std::vector<float>, std::vector<float>>;

inline Stereo fx_tube_drive(const Stereo& in, int sr, float amount) {
    float a = 1.0f + amount * 5.0f;
    float tanh_a = std::tanh(a);
    Stereo out;
    out.first.resize(in.first.size());
    out.second.resize(in.second.size());
    for (size_t i = 0; i < in.first.size(); i++) out.first[i] = std::tanh(in.first[i] * a) / tanh_a;
    for (size_t i = 0; i < in.second.size(); i++) out.second[i] = std::tanh(in.second[i] * a) / tanh_a;
    out.first = lowpass(out.first, 6500, sr);
    out.second = lowpass(out.second, 6500, sr);
    out.first = soft_clip(out.first, 1.2f * (1.0f + 0.12f * amount));
    out.second = soft_clip(out.second, 1.2f * (1.0f + 0.12f * amount));
    return out;
}

inline Stereo fx_pingpong_echo(const Stereo& in, int sr, float time_s, float fb, float damp_hz, float mix) {
    int d = std::max(1, int(time_s * sr));
    size_t n = in.first.size() + 2 * d;
    std::vector<float> acc_l(n, 0.f), acc_r(n, 0.f);
    std::copy(in.first.begin(), in.first.end(), acc_l.begin());
    std::copy(in.second.begin(), in.second.end(), acc_r.begin());
    auto dl = lowpass(in.first, damp_hz, sr);
    auto dr = lowpass(in.second, damp_hz, sr);
    float gl = fb, gr = fb;
    for (int i = 1; i < 14; i++) {
        size_t s = size_t(d) * (i + 1);
        if (s >= n) break;
        size_t seg = d;
        gl *= fb; gr *= fb;
        if (s + seg > n) seg = n - s;
        for (size_t k = 0; k < seg; k++) {
            acc_l[s + k] += dr[k] * gl;
            acc_r[s + k] += dl[k] * gr;
        }
    }
    Stereo out;
    out.first.resize(in.first.size());
    out.second.resize(in.second.size());
    for (size_t i = 0; i < in.first.size(); i++)
        out.first[i] = (1 - mix) * in.first[i] + mix * acc_l[i];
    for (size_t i = 0; i < in.second.size(); i++)
        out.second[i] = (1 - mix) * in.second[i] + mix * acc_r[i];
    return out;
}

inline Stereo fx_void_reverb(const Stereo& in, int sr, float mix, float tone_hz) {
    size_t n = in.first.size();
    std::vector<float> rl(n, 0.f), rr(n, 0.f);
    const int dms[6] = {61, 89, 127, 173, 211, 293};
    const float gs[6] = {0.72f, 0.66f, 0.61f, 0.57f, 0.52f, 0.47f};
    for (int b = 0; b < 6; b++) {
        int d = std::max(1, int(dms[b] * sr / 1000));
        const std::vector<float>& w = (dms[b] % 2) ? in.first : in.second;
        std::vector<float> acc(n, 0.f);
        for (int rep = 1; rep <= 5; rep++) {
            size_t s = size_t(d) * rep;
            if (s >= n) break;
            float g = std::pow(gs[b], float(rep));
            for (size_t k = 0; k + s < n; k++) acc[k + s] += w[k] * g;
        }
        if (dms[b] % 2) for (size_t k = 0; k < n; k++) rl[k] += acc[k];
        else for (size_t k = 0; k < n; k++) rr[k] += acc[k];
    }
    for (auto& v : rl) v /= 6.f;
    for (auto& v : rr) v /= 6.f;
    rl = lowpass(rl, tone_hz, sr);
    rr = lowpass(rr, tone_hz, sr);
    Stereo out;
    out.first.resize(n);
    out.second.resize(n);
    for (size_t i = 0; i < n; i++) {
        out.first[i] = (1 - mix) * in.first[i] + mix * rl[i];
        out.second[i] = (1 - mix) * in.second[i] + mix * rr[i];
    }
    return out;
}

inline Stereo fx_tremolo(const Stereo& in, int sr, float rate, float depth) {
    size_t n = in.first.size();
    Stereo out = in;
    for (size_t i = 0; i < n; i++) {
        float t = float(i) / float(sr);
        float m = 1.0f - depth * 0.5f * (1.0f - std::sin(2.f * float(M_PI) * rate * t));
        out.first[i] *= m;
        out.second[i] *= m;
    }
    return out;
}

inline std::vector<float> vibrato_bend(const std::vector<float>& x, int sr, float rate, float depth_ms) {
    size_t n = x.size();
    std::vector<float> out(n, 0.f);
    for (size_t i = 0; i < n; i++) {
        float t = float(i) / float(sr);
        float delay = depth_ms * 0.001f * float(sr) * (0.5f + 0.5f * std::sin(2.f * float(M_PI) * rate * t));
        float pos = float(i) - delay;
        int i0 = int(std::floor(pos));
        float frac = pos - float(i0);
        int i0c = std::min(std::max(i0, 0), int(n) - 1);
        int i1c = std::min(std::max(i0 + 1, 0), int(n) - 1);
        out[i] = x[size_t(i0c)] * (1 - frac) + x[size_t(i1c)] * frac;
    }
    return out;
}

inline Stereo fx_vibrato(const Stereo& in, int sr, float rate, float depth_ms) {
    return {vibrato_bend(in.first, sr, rate, depth_ms),
            vibrato_bend(in.second, sr, rate, depth_ms)};
}

inline Stereo fx_ring_mod(const Stereo& in, int sr, float hz, float mix, float depth = 0.85f) {
    size_t n = in.first.size();
    Stereo out = in;
    for (size_t i = 0; i < n; i++) {
        float t = float(i) / float(sr);
        float car = std::sin(2.f * float(M_PI) * hz * t) * depth + (1 - depth);
        out.first[i] = in.first[i] * (1 - mix) + in.first[i] * car * mix;
        out.second[i] = in.second[i] * (1 - mix) + in.second[i] * car * mix;
    }
    return out;
}

inline Stereo fx_crush(const Stereo& in, int sr, float bits) {
    (void)sr;
    float q = std::pow(2.0f, bits - 1);
    Stereo out = in;
    for (auto& v : out.first) v = std::round(v * q) / q;
    for (auto& v : out.second) v = std::round(v * q) / q;
    return out;
}

inline Stereo fx_slash_sweep(const Stereo& in, int sr, float gain) {
    size_t n = in.first.size();
    Stereo out = in;
    for (size_t i = 0; i < n; i++) {
        float t = float(i) / float(std::max<size_t>(1, n - 1));
        float sweep = std::pow(std::sin(float(M_PI) * t), 2.f) * gain;
        float off = std::sin(2.f * float(M_PI) * 0.5f * t) * 0.4f;
        out.first[i] *= 1 + sweep * (0.6f + off);
        out.second[i] *= 1 + sweep * (0.6f - off);
    }
    (void)sr;
    return out;
}

// ---------------------------------------------------------------- spectral tools
inline std::vector<cplx> stft_mag_phase(const std::vector<float>& x, int n_fft = 2048, int hop = 512) {
    auto w = hanning(n_fft);
    int frames = 1 + std::max(0, (int(x.size()) - n_fft + hop - 1)) / hop;
    size_t need = size_t(frames * hop + n_fft);
    std::vector<float> xp(x);
    if (xp.size() < need) xp.resize(need, 0.f);
    std::vector<cplx> S(size_t(n_fft / 2 + 1) * frames);
    std::vector<float> seg(n_fft);
    std::vector<cplx> bins;
    for (int i = 0; i < frames; i++) {
        for (int k = 0; k < n_fft; k++) seg[k] = xp[size_t(i * hop + k)] * w[k];
        rfft(seg, bins, size_t(n_fft));
        for (int k = 0; k <= n_fft / 2; k++) S[size_t(k) * frames + i] = bins[k];
    }
    return S;
}

inline std::vector<float> istft(const std::vector<cplx>& S, int frames, int n_fft, int hop, size_t length) {
    auto w = hanning(n_fft);
    size_t out_len = size_t(n_fft) + size_t(frames - 1) * hop;
    std::vector<float> out(out_len, 0.f), win_sum(out_len, 0.f), seg;
    for (int i = 0; i < frames; i++) {
        std::vector<cplx> col(n_fft / 2 + 1);
        for (int k = 0; k <= n_fft / 2; k++) col[k] = S[size_t(k) * frames + i];
        irfft(col, seg, size_t(n_fft));
        for (int k = 0; k < n_fft; k++) {
            out[size_t(i * hop + k)] += seg[k] * w[k];
            win_sum[size_t(i * hop + k)] += w[k];
        }
    }
    for (size_t i = 0; i < out_len; i++) out[i] /= std::max(win_sum[i], 1e-6f);
    if (out.size() < length) out.resize(length, 0.f);
    out.resize(std::min(out.size(), length));
    return out;
}

inline std::vector<float> spectral_shift(const std::vector<float>& x, int sr, float semitones) {
    if (std::fabs(semitones) < 0.01f) return x;
    (void)sr;
    float ratio = std::pow(2.0f, semitones / 12.0f);
    int n_fft = 2048, hop = 512;
    auto S = stft_mag_phase(x, n_fft, hop);
    int frames = int(x.size()) ? 1 + std::max(0, (int(x.size()) - n_fft + hop - 1)) / hop : 1;
    int bins = n_fft / 2 + 1;
    std::vector<cplx> moved(S.size(), cplx(0.f, 0.f));
    int shift = int(std::round((1 - ratio) * bins / 2));
    int lo = std::max(0, shift), hi = bins + std::min(0, shift);
    for (int i = 0; i < frames; i++) {
        if (shift != 0) {
            for (int k = lo; k < hi; k++) moved[size_t(k) * frames + i] = S[size_t(k - shift) * frames + i];
        } else {
            for (int k = 0; k < bins; k++) moved[size_t(k) * frames + i] = S[size_t(k) * frames + i];
        }
    }
    return istft(moved, frames, n_fft, hop, x.size());
}

inline Stereo fx_ice_shimmer(const Stereo& in, int sr, float mix) {
    auto one = [&](const std::vector<float>& x) {
        auto sh = spectral_shift(x, sr, 12.0f);
        sh = lowpass(sh, 9000, sr);
        auto air = highpass(x, 6000, sr);
        std::vector<float> out(x.size());
        for (size_t i = 0; i < x.size(); i++)
            out[i] = x[i] * (1 - mix) + (sh[i] * 0.45f + air[i] * 0.5f) * mix;
        return out;
    };
    return {one(in.first), one(in.second)};
}

inline Stereo fx_ghost_fog(const Stereo& in, int sr, float mix) {
    size_t n = in.first.size();
    auto rev = fx_void_reverb(in, sr, 1.0f, 1800);
    std::vector<float> fog_l(n), fog_r(n);
    auto rl12 = lowpass(rev.first, 1200, sr);
    auto rl28 = lowpass(rev.first, 2800, sr);
    auto rr12 = lowpass(rev.second, 1200, sr);
    auto rr28 = lowpass(rev.second, 2800, sr);
    for (size_t i = 0; i < n; i++) {
        float t = float(i) / float(sr);
        float breathe = 0.5f + 0.5f * std::sin(2.f * float(M_PI) * 0.13f * t);
        fog_l[i] = rl12[i] * (1 - breathe * 0.45f) + rl28[i] * (breathe * 0.45f);
        fog_r[i] = rr12[i] * (1 - (1 - breathe) * 0.45f) + rr28[i] * ((1 - breathe) * 0.45f);
    }
    auto dl = vibrato_bend(in.first, sr, 0.4f, 6.0f);
    auto dr = vibrato_bend(in.second, sr, 0.4f, 6.0f);
    Stereo out;
    out.first.resize(n);
    out.second.resize(n);
    for (size_t i = 0; i < n; i++) {
        out.first[i] = (1 - mix) * in.first[i] + mix * (fog_l[i] * 0.8f + dl[i] * 0.2f);
        out.second[i] = (1 - mix) * in.second[i] + mix * (fog_r[i] * 0.8f + dr[i] * 0.2f);
    }
    return out;
}

inline Stereo fx_fire_burn(const Stereo& in, int sr, float glow) {
    std::mt19937_64 rng(261002);
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    std::normal_distribution<float> gauss(0.f, 1.f);
    size_t n = in.first.size();
    auto crackle = [&](float thr_scale, float level) {
        std::vector<float> burst(n);
        for (size_t i = 0; i < n; i++) {
            float imp = uni(rng) < (0.00008f * thr_scale) ? 1.f : 0.f;
            burst[i] = imp * (uni(rng) * 0.8f + 0.2f) * 6.f * level;
        }
        return lowpass(burst, 3800, sr);
    };
    auto cl = crackle(1 + glow, 1.0f);
    auto cr = crackle(1 + glow, 0.6f);
    std::vector<float> rum(n);
    for (size_t i = 0; i < n; i++) rum[i] = gauss(rng);
    rum = lowpass(rum, 90, sr);
    for (auto& v : rum) v *= 2.2f;
    auto out = fx_tube_drive(in, sr, 0.35f + 0.2f * std::min(glow, 2.f));
    out = fx_tremolo(out, sr, 5.5f, 0.18f);
    for (size_t i = 0; i < n; i++) {
        out.first[i] += cl[i] * 0.5f + rum[i] * 0.7f;
        out.second[i] += cr[i] * 0.5f + rum[i] * 0.7f;
    }
    return out;
}

inline Stereo fx_robotic(const Stereo& in, int sr, float glow) {
    (void)glow;
    auto l2 = fx_ring_mod(in, sr, 88.0f, 0.65f);
    int d1 = std::max(1, int(0.021f * sr));
    int d2 = std::max(1, int(0.037f * sr));
    Stereo out;
    out.first.resize(in.first.size());
    out.second.resize(in.second.size());
    for (size_t i = 0; i < in.first.size(); i++)
        out.first[i] = l2.first[i] - (i >= size_t(d1) ? l2.first[i - d1] : 0.f) * 0.45f;
    for (size_t i = 0; i < in.second.size(); i++)
        out.second[i] = l2.second[i] - (i >= size_t(d2) ? l2.second[i - d2] : 0.f) * 0.45f;
    return fx_crush(out, sr, 10);
}

inline Stereo fx_void_pitch(const Stereo& in, int sr, float mix) {
    auto l2 = spectral_shift(lowpass(in.first, 4000, sr), sr, -12.0f);
    auto r2 = spectral_shift(lowpass(in.second, 4000, sr), sr, -12.0f);
    size_t n = in.first.size();
    Stereo out;
    out.first.resize(n);
    out.second.resize(n);
    for (size_t i = 0; i < n; i++) {
        float wide_l = 0.5f * (in.first[i] + l2[i]);
        float wide_r = 0.5f * (in.second[i] - r2[i]);
        out.first[i] = (1 - mix) * in.first[i] + mix * wide_l;
        out.second[i] = (1 - mix) * in.second[i] + mix * wide_r;
    }
    return out;
}

// ---------------------------------------------------------------- profiles
inline const char* AUDIO_PROFILES[] = {"fire", "ice", "robotic", "ghost", "void", "echo", "slash"};

struct Advanced {
    std::map<std::string, float> params;
    float get(const std::string& k, float dflt) const {
        auto it = params.find(k);
        return it == params.end() ? dflt : it->second;
    }
};

inline Stereo apply_audio_profile(const Stereo& in, int sr, const std::string& profile,
                                  float glow, const Advanced& advanced,
                                  const std::function<void(const std::string&, float)>& on_step,
                                  std::vector<std::string>* steps_done = nullptr) {
    float g = std::min(3.0f, std::max(0.1f, glow));
    const Advanced& adv = advanced;
    struct Step { std::string name; std::function<Stereo()> fn; };
    std::vector<Step> chain;

    if (profile == "fire") {
        chain.push_back({"heat drive", [&] { return fx_tube_drive(in, sr, adv.get("drive", 0.3f + 0.22f * std::min(g, 2.f))); }});
        chain.push_back({"flicker", [&] { return fx_tremolo(in, sr, adv.get("flicker_rate", 5.5f), std::min(0.14f + 0.05f * g, 0.5f)); }});
        chain.push_back({"crackle", [&] {
            std::mt19937_64 rng1(261002), rng2(726100);
            std::uniform_real_distribution<float> uni(0.f, 1.f);
            size_t n = in.first.size();
            std::vector<float> b1(n), b2(n);
            for (size_t i = 0; i < n; i++) {
                b1[i] = (uni(rng1) < (0.00008f * (1 + g)) ? 1.f : 0.f) * 6.f;
                b2[i] = (uni(rng2) < (0.00006f * (1 + g)) ? 1.f : 0.f) * 6.f;
            }
            float lv = adv.get("crackle", 0.5f);
            auto c1 = lowpass(b1, 3800, sr);
            auto c2 = lowpass(b2, 3800, sr);
            Stereo out = in;
            for (size_t i = 0; i < n; i++) {
                out.first[i] += c1[i] * lv;
                out.second[i] += c2[i] * lv;
            }
            return out;
        }});
        chain.push_back({"rumble", [&] {
            std::mt19937_64 rng1(112233), rng2(332211);
            std::normal_distribution<float> gauss(0.f, 1.f);
            size_t n = in.first.size();
            std::vector<float> w1(n), w2(n);
            for (size_t i = 0; i < n; i++) { w1[i] = gauss(rng1); w2[i] = gauss(rng2); }
            float hz = adv.get("rumble_hz", 90.f), lv = adv.get("rumble", 0.6f);
            auto r1 = lowpass(w1, hz, sr);
            auto r2 = lowpass(w2, hz, sr);
            Stereo out = in;
            for (size_t i = 0; i < n; i++) {
                out.first[i] += r1[i] * lv;
                out.second[i] += r2[i] * lv;
            }
            return out;
        }});
    } else if (profile == "ice") {
        chain.push_back({"shimmer", [&] { return fx_ice_shimmer(in, sr, adv.get("shimmer_mix", 0.28f + 0.1f * std::min(g, 2.f))); }});
        chain.push_back({"glass breath", [&] { return fx_vibrato(in, sr, adv.get("breath_rate", 0.5f), adv.get("breath_depth", 3.5f)); }});
        chain.push_back({"frost echo", [&] { return fx_pingpong_echo(in, sr, adv.get("time", 0.19f), adv.get("fb", 0.3f), adv.get("damp", 7000.f), adv.get("mix", 0.2f + 0.06f * g)); }});
    } else if (profile == "robotic") {
        chain.push_back({"ring mod", [&] { return fx_ring_mod(in, sr, adv.get("ring_hz", 88.f), adv.get("ring_mix", 0.6f)); }});
        chain.push_back({"formant combs", [&] {
            int d1 = std::max(1, int(0.021f * sr));
            int d2 = std::max(1, int(0.037f * sr));
            float comb = adv.get("comb", 0.45f);
            Stereo out;
            out.first.resize(in.first.size());
            out.second.resize(in.second.size());
            for (size_t i = 0; i < in.first.size(); i++)
                out.first[i] = in.first[i] - (i >= size_t(d1) ? in.first[i - d1] : 0.f) * comb;
            for (size_t i = 0; i < in.second.size(); i++)
                out.second[i] = in.second[i] - (i >= size_t(d2) ? in.second[i - d2] : 0.f) * comb;
            return out;
        }});
        chain.push_back({"crush", [&] { return fx_crush(in, sr, adv.get("bits", 10.f)); }});
    } else if (profile == "ghost") {
        chain.push_back({"fog reverb", [&] { return fx_ghost_fog(in, sr, adv.get("fog_mix", 0.42f + 0.08f * g)); }});
        chain.push_back({"whisper detune", [&] { return fx_vibrato(in, sr, adv.get("whisper_rate", 0.37f), adv.get("whisper_depth", 5.f)); }});
        chain.push_back({"far echo", [&] { return fx_pingpong_echo(in, sr, adv.get("time", 0.42f), adv.get("fb", 0.42f), adv.get("damp", 2600.f), adv.get("mix", 0.3f)); }});
    } else if (profile == "void") {
        chain.push_back({"descent", [&] { return fx_void_pitch(in, sr, adv.get("pitch_mix", 0.45f)); }});
        chain.push_back({"abyss reverb", [&] { return fx_void_reverb(in, sr, adv.get("mix", 0.4f + 0.08f * g), adv.get("tone", 1500.f)); }});
        chain.push_back({"cave echo", [&] { return fx_pingpong_echo(in, sr, adv.get("time", 0.55f), adv.get("fb", 0.5f), adv.get("damp", 1800.f), adv.get("mix", 0.3f)); }});
    } else if (profile == "echo") {
        chain.push_back({"ping-pong", [&] { return fx_pingpong_echo(in, sr, adv.get("time", 0.31f), adv.get("fb", 0.45f), adv.get("damp", 4200.f), adv.get("mix", 0.35f)); }});
        chain.push_back({"tone", [&] { return Stereo{lowpass(in.first, adv.get("lp", 9000.f), sr), lowpass(in.second, adv.get("lp", 9000.f), sr)}; }});
    } else {
        chain.push_back({"slash sweep", [&] { return fx_slash_sweep(in, sr, adv.get("gain", 0.4f + 0.15f * g)); }});
    }

    Stereo cur = in;
    int total = int(chain.size());
    for (int i = 0; i < total; i++) {
        if (on_step) on_step(chain[i].name, float(i) / float(total));
        cur = chain[i].fn();
        if (steps_done) steps_done->push_back(chain[i].name);
        if (on_step) on_step(chain[i].name, float(i + 1) / float(total));
    }
    normalize_pair(cur.first, cur.second);
    return cur;
}

// ---------------------------------------------------------------- io
inline bool decode_audio_stereo(const std::string& path, std::vector<float>& l,
                                std::vector<float>& r, int sr = AUDIO_SR) {
    std::string srate = std::to_string(sr);
    Proc p;
    if (!p.spawn({"ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
                  "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "2", "-ar", srate, "-"},
                 true, false))
        return false;
    std::vector<float> raw;
    char buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), p.out)) > 0) {
        size_t old = raw.size();
        raw.resize(old + got / 4);
        std::memcpy(&raw[old], buf, got - (got % 4));
        if (got % 4) raw.resize(old + (got - got % 4) / 4);
    }
    p.wait_close();
    if (raw.size() < 256) return false;
    size_t frames = raw.size() / 2;
    l.resize(frames);
    r.resize(frames);
    for (size_t i = 0; i < frames; i++) {
        l[i] = raw[i * 2];
        r[i] = raw[i * 2 + 1];
    }
    return true;
}

inline std::vector<std::pair<float, float>> spatial_energy_map(const std::vector<float>& al,
                                                               const std::vector<float>& ar,
                                                               int sr, int n_frames, float fps) {
    int n = int(al.size());
    int per_frame = std::max(1, int(float(sr) / fps));
    std::vector<std::pair<float, float>> maps(n_frames, {0.f, 0.f});
    int n_fft = 1024;
    auto win = hanning(n_fft);
    for (int f = 0; f < n_frames; f++) {
        int s = f * per_frame;
        if (s + 64 > n) break;
        int seg_len = std::min(per_frame, n - s);
        double el = 0, er = 0;
        for (int i = 0; i < seg_len; i++) {
            el += double(al[s + i]) * al[s + i];
            er += double(ar[s + i]) * ar[s + i];
        }
        float bal = float((el - er) / (el + er + 1e-9));
        float vert = 0.f;
        if (seg_len >= n_fft) {
            std::vector<float> seg(n_fft);
            for (int i = 0; i < n_fft; i++) seg[i] = al[s + i] + ar[s + i];
            for (int i = 0; i < n_fft; i++) seg[i] *= win[i];
            std::vector<cplx> S;
            rfft(seg, S, size_t(n_fft));
            double sm = 0, sf = 0;
            for (int k = 0; k <= n_fft / 2; k++) {
                float mag = std::abs(S[k]);
                float freq = float(k) * float(sr) / float(n_fft);
                sm += mag;
                sf += mag * freq;
            }
            float cent = float(sf / (sm + 1e-9));
            vert = std::min(1.f, std::max(-1.f, (cent - 1400.f) / 2600.f));
        }
        maps[f] = {bal, vert};
    }
    int k = 9;
    auto ker = hanning(k);
    float ksum = 0;
    for (float v : ker) ksum += v;
    for (auto& v : ker) v /= ksum;
    for (int c = 0; c < 2; c++) {
        std::vector<float> col(n_frames);
        for (int i = 0; i < n_frames; i++) col[i] = c ? maps[i].second : maps[i].first;
        std::vector<float> padded(n_frames + k, 0.f);
        for (int i = 0; i < n_frames; i++) padded[i + k / 2] = col[i];
        for (int i = 0; i < k / 2; i++) padded[i] = col[0];
        for (int i = 0; i < k / 2; i++) padded[n_frames + k / 2 + i] = col[n_frames - 1];
        for (int i = 0; i < n_frames; i++) {
            float acc = 0;
            for (int j = 0; j < k; j++) acc += padded[i + j] * ker[j];
            if (c) maps[i].second = acc;
            else maps[i].first = acc;
        }
    }
    return maps;
}

inline std::string neonize_audio_file(const std::string& inp, const std::string& out_path,
                                      const std::string& profile, float glow,
                                      const Advanced& advanced, StageTracker* tracker) {
    std::vector<float> l, r;
    if (!decode_audio_stereo(inp, l, r)) throw std::runtime_error("cannot decode audio: " + inp);
    if (tracker) {
        tracker->set_stages({"decode", "profile chain", "mastering", "write"});
        tracker->begin_stage(0, "decode");
        tracker->step(1.0);
        tracker->complete_stage(0);
        tracker->begin_stage(1, "profile chain");
    }
    Stereo in{l, r};
    auto on_step = [&](const std::string&, float frac) {
        if (tracker) tracker->step(frac);
    };
    auto out = apply_audio_profile(in, AUDIO_SR, profile, glow, advanced, on_step);
    if (tracker) {
        tracker->complete_stage(1);
        tracker->begin_stage(2, "mastering");
        tracker->step(0.5);
        tracker->complete_stage(2);
        tracker->begin_stage(3, "write");
        tracker->step(0.5);
    }
    std::string finalp = unique_output_path(out_path);
    write_wav_stereo(finalp, out.first, out.second);
    if (tracker) {
        tracker->complete_stage(3);
        tracker->finish();
    }
    return finalp;
}

}  // namespace neon
