// NEONIFY native — video engine: raw bgr24 pipes into x264, real-time compile %
#pragma once

#include "neon_common.h"
#include "neon_image.h"
#include "neon_audio.h"
#include <opencv2/imgproc.hpp>
#include <cstdlib>
#include <thread>
#include <atomic>

namespace neon {

struct VideoInfo {
    int w = 0, h = 0;
    float fps = 0.f;
    float dur = 0.f;
    int64_t n = 0;
    bool ok = false;
};

static std::string json_str_field(const std::string& s, const std::string& key) {
    std::string pat = "\"" + key + "\":";
    std::string::size_type p = s.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\"')) p++;
    std::string::size_type e = p;
    while (e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != '\"') e++;
    return s.substr(p, e - p);
}

inline VideoInfo probe_video(const std::string& path) {
    std::string cap;
    VideoInfo info;
    if (!run_ok({"ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=width,height,r_frame_rate,nb_frames:format=duration",
                 "-of", "json", path}, &cap))
        return info;
    info.w = std::atoi(json_str_field(cap, "width").c_str());
    info.h = std::atoi(json_str_field(cap, "height").c_str());
    std::string rate = json_str_field(cap, "r_frame_rate");
    std::string::size_type sl = rate.find('/');
    if (sl != std::string::npos) {
        float num = std::atof(rate.substr(0, sl).c_str());
        float den = std::atof(rate.substr(sl + 1).c_str());
        if (den > 0) info.fps = num / den;
    }
    info.dur = std::atof(json_str_field(cap, "duration").c_str());
    int64_t n = std::atoll(json_str_field(cap, "nb_frames").c_str());
    if (n <= 0 && info.dur > 0) n = int64_t(info.dur * info.fps);
    info.n = n;
    info.ok = info.w > 0 && info.h > 0 && info.fps > 0;
    return info;
}

inline bool has_audio_stream(const std::string& path) {
    std::string cap;
    run_ok({"ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
            "stream=codec_type", "-of", "csv=p=0", path}, &cap);
    return !cap.empty();
}

inline std::vector<std::string> ffmpeg_vsync_args() {
    std::string cap;
    run_ok({"ffmpeg", "-version"}, &cap);
    bool modern = false;
    std::string::size_type p = cap.find("ffmpeg version");
    if (p != std::string::npos) {
        float v = std::atof(cap.c_str() + p + 14);
        modern = v >= 5.0f;
    }
    if (modern) return {"-fps_mode", "vfr"};
    return {"-vsync", "vfr"};
}

inline cv::Mat apply_spatial_shift(const cv::Mat& bloom, float dx, float dy, float max_shift) {
    int h = bloom.rows, w = bloom.cols;
    cv::Mat m = (cv::Mat_<float>(2, 3) << 1.f, 0.f, dx * max_shift * w * 0.5f,
                 0.f, 1.f, dy * max_shift * h * 0.5f);
    cv::Mat out;
    cv::warpAffine(bloom, out, m, cv::Size(w, h), cv::INTER_LINEAR, cv::BORDER_REFLECT);
    return out;
}

inline std::pair<std::string, int> process_video_file(
        const std::string& inp, const std::string& out_path, const std::string& palette,
        float glow, float threshold, float env, const std::string& audio_profile,
        bool spatial_glow, bool neon_audio, const Advanced& advanced_audio,
        StageTracker* tracker) {
    VideoInfo probe = probe_video(inp);
    if (!probe.ok) throw std::runtime_error("cannot probe video: " + inp);
    int w = probe.w, h = probe.h;
    float fps = probe.fps, dur = probe.dur;
    int64_t n_frames = std::max<int64_t>(1, probe.n);
    if (w % 2 || h % 2) {
        w -= w % 2;
        h -= h % 2;
    }
    std::string tmp = "neonify_tmp_" + timestamp_suffix().substr(1);
#ifdef _WIN32
    CreateDirectoryA(tmp.c_str(), nullptr);
#else
    mkdir(tmp.c_str(), 0755);
#endif

    bool has_audio = has_audio_stream(inp);
    bool will_neon_audio = has_audio && neon_audio && !audio_profile.empty();
    std::vector<std::string> stages = {"analyze"};
    if (will_neon_audio) stages.push_back("neon audio");
    stages.push_back("neonify frames");
    stages.push_back("compile");
    if (tracker) {
        tracker->set_stages(stages);
        tracker->begin_stage(0, "analyze");
        tracker->step(0.6);
    }

    std::string audio_wav;
    std::vector<std::pair<float, float>> spatial_maps;
    if (has_audio) {
        std::vector<float> al, ar;
        if (decode_audio_stereo(inp, al, ar)) {
            if (tracker) tracker->step(0.9);
            if (spatial_glow)
                spatial_maps = spatial_energy_map(al, ar, AUDIO_SR, int(n_frames), fps);
            if (will_neon_audio) {
                if (tracker) tracker->begin_stage(1, "neon audio");
                auto on_step = [&](const std::string&, float frac) {
                    if (tracker) tracker->step(frac);
                };
                auto out = apply_audio_profile({al, ar}, AUDIO_SR, audio_profile, glow,
                                               advanced_audio, on_step);
                if (tracker) tracker->complete_stage(1);
                audio_wav = tmp + "/neon_audio.wav";
                write_wav_stereo(audio_wav, out.first, out.second);
            }
        }
    }
    if (tracker && tracker->current < 1) tracker->complete_stage(0);

    Proc rd;
    if (!rd.spawn({"ffmpeg", "-hide_banner", "-loglevel", "error", "-i", inp,
                   "-f", "rawvideo", "-pix_fmt", "bgr24", "-v", "error", "-"},
                  true, false))
        throw std::runtime_error("cannot spawn ffmpeg reader");

    std::string vsync_arg = ffmpeg_vsync_args()[1];
    std::string vsync_flag = ffmpeg_vsync_args()[0];
    std::string raw_out = tmp + "/neon_out.mp4";
    std::string size_arg = std::to_string(w) + "x" + std::to_string(h);
    std::string rate_arg = std::to_string(fps);
    std::vector<std::string> cmd = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                                    "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", size_arg,
                                    "-r", rate_arg, "-i", "-"};
    if (!audio_wav.empty()) cmd.push_back("-i"), cmd.push_back(audio_wav);
    cmd.push_back("-c:v"), cmd.push_back("libx264");
    cmd.push_back("-preset"), cmd.push_back("medium");
    cmd.push_back("-crf"), cmd.push_back("18");
    cmd.push_back("-pix_fmt"), cmd.push_back("yuv420p");
    if (!audio_wav.empty()) {
        cmd.push_back("-c:a"), cmd.push_back("aac");
        cmd.push_back("-b:a"), cmd.push_back("192k");
        cmd.push_back("-shortest");
    }
    cmd.push_back(vsync_flag), cmd.push_back(vsync_arg);
    cmd.push_back("-progress"), cmd.push_back("pipe:1");
    cmd.push_back(raw_out);
    Proc wr;
    if (!wr.spawn(cmd, true, true)) throw std::runtime_error("cannot spawn ffmpeg writer");

    std::atomic<long long> enc_ms{0};
    std::atomic<bool> enc_done{false};
    std::thread drain([&wr, &enc_ms, &enc_done]() {
        char line[256];
        while (std::fgets(line, sizeof(line), wr.out)) {
            if (std::strncmp(line, "out_time_ms=", 12) == 0)
                enc_ms = std::atoll(line + 12);
        }
        enc_done = true;
    });

    int frame_stage = will_neon_audio ? 2 : 1;
    if (tracker) tracker->begin_stage(frame_stage, "neonify frames");
    int64_t frame_no = 0;
    size_t frame_bytes = size_t(w) * h * 3;
    std::vector<unsigned char> buf(frame_bytes);
    try {
        while (true) {
            size_t got = 0;
            while (got < frame_bytes) {
                size_t n = std::fread(buf.data() + got, 1, frame_bytes - got, rd.out);
                if (n == 0) break;
                got += n;
            }
            if (got < frame_bytes) break;
            cv::Mat frame(h, w, CV_8UC3, buf.data());
            cv::Mat edges;
            EdgeAux aux;
            edge_field(frame, glow, threshold, env, edges, aux);
            cv::Mat field;
            neon_glow_stack(frame, edges, glow, env, threshold, field);
            if (!spatial_maps.empty() && frame_no < int64_t(spatial_maps.size())) {
                float dx = spatial_maps[size_t(frame_no)].first;
                float dy = spatial_maps[size_t(frame_no)].second;
                cv::Mat wide = gaussian_blur(edges, 9.0f);
                wide = apply_spatial_shift(wide, dx, dy, 0.22f);
                float addk = 0.4f * std::min(2.5f, std::max(0.2f, glow));
                field = cv::min(field + wide * addk, 1.0f);
            }
            cv::Mat out = colorize(field, palette);
            std::fwrite(out.data, 1, frame_bytes, wr.in);
            frame_no++;
            if (tracker && frame_no % 5 == 0)
                tracker->step(double(frame_no) / double(n_frames));
        }
    } catch (...) {
        if (rd.out) std::fclose(rd.out), rd.out = nullptr;
        throw;
    }
    if (rd.out) std::fclose(rd.out), rd.out = nullptr;
    rd.wait_close();
    std::fflush(wr.in);
    std::fclose(wr.in);
    wr.in = nullptr;

    if (tracker) tracker->complete_stage(frame_stage);
    if (tracker) {
        tracker->begin_stage(int(stages.size()) - 1, "compile");
        while (!enc_done) {
            long long ms = enc_ms.load();
            if (dur > 0 && ms > 0)
                tracker->step(std::min(1.0, (double(ms) / 1e6) / double(dur)));
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        long long ms = enc_ms.load();
        if (dur > 0 && ms > 0)
            tracker->step(std::min(1.0, (double(ms) / 1e6) / double(dur)));
    }
    drain.join();
    std::fclose(wr.out);
    wr.out = nullptr;
    wr.wait_close();

    std::string finalp;
    if (file_exists(raw_out)) {
        finalp = unique_output_path(out_path);
        std::remove(finalp.c_str());
        int code = 0;
#ifdef _WIN32
        if (!MoveFileExA(raw_out.c_str(), finalp.c_str(), MOVEFILE_REPLACE_EXISTING)) code = 1;
#else
        code = int(std::rename(raw_out.c_str(), finalp.c_str()));
#endif
        if (code != 0) throw std::runtime_error("video encode failed: rename " + inp);
    } else {
        throw std::runtime_error("video encode failed: " + inp);
    }
    if (tracker) {
        tracker->complete_stage(int(stages.size()) - 1);
        tracker->finish();
    }
    remove_tree(tmp);
    return {finalp, int(frame_no)};
}

}  // namespace neon
