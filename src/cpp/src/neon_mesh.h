// NEONIFY native — 3d engine: real geometry, real remesh, software neon renderer
#pragma once

#include "neon_common.h"
#include "neon_image.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <set>
#include <tuple>

namespace neon {

struct Mesh {
    std::vector<cv::Vec3f> verts;
    std::vector<cv::Vec3i> faces;
};

inline bool looks_ascii(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    char buf[1024];
    f.read(buf, sizeof(buf));
    auto n = f.gcount();
    for (std::streamsize i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c > 0x7e) return false;
    }
    return true;
}

inline Mesh load_mesh(const std::string& path) {
    Mesh m;
    std::string ext = lower_ext(path);
    if (ext == ".obj") {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.size() > 2 && line[0] == 'v' && line[1] == ' ') {
                cv::Vec3f v;
                if (std::sscanf(line.c_str(), "v %f %f %f", &v[0], &v[1], &v[2]) == 3)
                    m.verts.push_back(v);
            } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
                std::vector<int> idx;
                std::istringstream ss(line.substr(2));
                std::string tok;
                while (ss >> tok) {
                    int i = std::atoi(tok.c_str()) - 1;
                    idx.push_back(i);
                }
                for (size_t k = 1; k + 1 < idx.size(); k++)
                    m.faces.emplace_back(idx[0], idx[k], idx[k + 1]);
            }
        }
    } else if (ext == ".stl") {
        std::ifstream f(path, std::ios::binary);
        char head[5] = {0};
        f.read(head, 5);
        f.seekg(0);
        if (std::string(head) == "solid" && looks_ascii(path)) {
            std::string line;
            float tri[3][3];
            int nt = 0;
            while (std::getline(f, line)) {
                float x, y, z;
                if (std::sscanf(line.c_str(), " vertex %f %f %f", &x, &y, &z) == 3) {
                    tri[nt][0] = x; tri[nt][1] = y; tri[nt][2] = z;
                    nt++;
                    if (nt == 3) {
                        int base = int(m.verts.size());
                        for (auto& t : tri) m.verts.emplace_back(t[0], t[1], t[2]);
                        m.faces.emplace_back(base, base + 1, base + 2);
                        nt = 0;
                    }
                }
            }
        } else {
            f.seekg(80);
            uint32_t n = 0;
            f.read((char*)&n, 4);
            n = std::min<uint32_t>(n, 2000000);
            for (uint32_t i = 0; i < n; i++) {
                char rec[50];
                f.read(rec, 50);
                if (!f) break;
                const float* vals = (const float*)rec;
                int base = int(m.verts.size());
                m.verts.emplace_back(vals[3], vals[4], vals[5]);
                m.verts.emplace_back(vals[6], vals[7], vals[8]);
                m.verts.emplace_back(vals[9], vals[10], vals[11]);
                m.faces.emplace_back(base, base + 1, base + 2);
            }
        }
    } else if (ext == ".ply") {
        std::ifstream f(path, std::ios::binary);
        std::string header, ln;
        while (std::getline(f, ln)) {
            header += ln + "\n";
            if (ln.find("end_header") != std::string::npos) break;
            if (header.size() > 65536) throw std::runtime_error("bad ply");
        }
        int nv = 0, nf = 0;
        bool binary = header.find("binary_little_endian") != std::string::npos;
        {
            std::istringstream ss(header);
            while (std::getline(ss, ln)) {
                if (ln.rfind("element vertex", 0) == 0) nv = std::atoi(ln.c_str() + 14);
                else if (ln.rfind("element face", 0) == 0) nf = std::atoi(ln.c_str() + 12);
            }
        }
        if (binary) {
            auto props = [](const std::string& h, const std::string& el) {
                int count = 0;
                std::istringstream ss(h);
                std::string t;
                bool in_elem = false;
                while (ss >> t) {
                    if (t == "element") {
                        std::string name;
                        int cnt = 0;
                        ss >> name >> cnt;
                        in_elem = (name == el);
                        continue;
                    }
                    if (in_elem && (t == "float" || t == "float32")) count++;
                }
                return count;
            };
            int vp = props(header, "vertex");
            for (int i = 0; i < nv; i++) {
                float v[3] = {0, 0, 0};
                for (int k = 0; k < vp; k++) {
                    float val = 0;
                    f.read((char*)&val, 4);
                    if (k < 3) v[k] = val;
                }
                m.verts.emplace_back(v[0], v[1], v[2]);
            }
            for (int i = 0; i < nf; i++) {
                unsigned char cnt = 0;
                f.read((char*)&cnt, 1);
                std::vector<int> idx;
                for (int k = 0; k < cnt; k++) {
                    int32_t vi = 0;
                    f.read((char*)&vi, 4);
                    idx.push_back(vi);
                }
                for (size_t k = 1; k + 1 < idx.size(); k++)
                    m.faces.emplace_back(idx[0], idx[k], idx[k + 1]);
            }
        } else {
            for (int i = 0; i < nv; i++) {
                std::getline(f, ln);
                cv::Vec3f v;
                std::sscanf(ln.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
                m.verts.push_back(v);
            }
            for (int i = 0; i < nf; i++) {
                std::getline(f, ln);
                std::istringstream ss(ln);
                int cnt = 0;
                ss >> cnt;
                std::vector<int> idx(cnt);
                for (int k = 0; k < cnt; k++) ss >> idx[k];
                for (size_t k = 1; k + 1 < idx.size(); k++)
                    m.faces.emplace_back(idx[0], idx[k], idx[k + 1]);
            }
        }
    } else {
        throw std::runtime_error("unsupported mesh: " + path);
    }
    if (m.verts.empty() || m.faces.empty()) throw std::runtime_error("empty mesh: " + path);
    return m;
}

inline std::vector<cv::Vec2i> mesh_edges(const std::vector<cv::Vec3i>& faces) {
    std::set<std::pair<int, int>> e;
    for (const auto& tri : faces) {
        int a = tri[0], b = tri[1], c = tri[2];
        e.insert({std::min(a, b), std::max(a, b)});
        e.insert({std::min(b, c), std::max(b, c)});
        e.insert({std::min(c, a), std::max(c, a)});
    }
    std::vector<cv::Vec2i> out;
    out.reserve(e.size());
    for (const auto& pr : e) out.emplace_back(pr.first, pr.second);
    return out;
}

inline void image_relief_mesh(const cv::Mat& img_bgr, int grid, float depth, Mesh& out) {
    cv::Mat lum = luminance(img_bgr);
    int h_gt = img_bgr.rows, w_gt = img_bgr.cols;
    int gh = std::max(10, int(float(grid) * h_gt / float(w_gt)));
    cv::Mat sm;
    cv::resize(lum, sm, cv::Size(grid, gh), 0, 0, cv::INTER_AREA);
    cv::GaussianBlur(sm, sm, cv::Size(0, 0), std::max(1.2, grid * 0.02));
    int h = sm.rows, w = sm.cols;
    out.verts.clear();
    out.faces.clear();
    out.verts.reserve(size_t(w) * h);
    for (int y = 0; y < h; y++) {
        float yy = -1.0f + 2.0f * float(y) / float(std::max(1, h - 1));
        for (int x = 0; x < w; x++) {
            float xx = -float(w) / float(std::max(1, h)) + 2.0f * float(w) / float(std::max(1, h)) * float(x) / float(std::max(1, w - 1));
            float z = (sm.at<float>(y, x) / 255.0f - 0.5f) * 2.0f * depth;
            out.verts.emplace_back(xx, yy, z);
        }
    }
    for (int r = 0; r < h - 1; r++) {
        for (int c = 0; c < w - 1; c++) {
            int i0 = r * w + c;
            out.faces.emplace_back(i0, i0 + 1, i0 + w);
            out.faces.emplace_back(i0 + 1, i0 + w + 1, i0 + w);
        }
    }
}

inline void normalize_mesh(Mesh& m) {
    cv::Vec3f lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (const auto& v : m.verts)
        for (int k = 0; k < 3; k++) {
            lo[k] = std::min(lo[k], v[k]);
            hi[k] = std::max(hi[k], v[k]);
        }
    cv::Vec3f center = (lo + hi) / 2;
    float scale = 1e-6f;
    for (int k = 0; k < 3; k++) scale = std::max(scale, hi[k] - lo[k]);
    for (auto& v : m.verts) v = (v - center) / scale;
}

inline cv::Vec3f rot_y(const cv::Vec3f& v, float ang) {
    float c = std::cos(ang), s = std::sin(ang);
    return {c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]};
}

inline cv::Vec3f rot_x(const cv::Vec3f& v, float ang) {
    float c = std::cos(ang), s = std::sin(ang);
    return {v[0], c * v[1] - s * v[2], s * v[1] + c * v[2]};
}

inline cv::Mat render_neon_mesh(const Mesh& mesh, int W = 960, int H = 720, float yaw = 0.f,
                                float pitch = 0.2f, const std::string& palette = "electric",
                                float glow = 1.0f, float fov = 1.4f) {
    size_t nv = mesh.verts.size();
    std::vector<cv::Vec3f> v(nv);
    for (size_t i = 0; i < nv; i++) v[i] = rot_x(rot_y(mesh.verts[i], yaw), pitch);
    std::vector<float> z(nv);
    std::vector<cv::Vec2f> pts(nv);
    float zmin = 1e9f, zmax = -1e9f;
    for (size_t i = 0; i < nv; i++) {
        z[i] = v[i][2] + 2.6f;
        pts[i] = {(v[i][0] / (z[i] * 0.45f)) * W * 0.5f * fov + W / 2.f,
                  (-v[i][1] / (z[i] * 0.45f)) * H * 0.5f * fov + H / 2.f};
        zmin = std::min(zmin, z[i]);
        zmax = std::max(zmax, z[i]);
    }
    auto edges = mesh_edges(mesh.faces);
    std::vector<size_t> order(edges.size());
    std::vector<float> zmean(edges.size());
    for (size_t i = 0; i < edges.size(); i++) {
        order[i] = i;
        zmean[i] = (z[edges[i][0]] + z[edges[i][1]]) * 0.5f;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return zmean[a] > zmean[b]; });
    cv::Mat canvas = cv::Mat::zeros(H, W, CV_32F);
    float zrange = std::max(1e-5f, zmax - zmin);
    for (size_t ei : order) {
        float depth_norm = (zmean[ei] - zmin) / zrange;
        float inten = std::min(1.1f, std::max(0.05f, (1.0f - 0.72f * depth_norm) * 1.05f));
        cv::Vec2f a2 = pts[edges[ei][0]], b2 = pts[edges[ei][1]];
        cv::line(canvas, cv::Point(cvRound(a2[0]), cvRound(a2[1])),
                 cv::Point(cvRound(b2[0]), cvRound(b2[1])), inten, 1, cv::LINE_AA);
    }
    cv::Mat field;
    neon_glow_stack(cv::Mat(), canvas, glow, 1.0f, 0.12f, field);
    return colorize(field, palette);
}

inline bool render_turntable_pipe(const std::string& out_path, const Mesh& mesh, int n_frames,
                                  float azimuth, float elevation, const std::string& palette,
                                  float glow, int fps, int W = 960, int H = 720,
                                  const std::function<void(int)>& on_progress = nullptr) {
    Proc proc;
    std::vector<std::string> cmd = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                                    "-f", "rawvideo", "-pix_fmt", "bgr24",
                                    "-s", std::to_string(W) + "x" + std::to_string(H),
                                    "-r", std::to_string(fps), "-i", "-",
                                    "-c:v", "libx264", "-preset", "medium", "-crf", "18",
                                    "-pix_fmt", "yuv420p", "-progress", "pipe:1", out_path};
    if (!proc.spawn(cmd, false, true)) return false;
    float base_pitch = elevation * float(M_PI) / 180.f;
    float base_yaw = azimuth * float(M_PI) / 180.f;
    try {
        for (int i = 0; i < n_frames; i++) {
            float yaw = base_yaw + 2.f * float(M_PI) * float(i) / float(std::max(1, n_frames));
            cv::Mat frame = render_neon_mesh(mesh, W, H, yaw, base_pitch, palette, glow);
            std::fwrite(frame.data, 1, size_t(W) * H * 3, proc.in);
            if (on_progress) on_progress(i + 1);
        }
        std::fflush(proc.in);
        proc.wait_close();
    } catch (...) {
        proc.wait_close();
        return false;
    }
    return proc.exit_code == 0;
}

inline std::string neonize_mesh_file(const std::string& inp, const std::string& out_path,
                                     const std::string& palette, float glow, int turntable,
                                     float azimuth, float elevation, StageTracker* tracker) {
    Mesh mesh = load_mesh(inp);
    normalize_mesh(mesh);
    if (tracker) {
        tracker->set_stages({"load", "render", "write"});
        tracker->begin_stage(0, "load");
        tracker->step(1.0);
        tracker->complete_stage(0);
        tracker->begin_stage(1, "render");
    }
    std::string finalp = unique_output_path(out_path);
    if (turntable > 0) {
        auto cb = [&](int f) { if (tracker) tracker->step(double(f) / double(turntable)); };
        if (!render_turntable_pipe(finalp, mesh, turntable, azimuth, elevation, palette, glow, 30, 960, 720, cb))
            throw std::runtime_error("turntable encode failed");
    } else {
        cv::Mat img = render_neon_mesh(mesh, 960, 720, azimuth * float(M_PI) / 180.f,
                                       elevation * float(M_PI) / 180.f, palette, glow);
        cv::imwrite(finalp, img, {cv::IMWRITE_PNG_COMPRESSION, 6});
    }
    if (tracker) {
        tracker->complete_stage(1);
        tracker->begin_stage(2, "write");
        tracker->step(1.0);
        tracker->complete_stage(2);
        tracker->finish();
    }
    return finalp;
}

inline std::string neonize_relief_file(const std::string& inp, const std::string& out_path,
                                       const std::string& palette, float glow, float depth,
                                       int turntable, float azimuth, float elevation,
                                       StageTracker* tracker) {
    cv::Mat img = cv::imread(inp, cv::IMREAD_COLOR);
    if (img.empty()) throw std::runtime_error("cannot read image: " + inp);
    Mesh mesh;
    image_relief_mesh(img, 110, depth, mesh);
    normalize_mesh(mesh);
    if (tracker) {
        tracker->set_stages({"remesh", "render", "write"});
        tracker->begin_stage(0, "remesh");
        tracker->step(1.0);
        tracker->complete_stage(0);
        tracker->begin_stage(1, "render");
    }
    std::string finalp = unique_output_path(out_path);
    if (turntable > 0) {
        auto cb = [&](int f) { if (tracker) tracker->step(double(f) / double(turntable)); };
        if (!render_turntable_pipe(finalp, mesh, turntable, azimuth, elevation, palette, glow, 24, 960, 720, cb))
            throw std::runtime_error("relief turntable encode failed");
    } else {
        cv::Mat out = render_neon_mesh(mesh, 960, 720, azimuth * float(M_PI) / 180.f,
                                       elevation * float(M_PI) / 180.f, palette, glow);
        cv::imwrite(finalp, out);
    }
    if (tracker) {
        tracker->complete_stage(1);
        tracker->begin_stage(2, "write");
        tracker->step(1.0);
        tracker->complete_stage(2);
        tracker->finish();
    }
    return finalp;
}

}  // namespace neon
