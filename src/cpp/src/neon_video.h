// NEONIFY native — video engine: raw bgr24 pipes into the encoder, real-time
// compile %. the encoder comes from the ini (probed at first run): nvenc/qsv/
// amf/vaapi when the machine proved it, libx264 otherwise.
#pragma once

#include "neon_common.h"
#include "neon_image.h"
#include "neon_audio.h"
#include "neon_hw.h"
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

inline const std::vector<std::string>& ffmpeg_vsync_args() {
    static std::vector<std::string> args = [] {
        std::string cap;
        run_ok({"ffmpeg", "-nostdin", "-version"}, &cap);
        bool modern = false;
        std::string::size_type p = cap.find("ffmpeg version");
        if (p != std::string::npos) {
            float v = std::atof(cap.c_str() + p + 14);
            modern = v >= 5.0f;
        }
        if (modern) return std::vector<std::string>{"-fps_mode", "vfr"};
        return std::vector<std::string>{"-vsync", "vfr"};
    }();
    return args;
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
        StageTracker* tracker, int inside_mode = INSIDE_WIPE, bool hwaccel = false) {
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
    make_dir(tmp);

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
    trace("video: audio stage done");

    std::string enc = resolved_video_encoder(app_ini());
    bool hw_decode = hwaccel || app_ini().get_bool("video", "hwdecode", false);
    trace("video: probe ok");

    Proc rd;
    std::vector<std::string> rd_cmd = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error"};
    if (hw_decode) rd_cmd.push_back("-hwaccel"), rd_cmd.push_back("auto");
    rd_cmd.push_back("-i"), rd_cmd.push_back(inp);
    rd_cmd.push_back("-f"), rd_cmd.push_back("rawvideo");
    rd_cmd.push_back("-pix_fmt"), rd_cmd.push_back("bgr24");
    rd_cmd.push_back("-v"), rd_cmd.push_back("error");
    rd_cmd.push_back("-");
    if (!rd.spawn(rd_cmd, true, false, true))
        throw std::runtime_error("cannot spawn ffmpeg reader");

    const std::vector<std::string>& vsync = ffmpeg_vsync_args();
    std::string raw_out = tmp + "/neon_out.mp4";
    std::string size_arg = std::to_string(w) + "x" + std::to_string(h);
    std::string rate_arg = std::to_string(fps);
    std::vector<std::string> cmd = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                                    "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", size_arg,
                                    "-r", rate_arg, "-i", "-"};
    if (!audio_wav.empty()) cmd.push_back("-i"), cmd.push_back(audio_wav);
    video_encoder_args(enc, cmd);
    cmd.push_back("-pix_fmt"), cmd.push_back("yuv420p");
    if (!audio_wav.empty()) {
        cmd.push_back("-c:a"), cmd.push_back("aac");
        cmd.push_back("-b:a"), cmd.push_back("192k");
        cmd.push_back("-shortest");
    }
    cmd.push_back(vsync[0]), cmd.push_back(vsync[1]);
    cmd.push_back("-progress"), cmd.push_back("pipe:1");
    cmd.push_back(raw_out);
    Proc wr;
    if (!wr.spawn(cmd, true, true, true)) throw std::runtime_error("cannot spawn ffmpeg writer");
    trace("video: pipes up");

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
    // the frame loop is the one spot without a bounded wait inside — fread
    // blocks until the reader speaks. a watchdog thread watches activity and
    // kills both children when nothing moves for 120s, so a frozen reader
    // surfaces as an error instead of an eternal "compiling"
    auto now_ms = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    std::atomic<long long> last_act{now_ms()};
    std::atomic<bool> wd_stop{false};
    std::atomic<bool> wd_fired{false};
    std::thread watchdog([&] {
        while (!wd_stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (wd_stop.load(std::memory_order_relaxed)) break;
            if (now_ms() - last_act.load(std::memory_order_relaxed) > 120000) {
                wd_fired = true;
                rd.kill();
                wr.kill();
            }
        }
    });
    try {
        while (true) {
            size_t got = 0;
            while (got < frame_bytes) {
                size_t n = std::fread(buf.data() + got, 1, frame_bytes - got, rd.out);
                if (n == 0) break;
                got += n;
                last_act = now_ms();
            }
            if (got < frame_bytes) break;
            cv::Mat frame(h, w, CV_8UC3, buf.data());
            cv::Mat edges;
            EdgeAux aux;
            cv::Mat ang;
            edge_field(frame, glow, threshold, env, edges, aux, &ang);
            cv::Mat field;
            neon_glow_stack(edges, glow, env, field);
            if (!spatial_maps.empty() && frame_no < int64_t(spatial_maps.size())) {
                float dx = spatial_maps[size_t(frame_no)].first;
                float dy = spatial_maps[size_t(frame_no)].second;
                cv::Mat wide = gaussian_blur(edges, 9.0f);
                wide = apply_spatial_shift(wide, dx, dy, 0.22f);
                float addk = 0.4f * std::min(2.5f, std::max(0.2f, glow));
                field = cv::min(field + wide * addk, 1.0f);
            }
            cv::Mat out = (palette == "spectrum") ? spectrum_colorize(field, ang)
                                                  : colorize(field, palette);
            cv::add(out, neon_core_u8(edges), out);
            if (inside_mode != INSIDE_WIPE) keep_inside_composite(out, frame, edges, field, inside_mode);
            // a dead encoder must surface here, not as a silent short file
            if (std::fwrite(out.data, 1, frame_bytes, wr.in) != frame_bytes) {
                std::string tail = wr.stderr_tail();
                throw std::runtime_error("video encoder stopped accepting frames: " +
                                         (tail.empty() ? out_path : tail));
            }
            frame_no++;
            last_act = now_ms();
            if (tracker && frame_no % 5 == 0)
                tracker->step(double(frame_no) / double(n_frames),
                              "frame " + std::to_string(frame_no) + "/" + std::to_string(n_frames));
        }
    } catch (...) {
        wd_stop = true;
        watchdog.join();
        rd.kill();
        wr.kill();
        if (rd.out) std::fclose(rd.out), rd.out = nullptr;
        if (wr.in) std::fclose(wr.in), wr.in = nullptr;
        drain.join();
        throw;
    }
    wd_stop = true;
    watchdog.join();
    if (wd_fired)
        throw std::runtime_error("video pipeline stalled (no activity for 120s) — children killed");
    trace("video: frames done");
    if (rd.out) std::fclose(rd.out), rd.out = nullptr;
    rd.wait_close(15000);
    trace("video: reader reaped");
    if (rd.exit_code != 0 && frame_no > 0)
        throw std::runtime_error("video reader failed mid-stream: " + rd.stderr_tail());
    std::fflush(wr.in);
    std::fclose(wr.in);
    wr.in = nullptr;

    if (tracker) tracker->complete_stage(frame_stage);
    trace("video: compile wait");
    if (tracker) {
        tracker->begin_stage(int(stages.size()) - 1, "compile");
        auto last_move = std::chrono::steady_clock::now();
        long long last_ms = -1;
        bool stalled = false;
        while (!enc_done) {
            long long ms = enc_ms.load();
            if (ms != last_ms) {
                last_ms = ms;
                last_move = std::chrono::steady_clock::now();
            } else if (std::chrono::duration<double>(std::chrono::steady_clock::now() - last_move).count() > 90.0) {
                // a wedged encoder never recovers — kill it so the drain
                // thread sees EOF and this loop can leave
                stalled = true;
                wr.kill();
                break;
            }
            if (dur > 0 && ms > 0)
                tracker->step(std::min(1.0, (double(ms) / 1e6) / double(dur)), "compile");
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        long long ms = enc_ms.load();
        if (dur > 0 && ms > 0)
            tracker->step(std::min(1.0, (double(ms) / 1e6) / double(dur)), "compile");
        drain.join();
        std::fclose(wr.out);
        wr.out = nullptr;
        wr.wait_close(30000);
        trace("video: writer reaped");
        if (stalled)
            throw std::runtime_error("video encoder stalled and was killed: " +
                                     (wr.stderr_tail().empty() ? out_path : wr.stderr_tail()));
        if (wr.exit_code != 0)
            throw std::runtime_error("video encode failed: " + wr.stderr_tail());
    } else {
        drain.join();
        std::fclose(wr.out);
        wr.out = nullptr;
        wr.wait_close(30000);
    }

    std::string finalp;
    if (file_exists(raw_out)) {
        finalp = unique_output_path(out_path);
        remove_file(finalp);
        if (!move_file(raw_out, finalp))
            throw std::runtime_error("video encode failed: rename " + finalp);
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
