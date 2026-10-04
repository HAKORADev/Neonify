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
    bool next_to_input = false;
    int inside_mode = 0;  // 0 wipe, 1 keep original under neon, 2 global glow
    std::string profile;
    bool spatial = true;
    bool neon_audio = false;
    int audio_intensity = 1;  // 1 normal, 2 high (x2), 4 extreme (x4)
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

// the audio naming law: the tag carries the real applied profile — and when
// the intensity left normal, the file says so: echo_x2, echo_x4
inline int intensity_from_name(const std::string& word) {
    if (word == "high" || word == "x2" || word == "2") return 2;
    if (word == "extreme" || word == "x4" || word == "4") return 4;
    return 1;
}

inline std::string audio_tag(const std::string& profile, int intensity) {
    if (intensity >= 4) return profile + "_x4";
    if (intensity == 2 || intensity == 3) return profile + "_x2";
    return profile;
}

// 1:1 with python AUDIO_ADVANCED_SCHEMA: profile, key, label, lo, hi, default
inline const char* const ADV_SCHEMA[][6] = {
    {"fire", "drive", "heat drive amount", "0", "1", "0.52"},
    {"fire", "flicker_rate", "flicker rate (hz)", "1", "12", "5.5"},
    {"fire", "flicker_depth", "flicker depth", "0", "0.6", "0.25"},
    {"fire", "crackle", "crackle level", "0", "1.5", "0.5"},
    {"fire", "rumble", "rumble level", "0", "1.5", "0.6"},
    {"fire", "rumble_hz", "rumble cutoff (hz)", "40", "160", "90"},
    {"ice", "shimmer_mix", "shimmer mix", "0", "1", "0.4"},
    {"ice", "breath_rate", "breath rate (hz)", "0.1", "2", "0.5"},
    {"ice", "breath_depth", "breath depth (ms)", "0", "10", "3.5"},
    {"ice", "time", "frost echo time (s)", "0.05", "1", "0.19"},
    {"ice", "fb", "frost echo feedback", "0", "0.9", "0.3"},
    {"ice", "damp", "frost damping (hz)", "1000", "12000", "7000"},
    {"ice", "mix", "frost echo mix", "0", "1", "0.28"},
    {"robotic", "ring_hz", "ring modulator (hz)", "40", "220", "88"},
    {"robotic", "ring_mix", "ring mix", "0", "1", "0.6"},
    {"robotic", "comb", "formant comb depth", "0", "1", "0.45"},
    {"robotic", "bits", "crush bits", "6", "16", "10"},
    {"ghost", "fog_mix", "fog mix", "0", "1", "0.5"},
    {"ghost", "whisper_rate", "whisper rate (hz)", "0.1", "2", "0.37"},
    {"ghost", "whisper_depth", "whisper depth (ms)", "0", "12", "5"},
    {"ghost", "time", "far echo time (s)", "0.05", "1.5", "0.42"},
    {"ghost", "fb", "far echo feedback", "0", "0.9", "0.42"},
    {"ghost", "damp", "far damping (hz)", "500", "8000", "2600"},
    {"ghost", "mix", "far echo mix", "0", "1", "0.3"},
    {"void", "pitch_mix", "descent mix", "0", "1", "0.45"},
    {"void", "mix", "abyss reverb mix", "0", "1", "0.5"},
    {"void", "tone", "abyss tone (hz)", "500", "6000", "1500"},
    {"void", "time", "cave echo time (s)", "0.05", "1.5", "0.55"},
    {"void", "fb", "cave feedback", "0", "0.9", "0.5"},
    {"void", "damp", "cave damping (hz)", "500", "8000", "1800"},
    {"echo", "time", "echo time (s)", "0.05", "1.5", "0.31"},
    {"echo", "fb", "feedback", "0", "0.9", "0.45"},
    {"echo", "damp", "damping (hz)", "500", "12000", "4200"},
    {"echo", "mix", "echo mix", "0", "1", "0.35"},
    {"echo", "lp", "master lowpass (hz)", "1000", "16000", "9000"},
    {"slash", "gain", "sweep gain", "0", "2", "0.55"},
};

inline const int ADV_SCHEMA_ROWS = int(sizeof(ADV_SCHEMA) / sizeof(ADV_SCHEMA[0]));

inline float adv_lo(int row) { return float(std::atof(ADV_SCHEMA[row][3])); }
inline float adv_hi(int row) { return float(std::atof(ADV_SCHEMA[row][4])); }
inline float adv_default(int row) { return float(std::atof(ADV_SCHEMA[row][5])); }

inline float default_advanced_value(const std::string& profile, const std::string& key) {
    for (int i = 0; i < ADV_SCHEMA_ROWS; i++)
        if (profile == ADV_SCHEMA[i][0] && key == ADV_SCHEMA[i][1])
            return adv_default(i);
    return 0.f;
}

// default output for an input: results/ (flat) or next to the input file
inline std::string default_output_for(const std::string& inp, const std::string& tag,
                                      const std::string& ext, bool next_to_input) {
    std::string name = out_default(inp, tag, ext);
    if (!next_to_input) return results_path(name);
    std::string::size_type slash = inp.find_last_of("/\\");
    std::string dir = (slash == std::string::npos) ? std::string(".") : inp.substr(0, slash);
    if (dir.empty()) dir = ".";
    return dir + "/" + name;
}

}  // namespace neon
