// NEONIFY native — desktop neonifier gpu pass (windows): the duplication frame
// stays on the d3d11 device the whole time. three compute passes — sobel edges,
// four-scale glow, palette compose — then one staging readback for the overlay
// window. d3dcompiler_47 loads at runtime so the build needs no sdk beyond the
// system headers. any failure reports false once and the cpu path takes over.
#pragma once

#ifdef _WIN32

#include "neon_common.h"
#include "neon_image.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

namespace neon {

namespace {
const char* const kEdgesSrc = R"HLSL(
Texture2D<float4> src : register(t0);
RWTexture2D<float> edges : register(u0);
RWTexture2D<float> hue : register(u1);
float ss(float lo, float hi, float x) {
    float t = saturate((x - lo) / max(1e-6, hi - lo));
    return t * t * (3.0 - 2.0 * t);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float lum[3][3];
    [unroll]
    for (int dy = -1; dy <= 1; dy++)
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
            lum[dy + 1][dx + 1] = dot(src.Load(uint3(id.x + dx, id.y + dy, 0)).rgb,
                                      float3(0.2126, 0.7152, 0.0722));
    float gx = (lum[0][2] + 2 * lum[1][2] + lum[2][2]) - (lum[0][0] + 2 * lum[1][0] + lum[2][0]);
    float gy = (lum[2][0] + 2 * lum[2][1] + lum[2][2]) - (lum[0][0] + 2 * lum[0][1] + lum[0][2]);
    gx *= 0.25; gy *= 0.25;
    float mag = sqrt(gx * gx + gy * gy);
    edges[id.xy] = ss(Thr, Thr + 0.22, mag);
    hue[id.xy] = (atan2(gy, gx) + 3.14159265) / 6.28318530;
}
)HLSL";

const char* const kGlowSrc = R"HLSL(
Texture2D<float> edgeTex : register(t0);
SamplerState samp : register(s0);
RWTexture2D<float> energy : register(u0);
static const float2 ring[8] = {
    float2( 1, 0), float2(-1, 0), float2(0,  1), float2(0, -1),
    float2( 1, 1), float2(-1, 1), float2(1, -1), float2(-1, -1)
};
float blurAt(float2 uv, float r) {
    float acc = 0;
    [unroll]
    for (int i = 0; i < 8; i++)
        acc += edgeTex.SampleLevel(samp, uv + ring[i] * r / float2(ResX, ResY), 0).r;
    return acc / 8.0;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float2 uv = (id.xy + 0.5) / float2(ResX, ResY);
    float e = edgeTex.SampleLevel(samp, uv, 0).r;
    float g = blurAt(uv, 2.0) * 0.9 + blurAt(uv, 6.0) * 0.62
            + blurAt(uv, 16.0) * 0.45 + blurAt(uv, 38.0) * 0.32;
    g *= Glow * Env;
    float excite = 0.72 + 0.42 * Env;
    energy[id.xy] = saturate((e * 1.15 + g * 0.85) * excite);
}
)HLSL";

const char* const kComposeSrc = R"HLSL(
Texture2D<float> energyTex : register(t0);
Texture2D<float> edgeTex : register(t1);
Texture2D<float> hueTex : register(t2);
Texture2D<float4> lutTex : register(t3);
Texture2D<float4> src : register(t4);
SamplerState samp : register(s0);
RWTexture2D<float4> outTex : register(u0);
float3 hsv2rgb(float h, float s, float v) {
    h = frac(h) * 6.0;
    int i = (int)floor(h) % 6;
    float f = h - floor(h);
    float p = v * (1 - s);
    float q = v * (1 - s * f);
    float t = v * (1 - s * (1 - f));
    float3 c = i == 0 ? float3(v, t, p) : i == 1 ? float3(q, v, p) : i == 2 ? float3(p, v, t)
             : i == 3 ? float3(p, q, v) : i == 4 ? float3(t, p, v) : float3(v, p, q);
    return c;
}
float haloAt(float2 uv) {
    float acc = 0;
    [unroll]
    for (int i = 0; i < 8; i++) {
        float2 o = float2(i < 4 ? 6.0 : -6.0, (i % 2) == 0 ? 6.0 : -6.0);
        acc += edgeTex.SampleLevel(samp, uv + o / float2(ResX, ResY), 0).r;
    }
    return acc / 8.0;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float2 uv = (id.xy + 0.5) / float2(ResX, ResY);
    float e = energyTex.SampleLevel(samp, uv, 0).r;
    float edge = edgeTex.SampleLevel(samp, uv, 0).r;
    float core = pow(edge, 1.2) * 0.85 * 0.55;
    float4 col;
    if (Spectrum > 0.5) {
        float h = hueTex.SampleLevel(samp, uv, 0).r;
        col = float4(saturate(hsv2rgb(h, 0.9, e) + core), 1);
    } else {
        col = saturate(lutTex.SampleLevel(samp, float2(saturate(e), 0.5), 0) + core);
        col.a = 1;
    }
    if (Inside > 1.5) {
        col.rgb = lerp(src.SampleLevel(samp, uv, 0).rgb, col.rgb, saturate(e));
    } else if (Inside > 0.5) {
        float m = saturate(edge * 1.15 + haloAt(uv) * 0.55);
        col.rgb = lerp(src.SampleLevel(samp, uv, 0).rgb, col.rgb, m);
    }
    outTex[id.xy] = col;
}
)HLSL";


}  // namespace

class DesktopNeonGpu {
public:
    ~DesktopNeonGpu() { release(); }

    bool init(ID3D11Device* device) {
        device_ = device;
        device_->GetImmediateContext(&context_);
        if (!context_) return false;
        HMODULE mod = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!mod) mod = LoadLibraryW(L"d3dcompiler_43.dll");
        if (!mod) return false;
        compile_ = (pD3DCompile)(void*)GetProcAddress(mod, "D3DCompile");
        return compile_ != nullptr;
    }

    // recompiles the three passes when settings or size change; returns false
    // once and the caller falls back to cpu for the session
    bool process(ID3D11Texture2D* src, ID3D11Texture2D* dst, int w, int h,
                 const std::string& palette, float glow, float threshold, float env, int inside) {
        if (!device_ || !compile_) return false;
        uint64_t sig = params_sig(palette, glow, threshold, env, inside, w, h);
        if (sig != sig_) {
            release_passes();
            if (!compile_passes(palette, glow, threshold, env, inside, w, h)) return false;
            if (!make_targets(w, h)) return false;
            sig_ = sig;
        }
        if (last_dst_ != dst) {
            if (out_uav_) { out_uav_->Release(); out_uav_ = nullptr; }
            if (FAILED(device_->CreateUnorderedAccessView(dst, nullptr, &out_uav_))) return false;
            last_dst_ = dst;
        }
        ID3D11ShaderResourceView* src_srv = nullptr;
        if (FAILED(device_->CreateShaderResourceView(src, nullptr, &src_srv))) return false;

        float vzero[4] = {0.f, 0.f, 0.f, 0.f};
        context_->ClearUnorderedAccessViewFloat(edges_uav_, vzero);

        context_->CSSetShader(edges_cs_, nullptr, 0);
        ID3D11ShaderResourceView* sr0[] = {src_srv};
        ID3D11UnorderedAccessView* ua0[] = {edges_uav_, hue_uav_};
        context_->CSSetShaderResources(0, 1, sr0);
        context_->CSSetSamplers(0, 1, &sampler_);
        context_->CSSetUnorderedAccessViews(0, 2, ua0, nullptr);
        context_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

        context_->CSSetShader(glow_cs_, nullptr, 0);
        ID3D11ShaderResourceView* sr1[] = {edges_srv_};
        ID3D11UnorderedAccessView* ua1[] = {energy_uav_};
        context_->CSSetShaderResources(0, 1, sr1);
        context_->CSSetUnorderedAccessViews(0, 1, ua1, nullptr);
        context_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

        context_->CSSetShader(compose_cs_, nullptr, 0);
        ID3D11ShaderResourceView* sr2[] = {energy_srv_, edges_srv_, hue_srv_, lut_srv_, src_srv};
        ID3D11UnorderedAccessView* ua2[] = {out_uav_};
        context_->CSSetShaderResources(0, 5, sr2);
        context_->CSSetUnorderedAccessViews(0, 1, ua2, nullptr);
        context_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

        ID3D11UnorderedAccessView* ua_none[2] = {nullptr, nullptr};
        ID3D11ShaderResourceView* sr_none[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        context_->CSSetUnorderedAccessViews(0, 2, ua_none, nullptr);
        context_->CSSetShaderResources(0, 5, sr_none);
        context_->CSSetShader(nullptr, nullptr, 0);
        src_srv->Release();
        return true;
    }

private:
    static uint64_t params_sig(const std::string& p, float g, float t, float e, int in, int w, int h) {
        uint64_t s = 1469598103934665603ull;
        auto mix = [&s](uint64_t v) { s ^= v; s *= 1099511628211ull; };
        for (char c : p) mix(uint8_t(c));
        mix(uint64_t(g * 100));
        mix(uint64_t(t * 1000));
        mix(uint64_t(e * 100));
        mix(uint64_t(in));
        mix(uint64_t(w));
        mix(uint64_t(h));
        return s;
    }

    bool compile_passes(const std::string& palette, float glow, float threshold, float env,
                        int inside, int w, int h) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%g", threshold);
        std::string thr = buf;
        std::snprintf(buf, sizeof(buf), "%g", glow);
        std::string glow_s = buf;
        std::snprintf(buf, sizeof(buf), "%g", env);
        std::string env_s = buf;
        std::snprintf(buf, sizeof(buf), "%d", inside);
        std::string inside_s = buf;
        std::snprintf(buf, sizeof(buf), "%d", palette == "spectrum" ? 1 : 0);
        std::string spec_s = buf;
        std::snprintf(buf, sizeof(buf), "%d", w);
        std::string w_s = buf;
        std::snprintf(buf, sizeof(buf), "%d", h);
        std::string h_s = buf;
        D3D_SHADER_MACRO m_thr = {"Thr", thr.c_str()};
        D3D_SHADER_MACRO m_glow = {"Glow", glow_s.c_str()};
        D3D_SHADER_MACRO m_env = {"Env", env_s.c_str()};
        D3D_SHADER_MACRO m_inside = {"Inside", inside_s.c_str()};
        D3D_SHADER_MACRO m_spec = {"Spectrum", spec_s.c_str()};
        D3D_SHADER_MACRO m_w = {"ResX", w_s.c_str()};
        D3D_SHADER_MACRO m_h = {"ResY", h_s.c_str()};
        D3D_SHADER_MACRO macros[9] = {m_thr, m_glow, m_env, m_inside, m_spec, m_w, m_h, {nullptr, nullptr}, {nullptr, nullptr}};
        if (!make_cs(kEdgesSrc, macros, &edges_cs_)) return false;
        if (!make_cs(kGlowSrc, macros, &glow_cs_)) return false;
        if (!make_cs(kComposeSrc, macros, &compose_cs_)) return false;
        cv::Mat lut = build_palette_lut(palette, 256);
        cv::Mat lut4(1, 256, CV_8UC4);
        for (int i = 0; i < 256; i++) {
            cv::Vec3b v = lut.at<cv::Vec3b>(i);
            lut4.at<cv::Vec4b>(i) = cv::Vec4b(v[0], v[1], v[2], 255);
        }
        D3D11_TEXTURE2D_DESC d{};
        d.Width = 256;
        d.Height = 1;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{lut4.data, 256 * 4, 0};
        if (FAILED(device_->CreateTexture2D(&d, &init, &lut_tex_))) return false;
        if (FAILED(device_->CreateShaderResourceView(lut_tex_, nullptr, &lut_srv_))) return false;
        D3D11_SAMPLER_DESC s{};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxLOD = 1e9f;
        if (FAILED(device_->CreateSamplerState(&s, &sampler_))) return false;
        return true;
    }

    bool make_targets(int w, int h) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        struct { ID3D11Texture2D** tex; ID3D11ShaderResourceView** srv; ID3D11UnorderedAccessView** uav; } chain[3] = {
            {&edges_tex_, &edges_srv_, &edges_uav_},
            {&hue_tex_, &hue_srv_, &hue_uav_},
            {&energy_tex_, &energy_srv_, &energy_uav_},
        };
        for (auto& c : chain) {
            if (FAILED(device_->CreateTexture2D(&d, nullptr, c.tex))) return false;
            if (FAILED(device_->CreateShaderResourceView(*c.tex, nullptr, c.srv))) return false;
            if (FAILED(device_->CreateUnorderedAccessView(*c.tex, nullptr, c.uav))) return false;
        }
        return true;
    }

    bool make_cs(const char* hlsl, D3D_SHADER_MACRO* macros, ID3D11ComputeShader** out) {
        ID3DBlob* code = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(compile_(hlsl, std::strlen(hlsl), nullptr, macros, nullptr,
                            "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            if (err) {
                OutputDebugStringA((char*)err->GetBufferPointer());
                err->Release();
            }
            return false;
        }
        HRESULT hr = device_->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, out);
        code->Release();
        return SUCCEEDED(hr);
    }

    void release_passes() {
        auto rel = [](auto** p) { if (*p) { (*p)->Release(); *p = nullptr; } };
        rel(&edges_cs_); rel(&glow_cs_); rel(&compose_cs_);
        rel(&lut_tex_); rel(&lut_srv_); rel(&sampler_);
    }

    void release() {
        release_passes();
        auto rel = [](auto** p) { if (*p) { (*p)->Release(); *p = nullptr; } };
        rel(&edges_tex_); rel(&hue_tex_); rel(&energy_tex_);
        rel(&edges_srv_); rel(&hue_srv_); rel(&energy_srv_);
        rel(&edges_uav_); rel(&hue_uav_); rel(&energy_uav_); rel(&out_uav_);
    }

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    uint64_t sig_ = 0;
    ID3D11Texture2D* last_dst_ = nullptr;
    ID3D11Texture2D* edges_tex_ = nullptr;
    ID3D11Texture2D* hue_tex_ = nullptr;
    ID3D11Texture2D* energy_tex_ = nullptr;
    ID3D11Texture2D* lut_tex_ = nullptr;
    ID3D11ShaderResourceView* edges_srv_ = nullptr;
    ID3D11ShaderResourceView* hue_srv_ = nullptr;
    ID3D11ShaderResourceView* energy_srv_ = nullptr;
    ID3D11ShaderResourceView* lut_srv_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11UnorderedAccessView* edges_uav_ = nullptr;
    ID3D11UnorderedAccessView* hue_uav_ = nullptr;
    ID3D11UnorderedAccessView* energy_uav_ = nullptr;
    ID3D11UnorderedAccessView* out_uav_ = nullptr;
    ID3D11ComputeShader* edges_cs_ = nullptr;
    ID3D11ComputeShader* glow_cs_ = nullptr;
    ID3D11ComputeShader* compose_cs_ = nullptr;
    using pD3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                         ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    pD3DCompile compile_ = nullptr;
};

}  // namespace neon

#endif  // _WIN32
