// NEONIFY native — shared options + audio profile schema (CLI & GUI)
#pragma once
#include "neon_common.h"
#include "neon_audio.h"

namespace neon {

struct Options {
    std::string palette = "electric";
    float glow = 1.0f;
    float threshold = 0.12f;
    float env = 1.0f;
    std::string output;
    std::string profile;
    bool spatial = true;
    bool neon_audio = false;
    Advanced advanced;
    int turntable = 0;
    float azimuth = 30.0f;
    float elevation = 20.0f;
    float depth = 0.85f;
    bool hwaccel = false;
    bool export_mesh = false;
};

inline const char* PROFILE_DESCRIPTIONS(const std::string& p) {
    if (p == "fire") return "burn — crackle, rumble, flicker, heat drive";
    if (p == "ice") return "ice-steam — octave shimmer, airy shelf, glassy breath";
    if (p == "robotic") return "metal ring-mod, formant combs, crushed edges";
    if (p == "ghost") return "fog — breathing dark reverb, whisper detune";
    if (p == "void") return "the abyss — octave-down, huge dark space";
    if (p == "echo") return "proper clean ping-pong echo, tone-shaped";
    return "the signature diagonal energy sweep";
}

inline const char* const ADV_SCHEMA[][3] = {
    {"fire", "drive", "heat drive amount"},
    {"fire", "flicker_rate", "flicker rate (hz)"},
    {"fire", "flicker_depth", "flicker depth"},
    {"fire", "crackle", "crackle level"},
    {"fire", "rumble", "rumble level"},
    {"fire", "rumble_hz", "rumble cutoff (hz)"},
    {"ice", "shimmer_mix", "shimmer mix"},
    {"ice", "breath_rate", "breath rate (hz)"},
    {"ice", "breath_depth", "breath depth (ms)"},
    {"ice", "time", "frost echo time (s)"},
    {"ice", "fb", "frost echo feedback"},
    {"ice", "damp", "frost damping (hz)"},
    {"ice", "mix", "frost echo mix"},
    {"robotic", "ring_hz", "ring modulator (hz)"},
    {"robotic", "ring_mix", "ring mix"},
    {"robotic", "comb", "formant comb depth"},
    {"robotic", "bits", "crush bits"},
    {"ghost", "fog_mix", "fog mix"},
    {"ghost", "whisper_rate", "whisper rate (hz)"},
    {"ghost", "whisper_depth", "whisper depth (ms)"},
    {"ghost", "time", "far echo time (s)"},
    {"ghost", "fb", "far echo feedback"},
    {"ghost", "damp", "far damping (hz)"},
    {"ghost", "mix", "far echo mix"},
    {"void", "pitch_mix", "descent mix"},
    {"void", "mix", "abyss reverb mix"},
    {"void", "tone", "abyss tone (hz)"},
    {"void", "time", "cave echo time (s)"},
    {"void", "fb", "cave feedback"},
    {"void", "damp", "cave damping (hz)"},
    {"echo", "time", "echo time (s)"},
    {"echo", "fb", "feedback"},
    {"echo", "damp", "damping (hz)"},
    {"echo", "mix", "echo mix"},
    {"echo", "lp", "master lowpass (hz)"},
    {"slash", "gain", "sweep gain"},
};

inline float default_advanced_value(const std::string& profile, const std::string& key) {
    if (profile == "fire") {
        if (key == "drive") return 0.52f;
        if (key == "flicker_rate") return 5.5f;
        if (key == "flicker_depth") return 0.25f;
        if (key == "crackle") return 0.5f;
        if (key == "rumble") return 0.6f;
        if (key == "rumble_hz") return 90.f;
    } else if (profile == "ice") {
        if (key == "shimmer_mix") return 0.4f;
        if (key == "breath_rate") return 0.5f;
        if (key == "breath_depth") return 3.5f;
        if (key == "time") return 0.19f;
        if (key == "fb") return 0.3f;
        if (key == "damp") return 7000.f;
        if (key == "mix") return 0.28f;
    } else if (profile == "robotic") {
        if (key == "ring_hz") return 88.f;
        if (key == "ring_mix") return 0.6f;
        if (key == "comb") return 0.45f;
        if (key == "bits") return 10.f;
    } else if (profile == "ghost") {
        if (key == "fog_mix") return 0.5f;
        if (key == "whisper_rate") return 0.37f;
        if (key == "whisper_depth") return 5.f;
        if (key == "time") return 0.42f;
        if (key == "fb") return 0.42f;
        if (key == "damp") return 2600.f;
        if (key == "mix") return 0.3f;
    } else if (profile == "void") {
        if (key == "pitch_mix") return 0.45f;
        if (key == "mix") return 0.5f;
        if (key == "tone") return 1500.f;
        if (key == "time") return 0.55f;
        if (key == "fb") return 0.5f;
        if (key == "damp") return 1800.f;
    } else if (profile == "echo") {
        if (key == "time") return 0.31f;
        if (key == "fb") return 0.45f;
        if (key == "damp") return 4200.f;
        if (key == "mix") return 0.35f;
        if (key == "lp") return 9000.f;
    } else if (profile == "slash") {
        if (key == "gain") return 0.55f;
    }
    return 0.f;
}

}  // namespace neon
