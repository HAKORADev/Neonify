// NEONIFY native — desktop neonifier capture. one frame source per platform:
//   windows  — dxgi desktop duplication (the same low-level path games and
//              recorders use), frames land as bgra textures on our d3d11 device
//   linux/x11 — xshm root-window grabs with an xgetimage fallback
//   linux/wayland — xdg-desktop-portal screencast over dbus (sd-bus) feeding a
//              pipewire stream; works on gnome, kde and wlroots compositors
// every backend reports bgr mats plus, on windows, the raw gpu texture so the
// overlay never round-trips through the cpu when it does not have to.
#pragma once

#include "neon_common.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#ifdef NEONIFY_WITH_PORTAL
// c headers stay outside the namespace — including them inside neon would
// namespace their symbols and break the abi against the real libraries
#include <systemd/sd-bus.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#endif

namespace neon {

inline uint64_t now_us() {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}

class ScreenCapture {
public:
    virtual ~ScreenCapture() = default;
    virtual bool start() = 0;
    virtual bool grab(cv::Mat& bgr, uint64_t& ts_us) = 0;   // blocks up to ~50ms
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual std::string name() const = 0;
    virtual void stop() {}
#ifdef _WIN32
    virtual ID3D11Device* d3d_device() { return nullptr; }
    virtual ID3D11DeviceContext* d3d_context() { return nullptr; }
    // gpu path: returns our own copy of the newest frame as a bgra texture
    virtual bool grab_gpu(ID3D11Texture2D** tex, uint64_t& ts_us) { return false; }
#endif
};

#ifdef _WIN32

class DxgiCapture : public ScreenCapture {
public:
    ~DxgiCapture() override { stop(); }

    bool start() override {
        stop();
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory_);
        if (FAILED(hr)) return false;
        idx_ = monitor_index_ < 0 ? 0 : monitor_index_;
        IDXGIAdapter1* adapter = nullptr;
        if (FAILED(factory_->EnumAdapters1(0, &adapter))) return false;
        if (FAILED(adapter->EnumOutputs(idx_, &output_))) {
            adapter->Release();
            if (idx_ == 0) return false;
            idx_ = 0;
            if (FAILED(factory_->EnumAdapters1(0, &adapter))) return false;
            if (FAILED(adapter->EnumOutputs(idx_, &output_))) {
                adapter->Release();
                return false;
            }
        }
        DXGI_OUTPUT_DESC desc{};
        output_->GetDesc(&desc);
        if (desc.Monitor) monitor_ = desc.Monitor;
        DXGI_RATIONAL dummy{0, 0};
        (void)dummy;
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2,
                               D3D11_SDK_VERSION, &device_, &fl_, &context_);
        adapter->Release();
        if (FAILED(hr)) return false;
        IDXGIOutput1* out1 = nullptr;
        if (FAILED(output_->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1))) return false;
        hr = out1->DuplicateOutput(device_, &dup_);
        out1->Release();
        if (FAILED(hr)) return false;
        DXGI_OUTDUPL_DESC dd{};
        dup_->GetDesc(&dd);
        w_ = dd.ModeDesc.Width;
        h_ = dd.ModeDesc.Height;
        if (w_ == 0 || h_ == 0) return false;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w_;
        d.Height = h_;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &frame_tex_))) return false;
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &staging_))) return false;
        return true;
    }

    void stop() override {
        auto rel = [](auto** p) { if (*p) { (*p)->Release(); *p = nullptr; } };
        rel(&frame_tex_);
        rel(&staging_);
        rel(&dup_);
        rel(&context_);
        rel(&device_);
        rel(&output_);
        rel(&factory_);
    }

    int width() const override { return w_; }
    int height() const override { return h_; }
    std::string name() const override { return "dxgi"; }
    ID3D11Device* d3d_device() override { return device_; }
    ID3D11DeviceContext* d3d_context() override { return context_; }

    // acquire + copy to our own texture; the duplication frame is released
    // immediately so the compositor never waits on us
    bool grab_gpu(ID3D11Texture2D** tex, uint64_t& ts_us) override {
        if (!dup_) return false;
        IDXGIResource* res = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info{};
        HRESULT hr = dup_->AcquireNextFrame(50, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (FAILED(hr)) {
            rebuild();
            return false;
        }
        if (info.LastPresentTime.QuadPart == 0 && have_frame_) {
            // only the mouse moved; the previous texture is still current
            res->Release();
            dup_->ReleaseFrame();
            *tex = frame_tex_;
            ts_us = last_ts_;
            return true;
        }
        ID3D11Texture2D* src = nullptr;
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src);
        res->Release();
        bool ok = false;
        if (src) {
            context_->CopyResource(frame_tex_, src);
            src->Release();
            ok = true;
        }
        dup_->ReleaseFrame();
        if (ok) {
            have_frame_ = true;
            last_ts_ = now_us();
            *tex = frame_tex_;
            ts_us = last_ts_;
        }
        return ok;
    }

    bool grab(cv::Mat& bgr, uint64_t& ts_us) override {
        ID3D11Texture2D* tex = nullptr;
        if (!grab_gpu(&tex, ts_us) || !have_frame_) return false;
        context_->CopyResource(staging_, frame_tex_);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(context_->Map(staging_, 0, D3D11_MAP_READ, 0, &m))) return false;
        cv::Mat bgra(h_, w_, CV_8UC4, m.pData, m.RowPitch);
        bgra.copyTo(bgr);
        context_->Unmap(staging_, 0);
        cv::cvtColor(bgr, bgr, cv::COLOR_BGRA2BGR);
        return true;
    }

    void set_monitor(int index) { monitor_index_ = index; }

private:
    void rebuild() {
        // the duplication dies on mode switches and fullscreen transitions;
        // one quiet rebuild attempt, then the caller just retries next tick
        std::string n = name();
        (void)n;
        IDXGIOutput1* out1 = nullptr;
        if (output_ && device_ && SUCCEEDED(output_->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1))) {
            if (dup_) { dup_->Release(); dup_ = nullptr; }
            out1->DuplicateOutput(device_, &dup_);
            out1->Release();
        }
    }

    int monitor_index_ = 0;
    int idx_ = 0;
    int w_ = 0, h_ = 0;
    bool have_frame_ = false;
    uint64_t last_ts_ = 0;
    HMONITOR monitor_ = nullptr;
    D3D_FEATURE_LEVEL fl_ = D3D_FEATURE_LEVEL_11_0;
    IDXGIFactory1* factory_ = nullptr;
    IDXGIOutput* output_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIOutputDuplication* dup_ = nullptr;
    ID3D11Texture2D* frame_tex_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
};

#elif defined(__linux__)

class X11Capture : public ScreenCapture {
public:
    ~X11Capture() override { stop(); }

    bool start() override {
        stop();
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) return false;
        root_ = DefaultRootWindow(dpy_);
        XWindowAttributes a{};
        if (!XGetWindowAttributes(dpy_, root_, &a)) return false;
        w_ = a.width;
        h_ = a.height;
        have_shm_ = XShmQueryExtension(dpy_) != False;
        if (have_shm_) {
            ximg_ = XShmCreateImage(dpy_, a.visual, a.depth, ZPixmap, nullptr, &shminfo_, w_, h_);
            if (ximg_) {
                shminfo_.shmid = shmget(IPC_PRIVATE, size_t(ximg_->bytes_per_line) * ximg_->height, IPC_CREAT | 0777);
                if (shminfo_.shmid >= 0) {
                    shminfo_.shmaddr = ximg_->data = (char*)shmat(shminfo_.shmid, nullptr, 0);
                    shminfo_.readOnly = False;
                    if (!XShmAttach(dpy_, &shminfo_)) have_shm_ = false;
                } else {
                    have_shm_ = false;
                }
            } else {
                have_shm_ = false;
            }
        }
        return true;
    }

    void stop() override {
        if (dpy_) {
            if (have_shm_ && ximg_) {
                XShmDetach(dpy_, &shminfo_);
                if (shminfo_.shmaddr) shmdt(shminfo_.shmaddr);
                if (shminfo_.shmid >= 0) shmctl(shminfo_.shmid, IPC_RMID, nullptr);
            }
            if (ximg_ && !have_shm_) XDestroyImage(ximg_);
            ximg_ = nullptr;
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
        }
    }

    int width() const override { return w_; }
    int height() const override { return h_; }
    std::string name() const override { return "x11"; }

    // the focused window's absolute rect; false when nothing useful is focused
    bool focused_window_rect(int& x, int& y, int& w, int& h) {
        if (!dpy_) return false;
        Window focus = None;
        int revert = 0;
        XGetInputFocus(dpy_, &focus, &revert);
        if (focus == None || focus == PointerRoot || focus == root_) return false;
        Window child = None;
        if (!XTranslateCoordinates(dpy_, focus, root_, 0, 0, &x, &y, &child)) return false;
        XWindowAttributes a{};
        if (!XGetWindowAttributes(dpy_, focus, &a)) return false;
        w = a.width;
        h = a.height;
        return w > 8 && h > 8;
    }

    // region grabs share the same path; the caller crops what it needs
    bool grab(cv::Mat& bgr, uint64_t& ts_us) override {
        if (!dpy_) return false;
        XImage* img = nullptr;
        XShmSegmentInfo tmp{};
        if (have_shm_) {
            XLockDisplay(dpy_);
            if (!XShmGetImage(dpy_, root_, ximg_, 0, 0, AllPlanes)) {
                XUnlockDisplay(dpy_);
                return false;
            }
            XUnlockDisplay(dpy_);
            img = ximg_;
        } else {
            img = XGetImage(dpy_, root_, 0, 0, w_, h_, AllPlanes, ZPixmap);
            if (!img) return false;
            tmp.shmaddr = nullptr;
        }
        ts_us = now_us();
        if (img->bits_per_pixel == 32 || img->bits_per_pixel == 24) {
            cv::Mat bgra(h_, w_, CV_8UC4, img->data, size_t(img->bytes_per_line));
            cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        } else {
            cv::Mat dummy;
            (void)dummy;
            bgr = cv::Mat();
        }
        if (!have_shm_ && img) XDestroyImage(img);
        return !bgr.empty();
    }

private:
    Display* dpy_ = nullptr;
    Window root_ = None;
    XImage* ximg_ = nullptr;
    XShmSegmentInfo shminfo_{};
    bool have_shm_ = false;
    int w_ = 0, h_ = 0;
};

#ifdef NEONIFY_WITH_PORTAL

// wayland capture: a screencast session through the desktop portal. the dance
// is create-session -> select-sources -> start, each confirmed by a Response
// signal on a per-call request path; then the fd pipewire gave the portal is
// opened and the monitor stream is consumed as bgrx frames.
class PortalCapture : public ScreenCapture {
public:
    ~PortalCapture() override { stop(); }

    bool start() override {
        stop();
        if (sd_bus_open_user(&bus_) < 0) return false;
        std::map<std::string, std::string> res;
        if (!portal_call("CreateSession", {{"session_handle_token", "neonify"}}, res)) return fail();
        auto sit = res.find("session_handle");
        if (sit == res.end()) return fail();
        session_ = sit->second;
        res.clear();
        if (!portal_call("SelectSources", {{"types", "1"}, {"multiple", "false"}, {"cursor_mode", "1"}}, res))
            return fail();
        res.clear();
        if (!portal_call("Start", {}, res)) return fail();
        auto stit = res.find("node");
        if (stit == res.end()) return fail();
        node_id_ = uint32_t(std::stoul(stit->second));
        if (!connect_pipewire()) return fail();
        running_ = true;
        return true;
    }

    void stop() override {
        running_ = false;
        if (loop_) {
            pw_thread_loop_stop(loop_);
            auto rel = [](auto** p) { if (*p) { *p = nullptr; } };
            rel(&stream_);
            if (core_) pw_core_disconnect(core_);
            core_ = nullptr;
            if (context_) pw_context_destroy(context_);
            context_ = nullptr;
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        if (bus_) sd_bus_unref(bus_);
        bus_ = nullptr;
    }

    int width() const override {
        std::lock_guard<std::mutex> lk(latest_mtx_);
        return w_;
    }
    int height() const override {
        std::lock_guard<std::mutex> lk(latest_mtx_);
        return h_;
    }
    std::string name() const override { return "portal"; }

    bool grab(cv::Mat& bgr, uint64_t& ts_us) override {
        std::lock_guard<std::mutex> lk(latest_mtx_);
        if (latest_.empty()) return false;
        latest_.copyTo(bgr);
        ts_us = ts_.load();
        return true;
    }

private:
    mutable std::mutex latest_mtx_;   // const probes lock this too
    bool fail() {
        stop();
        return false;
    }

    // one portal method + its Response signal, synchronous with a timeout
    bool portal_call(const char* method, const std::map<std::string, std::string>& args,
                     std::map<std::string, std::string>& out) {
        char token[64];
        std::snprintf(token, sizeof(token), "neonify%u", ++token_n_);
        const char* uniq = nullptr;
        if (sd_bus_get_unique_name(bus_, &uniq) < 0 || !uniq) return false;
        std::string sender = uniq;
        for (char& c : sender)
            if (c == ':' || c == '.') c = '_';
        request_path_ = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
        sd_bus_message* m = nullptr;
        if (sd_bus_message_new_method_call(bus_, &m, "org.freedesktop.portal.Desktop",
                                           "/org/freedesktop/portal/desktop",
                                           "org.freedesktop.portal.ScreenCast", method) < 0)
            return false;
        auto append_args = [&](sd_bus_message* msg) -> bool {
            if (session_.empty() == false && std::string(method) != "CreateSession")
                if (sd_bus_message_append_basic(msg, 'o', session_.c_str()) < 0) return false;
            // Start carries a parent_window string between the session and the
            // options — we are standalone, so an empty parent
            if (std::string(method) == "Start")
                if (sd_bus_message_append_basic(msg, 's', "") < 0) return false;
            if (sd_bus_message_open_container(msg, 'a', "{sv}") < 0) return false;
            for (const auto& kv : args) {
                if (sd_bus_message_open_container(msg, 'e', "sv") < 0) return false;
                if (sd_bus_message_append_basic(msg, 's', kv.first.c_str()) < 0) return false;
                bool is_bool = (kv.second == "true" || kv.second == "false");
                if (is_bool) {
                    if (sd_bus_message_open_container(msg, 'v', "b") < 0) return false;
                    int b = kv.second == "true" ? 1 : 0;
                    if (sd_bus_message_append_basic(msg, 'b', &b) < 0) return false;
                } else if (kv.first == "types" || kv.first == "cursor_mode") {
                    if (sd_bus_message_open_container(msg, 'v', "u") < 0) return false;
                    uint32_t u = uint32_t(std::stoul(kv.second));
                    if (sd_bus_message_append_basic(msg, 'u', &u) < 0) return false;
                } else {
                    if (sd_bus_message_open_container(msg, 'v', "s") < 0) return false;
                    if (sd_bus_message_append_basic(msg, 's', kv.second.c_str()) < 0) return false;
                }
                if (sd_bus_message_close_container(msg) < 0) return false;
                if (sd_bus_message_close_container(msg) < 0) return false;
            }
            if (sd_bus_message_close_container(msg) < 0) return false;
            return true;
        };
        if (!append_args(m)) {
            sd_bus_message_unref(m);
            return false;
        }
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        int r = sd_bus_call(bus_, m, 2000000, &err, &reply);
        sd_bus_message_unref(m);
        if (r < 0) return false;
        sd_bus_message_unref(reply);
        return wait_response(out);
    }

    bool wait_response(std::map<std::string, std::string>& out) {
        int64_t deadline = int64_t(now_us()) + 3000000;
        bool got = false;
        auto handler = [](sd_bus_message* m, void* userdata, sd_bus_error*) -> int {
            auto* ctx = static_cast<std::pair<bool, std::map<std::string, std::string>>*>(userdata);
            uint32_t response = 1;
            sd_bus_message_read_basic(m, 'u', &response);
            if (response != 0) return 0;
            if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return 0;
            const char* key = nullptr;
            while (sd_bus_message_read_basic(m, 's', &key) > 0) {
                if (sd_bus_message_enter_container(m, 'v', nullptr) < 0) break;
                const char* contents = sd_bus_message_get_signature(m, true);
                if (contents && contents[0] == 'o' && contents[1] == '\0') {
                    const char* o = nullptr;
                    if (sd_bus_message_read_basic(m, 'o', &o) > 0)
                        ctx->second["session_handle"] = o;
                } else if (contents && std::string(contents).rfind("a(ua{sv})", 0) == 0) {
                    if (sd_bus_message_enter_container(m, 'a', "(ua{sv})") >= 0) {
                        uint32_t n = 0;
                        if (sd_bus_message_read_basic(m, 'u', &n) > 0 && n > 0) {
                            if (sd_bus_message_enter_container(m, 'r', nullptr) >= 0) {
                                uint32_t node = 0;
                                if (sd_bus_message_read_basic(m, 'u', &node) > 0)
                                    ctx->second["node"] = std::to_string(node);
                                // the message is abandoned after the callback —
                                // no need to walk the rest of the stream list
                                return 0;
                            }
                        }
                    }
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
            ctx->first = true;
            return 0;
        };
        std::pair<bool, std::map<std::string, std::string>> ctx{false, {}};
        sd_bus_slot* slot = nullptr;
        sd_bus_match_signal(bus_, &slot, "org.freedesktop.portal.Desktop", request_path_.c_str(),
                            "org.freedesktop.portal.Request", "Response", handler, &ctx);
        while (!ctx.first && int64_t(now_us()) < deadline) {
            sd_bus_process(bus_, nullptr) ;
            int r = sd_bus_wait(bus_, 50000);
            if (r < 0) break;
        }
        if (slot) sd_bus_slot_unref(slot);
        if (!ctx.first) return false;
        out = ctx.second;
        return true;
    }

    static void on_process(void* data) {
        auto* self = static_cast<PortalCapture*>(data);
        pw_buffer* buf = pw_stream_dequeue_buffer(self->stream_);
        if (!buf) return;
        spa_buffer* sb = buf->buffer;
        if (sb && sb->n_datas > 0 && sb->datas[0].data) {
            int stride = sb->datas[0].chunk->stride;
            int h = self->h_, w = self->w_;
            if (stride <= 0) stride = w * 4;
            if (w > 0 && h > 0) {
                cv::Mat bgra(h, w, CV_8UC4, sb->datas[0].data, size_t(stride));
                cv::Mat bgr;
                cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
                std::lock_guard<std::mutex> lk(self->latest_mtx_);
                bgr.copyTo(self->latest_);
                self->ts_.store(now_us());
            }
        }
        pw_stream_queue_buffer(self->stream_, buf);
    }

    static void on_param_changed(void* data, uint32_t id, const struct spa_pod* param) {
        auto* self = static_cast<PortalCapture*>(data);
        if (param == nullptr || id != SPA_PARAM_Format) return;
        spa_video_info_raw fmt{};
        if (spa_format_video_raw_parse(param, &fmt) < 0) return;
        std::lock_guard<std::mutex> lk(self->latest_mtx_);
        self->w_ = int(fmt.size.width);
        self->h_ = int(fmt.size.height);
    }

    bool connect_pipewire() {
        fd_ = portal_open_fd();
        if (fd_ < 0) return false;
        pw_init(nullptr, nullptr);
        loop_ = pw_thread_loop_new("neonify-portal", nullptr);
        if (!loop_) return false;
        pw_thread_loop_lock(loop_);
        context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
        if (!context_) {
            pw_thread_loop_unlock(loop_);
            return false;
        }
        pw_properties* props = pw_properties_new(nullptr, nullptr);
        core_ = pw_context_connect_fd(context_, ::dup(fd_), nullptr, 0);
        if (!core_) {
            pw_properties_free(props);
            pw_thread_loop_unlock(loop_);
            return false;
        }
        stream_ = pw_stream_new(core_, "neonify-capture", props);
        if (!stream_) {
            pw_thread_loop_unlock(loop_);
            return false;
        }
        static const pw_stream_events events = [] {
            pw_stream_events ev{};
            ev.version = PW_VERSION_STREAM_EVENTS;
            ev.process = on_process;
            ev.param_changed = on_param_changed;
            return ev;
        }();
        pw_stream_add_listener(stream_, &hooks_, &events, this);
        uint8_t podbuf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(podbuf, sizeof(podbuf));
        const spa_pod* params[1] = {nullptr};
        {
            // the primitives instead of the add_object sugar — that macro
            // changed shape across pipewire versions, these calls did not
            spa_pod_frame frame;
            if (spa_pod_builder_push_object(&b, &frame, SPA_TYPE_OBJECT_Format,
                                            SPA_PARAM_EnumFormat) >= 0) {
                spa_pod_builder_add(&b,
                                    SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
                                    SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                                    SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
                                    0);
                params[0] = (const spa_pod*)spa_pod_builder_pop(&b, &frame);
            }
        }
        int flags = PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_AUTOCONNECT;
        if (pw_stream_connect(stream_, PW_DIRECTION_INPUT, node_id_,
                              pw_stream_flags(flags), params, 1) < 0) {
            pw_thread_loop_unlock(loop_);
            return false;
        }
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_start(loop_);
        int64_t deadline = int64_t(now_us()) + 5000000;
        while (int64_t(now_us()) < deadline) {
            {
                std::lock_guard<std::mutex> lk(latest_mtx_);
                if (w_ > 0 && h_ > 0) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    int portal_open_fd() {
        sd_bus_message* m = nullptr;
        if (sd_bus_message_new_method_call(bus_, &m, "org.freedesktop.portal.Desktop",
                                           "/org/freedesktop/portal/desktop",
                                           "org.freedesktop.portal.ScreenCast",
                                           "OpenFileDescriptor") < 0)
            return -1;
        if (sd_bus_message_append_basic(m, 'o', session_.c_str()) < 0 ||
            sd_bus_message_open_container(m, 'a', "{sv}") < 0 ||
            sd_bus_message_close_container(m) < 0) {
            sd_bus_message_unref(m);
            return -1;
        }
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        int r = sd_bus_call(bus_, m, 2000000, &err, &reply);
        sd_bus_message_unref(m);
        if (r < 0) return -1;
        int fd = -1;
        r = sd_bus_message_read(reply, "h", &fd);
        int own = -1;
        if (r >= 0 && fd >= 0) own = ::dup(fd);
        sd_bus_message_unref(reply);
        return own;
    }

    sd_bus* bus_ = nullptr;
    std::string session_;
    std::string request_path_;
    unsigned token_n_ = 0;
    uint32_t node_id_ = 0;
    int fd_ = -1;
    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_stream* stream_ = nullptr;
    spa_hook hooks_{};
    cv::Mat latest_;
    std::atomic<uint64_t> ts_{0};
    int w_ = 0, h_ = 0;
    std::atomic<bool> running_{false};
};

#endif  // NEONIFY_WITH_PORTAL

#endif  // __linux__

// the factory the desktop main uses: primary or indexed monitor, desktop or
// focused-window mode lives above this (the crop is the same on every backend)
inline ScreenCapture* make_capture(const std::string& backend, int monitor_index) {
#ifdef _WIN32
    (void)backend;
    DxgiCapture* c = new DxgiCapture();
    c->set_monitor(monitor_index);
    return c;
#elif defined(__linux__)
    if (backend == "portal")
#ifdef NEONIFY_WITH_PORTAL
        return new PortalCapture();
#else
        return nullptr;
#endif
    return new X11Capture();
#else
    (void)backend;
    (void)monitor_index;
    return nullptr;
#endif
}

}  // namespace neon
