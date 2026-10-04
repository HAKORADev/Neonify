// NEONIFY native — the settings ini shared by the gui and the engines. layout
// law: every value carries two comment lines, the first says what it is, the
// second says the valid range. a checker validates
// every load: missing, corrupt or extra data falls back to defaults without
// wiping the file — only the broken values are rewritten, user-set values and
// their comments survive. deleting the file makes the next run detect the
// hardware again and start a fresh one.
#pragma once

#include "neon_common.h"
#include <cctype>

namespace neon {

struct IniField {
    const char* section;
    const char* key;
    const char* what;    // comment line 1: what the value does
    const char* valid;   // comment line 2: "0|1", "auto|gpu|cpu", "0.2-3", "text"
    const char* dflt;
};

inline bool ini_field_valid(const std::string& value, const std::string& valid) {
    if (valid == "text") return !value.empty();
    size_t bar = valid.find('|');
    if (bar != std::string::npos) {
        size_t start = 0;
        while (start <= valid.size()) {
            size_t end = valid.find('|', start);
            std::string opt = valid.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (value == opt) return true;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return false;
    }
    size_t dash = valid.find('-');
    if (dash != std::string::npos) {
        bool integer = (valid.rfind("int ", 0) == 0);
        std::string range = integer ? valid.substr(4) : valid;
        dash = range.find('-');
        double lo = std::atof(range.substr(0, dash).c_str());
        double hi = std::atof(range.c_str() + dash + 1);
        char* endp = nullptr;
        double v = std::strtod(value.c_str(), &endp);
        if (endp != value.c_str() && *endp == '\0' && v >= lo && v <= hi) {
            if (integer && value.find('.') != std::string::npos) return false;
            return true;
        }
        return false;
    }
    return !value.empty();
}

inline std::string ini_path() {
    return exe_dir() + "/neonify.ini";
}

class IniFile {
public:
    explicit IniFile(std::string path) : path_(std::move(path)) {}

    // load + validate + repair. created/true when the file was missing, and
    // repaired/true when corrupt or extra entries were fixed in place.
    bool load(const std::vector<IniField>& schema, bool* created = nullptr, bool* repaired = nullptr) {
        bool existed = file_exists(path_);
        std::map<std::pair<std::string, std::string>, std::string> raw;
        if (existed) {
            std::ifstream f = open_ifstream(path_);
            std::string line, section;
            while (std::getline(f, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                std::string t = line;
                while (!t.empty() && (t[0] == ' ' || t[0] == '\t')) t.erase(t.begin());
                if (t.empty() || t[0] == '#' || t[0] == ';') continue;
                if (t[0] == '[') {
                    auto close = t.find(']');
                    if (close != std::string::npos) section = t.substr(1, close - 1);
                    continue;
                }
                auto eq = t.find('=');
                if (eq == std::string::npos || section.empty()) continue;
                raw[{section, t.substr(0, eq)}] = t.substr(eq + 1);
            }
        }
        bool changed = !existed;
        values_.clear();
        for (const IniField& f : schema) {
            auto key = std::make_pair(std::string(f.section), std::string(f.key));
            auto it = raw.find(key);
            std::string value;
            if (it == raw.end()) {
                value = f.dflt;
                changed = true;
            } else {
                raw.erase(it);
                if (ini_field_valid(it->second, f.valid)) value = it->second;
                else {
                    value = f.dflt;
                    changed = true;
                }
            }
            values_[key] = value;
        }
        if (!raw.empty()) changed = true;  // extra data never survives a load
        if (changed) save(schema);
        if (created) *created = !existed;
        if (repaired) *repaired = existed && changed;
        return true;
    }

    bool save(const std::vector<IniField>& schema) const {
        std::string out;
        out += "# neonify settings — hardware facts and per-step choices\n";
        out += "# every value carries two lines: what it is, then the valid range\n";
        out += "# delete this file and the next run re-detects the hardware fresh\n";
        std::string current_section;
        for (const IniField& f : schema) {
            if (std::string(f.section) != current_section) {
                current_section = f.section;
                out += std::string("\n[") + f.section + "]\n";
            }
            out += std::string("\n# ") + f.what + "\n";
            out += std::string("# valid: ") + f.valid + "\n";
            out += std::string(f.key) + "=" + get(f.section, f.key, f.dflt) + "\n";
        }
        std::ofstream f = open_ofstream(path_, std::ios::binary | std::ios::trunc);
        if (!f.good()) return false;
        f.write(out.data(), std::streamsize(out.size()));
        return f.good();
    }

    std::string get(const std::string& section, const std::string& key, const std::string& dflt = "") const {
        auto it = values_.find({section, key});
        return it == values_.end() ? dflt : it->second;
    }
    void set(const std::string& section, const std::string& key, const std::string& value) {
        values_[{section, key}] = value;
    }
    int get_int(const std::string& section, const std::string& key, int dflt = 0) const {
        std::string v = get(section, key);
        return v.empty() ? dflt : std::atoi(v.c_str());
    }
    float get_float(const std::string& section, const std::string& key, float dflt = 0.f) const {
        std::string v = get(section, key);
        return v.empty() ? dflt : float(std::atof(v.c_str()));
    }
    bool get_bool(const std::string& section, const std::string& key, bool dflt = false) const {
        std::string v = get(section, key);
        if (v.empty()) return dflt;
        return v == "1" || v == "true" || v == "yes";
    }

private:
    std::string path_;
    std::map<std::pair<std::string, std::string>, std::string> values_;
};

}  // namespace neon
