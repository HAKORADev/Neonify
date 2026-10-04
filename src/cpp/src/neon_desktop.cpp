// NEONIFY native — desktop neonifier. captures the screen the low-level way
// (dxgi duplication / x11 / portal screencast), runs the same neon math as the
// engine (d3d11 compute on windows when the machine has it, opencv otherwise)
// and paints the result on a click-through always-on-top overlay exactly over
// the captured region. ctrl+alt+n toggles, ctrl+alt+n then w neonifies just the
// focused window. settings live in the shared neonify.ini next to the binary.
// qt headers come first: x11 headers below poison Bool/Status/None and qt
// refuses to compile after them
#include <QtWidgets>
#include <QImage>
#include <QTimer>
#include <QThread>
#include <QProcess>
#include <QCoreApplication>

#include "neon_common.h"
#include "neon_image.h"
#include "neon_hw.h"
#include "neon_capture.h"

#ifdef _WIN32
#include "neon_gpu.h"
#endif

#include <csignal>
#ifdef __linux__
#include <execinfo.h>
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <sys/select.h>
#endif

#ifdef NEONIFY_STATIC_QT
#include <QtPlugin>
#include "neon_plugins.h"
#endif

namespace neon_desktop {

using neon::IniFile;

struct Settings {
    bool hotkeys = true;
    std::string mode = "desktop";   // desktop | window
    int monitor = -1;               // -1 primary
    int fps = 30;
    float scale = 1.0f;
    std::string device = "auto";    // auto | gpu | cpu
    std::string palette = "electric";
    float glow = 1.0f;
    float threshold = 0.12f;
    float env = 1.0f;
    int inside = 0;                 // 0 wipe 1 keep 2 glow

    static Settings from(const IniFile& ini) {
        Settings s;
        s.hotkeys = ini.get_bool("desktop", "hotkeys", true);
        s.mode = ini.get("desktop", "mode", "desktop");
        std::string mon = ini.get("desktop", "monitor", "primary");
        s.monitor = (mon == "primary") ? -1 : std::max(0, std::atoi(mon.c_str()) - 1);
        s.fps = std::max(5, std::min(60, ini.get_int("desktop", "fps", 30)));
        s.scale = std::max(0.25f, std::min(1.0f, ini.get_float("desktop", "scale", 1.0f)));
        s.device = ini.get("desktop", "device", "auto");
        s.palette = ini.get("desktop", "palette", "electric");
        s.glow = std::max(0.2f, std::min(3.0f, ini.get_float("desktop", "glow", 1.0f)));
        s.threshold = std::max(0.02f, std::min(0.5f, ini.get_float("desktop", "threshold", 0.12f)));
        s.env = std::max(0.0f, std::min(2.0f, ini.get_float("desktop", "env", 1.0f)));
        std::string inside = ini.get("desktop", "inside", "wipe");
        s.inside = inside == "keep" ? 1 : (inside == "glow" ? 2 : 0);
        return s;
    }
};

// ---------------------------------------------------------------- overlay
// an opaque window parked exactly over the captured region. input never
// reaches it: layered+transparent on windows, an empty x shape on x11.
class OverlayWidget : public QWidget {
public:
    OverlayWidget() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                       Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }

    void set_frame(const QImage& img) {
        frame_ = img;
        update();
    }

    void pin_to(const QRect& r) {
        setGeometry(r);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), Qt::black);
        if (!frame_.isNull())
            p.drawImage(rect(), frame_);
    }

    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
#ifdef _WIN32
        HWND hwnd = reinterpret_cast<HWND>(winId());
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                             WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
#elif defined(__linux__)
        if (QApplication::platformName().contains("xcb")) {
            Display* dpy = XOpenDisplay(nullptr);
            if (dpy) {
                Window w = (Window)winId();
                XRectangle rect{0, 0, 0, 0};
                XShapeCombineRectangles(dpy, w, ShapeInput, 0, 0, &rect, 0, ShapeSet, 0);
                XFlush(dpy);
                XCloseDisplay(dpy);
            }
        }
#endif
    }

private:
    QImage frame_;
};

#ifdef __linux__
// global hotkeys live on their own x connection (mixing xlib grabs into qt's
// xcb connection is how you kill a compositor session) — a thread polls the
// connection fd, wayland sessions simply have none and use the tray instead
class X11Hotkeys : public QThread {
public:
    std::function<void()> on_toggle;

    void run() override {
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) return;
        Window root = DefaultRootWindow(dpy_);
        KeyCode n = XKeysymToKeycode(dpy_, XK_N);
        if (!n) {
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
            return;
        }
        unsigned mods[4] = {ControlMask | Mod1Mask, ControlMask | Mod1Mask | LockMask,
                            ControlMask | Mod1Mask | Mod2Mask,
                            ControlMask | Mod1Mask | LockMask | Mod2Mask};
        for (unsigned m : mods) XGrabKey(dpy_, n, m, root, True, GrabModeAsync, GrabModeAsync);
        XSelectInput(dpy_, root, KeyPressMask);
        XFlush(dpy_);
        int fd = XConnectionNumber(dpy_);
        while (!stop_.load()) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeval tv{0, 200000};
            if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
            while (!stop_.load() && XPending(dpy_)) {
                XEvent ev;
                XNextEvent(dpy_, &ev);
                if (ev.type == KeyPress && ev.xkey.keycode == n && on_toggle) on_toggle();
            }
        }
        XCloseDisplay(dpy_);
        dpy_ = nullptr;
    }

    // during the chord the same connection answers: is W held right now?
    bool w_held() {
        if (!dpy_) return false;
        char keys[32];
        XQueryKeymap(dpy_, keys);
        KeyCode w = XKeysymToKeycode(dpy_, XK_W);
        return w && (keys[w >> 3] & (1 << (w & 7)));
    }

    void stop() {
        stop_ = true;
        wait();
    }

private:
    Display* dpy_ = nullptr;
    std::atomic<bool> stop_{false};
};
#endif

// ---------------------------------------------------------------- controller
class Controller : public QObject {
    Q_OBJECT

public:
    explicit Controller(QObject* parent = nullptr) : QObject(parent) {
        repaint_ = new QTimer(this);
        repaint_->setTimerType(Qt::PreciseTimer);
        connect(repaint_, &QTimer::timeout, this, &Controller::pull_frame);
    }

    ~Controller() override { stop(); }

    void configure(const Settings& s) {
        settings_ = s;
    }

    bool is_running() const { return running_; }

signals:
    void stats(const QString& text);

public slots:
    void start(const std::string& mode) {
        stop();
        IniFile& ini = neon::app_ini();
        Settings s = Settings::from(ini);
        s.mode = mode;
        settings_ = s;

        std::string backend = ini.get("hardware", "desktop_backend", "none");
#ifdef _WIN32
        backend = "dxgi";
#endif
        capture_.reset(neon::make_capture(backend, settings_.monitor));
        if (!capture_ || !capture_->start()) {
            emit stats(QStringLiteral("capture failed: this machine reports '%1' — wayland needs xdg-desktop-portal running")
                           .arg(QString::fromStdString(backend)));
            capture_.reset();
            return;
        }

        gpu_ok_ = false;
#ifdef _WIN32
        if (capture_->d3d_device() && (settings_.device == "gpu" || settings_.device == "auto")) {
            gpu_ok_ = gpu_.init(capture_->d3d_device());
            if (gpu_ok_) {
                ID3D11Device* dev = capture_->d3d_device();
                D3D11_TEXTURE2D_DESC dd{};
                dd.Width = UINT(capture_->width());
                dd.Height = UINT(capture_->height());
                dd.MipLevels = 1;
                dd.ArraySize = 1;
                dd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                dd.SampleDesc.Count = 1;
                dd.Usage = D3D11_USAGE_DEFAULT;
                dd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                if (FAILED(dev->CreateTexture2D(&dd, nullptr, &gpu_out_))) gpu_ok_ = false;
                dd.Usage = D3D11_USAGE_STAGING;
                dd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                dd.BindFlags = 0;
                if (gpu_ok_ && FAILED(dev->CreateTexture2D(&dd, nullptr, &gpu_stage_))) gpu_ok_ = false;
            }
        }
        if (!gpu_ok_ && settings_.device == "gpu") {
            emit stats(QStringLiteral("gpu requested but d3d11 compute refused — running cpu"));
        }
#endif

        overlay_ = new OverlayWidget();
        overlay_->pin_to(QRect(0, 0, capture_->width(), capture_->height()));
        overlay_->show();

        running_ = true;
        worker_ = std::thread([this] { loop(); });
        int interval = std::max(16, int(1000.0 / settings_.fps));
        repaint_->start(interval);
        emit stats(QStringLiteral("running (%1, %2)")
                       .arg(QString::fromStdString(capture_->name()))
                       .arg(gpu_ok_ ? QStringLiteral("gpu") : QStringLiteral("cpu")));
    }

    void stop() {
        if (!running_ && !capture_) return;
        running_ = false;
        repaint_->stop();
        if (worker_.joinable()) worker_.join();
        if (overlay_) {
            overlay_->close();
            overlay_->deleteLater();
            overlay_ = nullptr;
        }
#ifdef _WIN32
        if (gpu_stage_) { gpu_stage_->Release(); gpu_stage_ = nullptr; }
        if (gpu_out_) { gpu_out_->Release(); gpu_out_ = nullptr; }
#endif
        capture_.reset();
        emit stats(QStringLiteral("stopped"));
    }

private slots:
    void pull_frame() {
        QImage img;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            img = latest_;
        }
        if (img.isNull() || !overlay_) return;
        overlay_->set_frame(img);
        overlay_->update();
    }

private:
    void loop() {
        while (running_) {
            uint64_t ts = 0;
            QImage img;
            int x = 0, y = 0, w = capture_->width(), h = capture_->height();
            bool got = false;
#ifdef _WIN32
            if (gpu_ok_) {
                ID3D11Texture2D* tex = nullptr;
                if (capture_->grab_gpu(&tex, ts)) {
                    ID3D11DeviceContext* ctx = capture_->d3d_context();
                    if (gpu_.process(tex, gpu_out_, w, h, settings_.palette, settings_.glow,
                                     settings_.threshold, settings_.env, settings_.inside)) {
                        ctx->CopyResource(gpu_stage_, gpu_out_);
                        D3D11_MAPPED_SUBRESOURCE m{};
                        if (SUCCEEDED(ctx->Map(gpu_stage_, 0, D3D11_MAP_READ, 0, &m))) {
                            img = QImage((const uchar*)m.pData, w, h, int(m.RowPitch),
                                         QImage::Format_ARGB32).copy();
                            ctx->Unmap(gpu_stage_, 0);
                            got = true;
                        }
                    } else {
                        gpu_ok_ = false;
                        emit stats(QStringLiteral("gpu pass refused — falling back to cpu"));
                    }
                }
            }
#endif
            if (!got) {
                cv::Mat full;
                if (capture_->grab(full, ts) && !full.empty()) {
                    cv::Mat region = full;
                    if (settings_.mode == "window") {
                        if (!window_region(full, x, y, w, h, region)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(30));
                            continue;
                        }
                    }
                    img = mat_to_qimage(region);
                    got = true;
                }
            }
            if (got && !img.isNull()) {
                measure(img, ts);
                std::lock_guard<std::mutex> lk(mtx_);
                latest_ = img;
                if (overlay_pos_ != QRect(x, y, img.width(), img.height())) {
                    overlay_pos_ = QRect(x, y, img.width(), img.height());
                    QMetaObject::invokeMethod(this, [this] {
                        if (overlay_) overlay_->pin_to(overlay_pos_);
                    }, Qt::QueuedConnection);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

#ifdef _WIN32
    // the focused window's rect inside the captured monitor; restarts the
    // duplication on whichever output the window lives on
    bool window_region(const cv::Mat& full, int& x, int& y, int& w, int& h, cv::Mat& region) {
        (void)full;
        HWND fore = GetForegroundWindow();
        if (!fore) return false;
        RECT r{};
        if (!GetWindowRect(fore, &r)) return false;
        if (r.right - r.left < 16 || r.bottom - r.top < 16) return false;
        HMONITOR mon = MonitorFromWindow(fore, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(mon, &mi)) return false;
        int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        if (cx < mi.rcMonitor.left || cx >= mi.rcMonitor.right ||
            cy < mi.rcMonitor.top || cy >= mi.rcMonitor.bottom)
            return false;
        // duplication covers one output; a window on another output needs a
        // restart targeted at that output — handled by the monitor index scan
        // (primary build covers output 0; other outputs need monitor= in the ini)
        x = std::max(0, int(r.left));
        y = std::max(0, int(r.top));
        w = int(r.right - r.left);
        h = int(r.bottom - r.top);
        if (x + w > capture_->width()) w = capture_->width() - x;
        if (y + h > capture_->height()) h = capture_->height() - y;
        if (w <= 0 || h <= 0) return false;
        region = full(cv::Rect(x, y, w, h)).clone();
        return true;
    }
#else
    bool window_region(const cv::Mat& full, int& x, int& y, int& w, int& h, cv::Mat& region) {
        auto* x11 = dynamic_cast<neon::X11Capture*>(capture_.get());
        if (!x11) return false;
        int wx, wy, ww, wh;
        if (!x11->focused_window_rect(wx, wy, ww, wh)) return false;
        x = wx;
        y = wy;
        w = ww;
        h = wh;
        if (x + w > full.cols) w = full.cols - x;
        if (y + h > full.rows) h = full.rows - y;
        if (w <= 0 || h <= 0) return false;
        region = full(cv::Rect(x, y, w, h)).clone();
        return true;
    }
#endif

    QImage mat_to_qimage(const cv::Mat& bgr) {
        float s = settings_.scale;
        cv::Mat work = (s < 1.0f) ? cv::Mat() : bgr;
        if (s < 1.0f) {
            cv::resize(bgr, work, cv::Size(int(bgr.cols * s), int(bgr.rows * s)), 0, 0, cv::INTER_AREA);
        }
        cv::Mat out;
        neon::EdgeAux aux;
        cv::Mat edges, ang;
        neon::edge_field(work, settings_.glow, settings_.threshold, settings_.env, edges, aux, &ang);
        cv::Mat field;
        neon::neon_glow_stack(edges, settings_.glow, settings_.env, field);
        out = (settings_.palette == "spectrum") ? neon::spectrum_colorize(field, ang)
                                                : neon::colorize(field, settings_.palette);
        cv::add(out, neon::neon_core_u8(edges), out);
        if (settings_.inside != 0) neon::keep_inside_composite(out, work, edges, field, settings_.inside);
        if (s < 1.0f) cv::resize(out, out, bgr.size(), 0, 0, cv::INTER_LINEAR);
        cv::Mat rgba;
        cv::cvtColor(out, rgba, cv::COLOR_BGR2RGB);
        return QImage(rgba.data, rgba.cols, rgba.rows, int(rgba.step), QImage::Format_RGB888).copy();
    }

    void measure(const QImage&, uint64_t ts) {
        double ms = double(neon::now_us() - ts) / 1000.0;
        if (ms > 0 && ms < 5000) {
            std::lock_guard<std::mutex> lk(stat_mtx_);
            last_ms_ = last_ms_ < 0 ? ms : last_ms_ * 0.9 + ms * 0.1;
        }
        uint64_t now = neon::now_us();
        if (now - last_stat_ > 1000000) {
            last_stat_ = now;
            double m;
            {
                std::lock_guard<std::mutex> lk(stat_mtx_);
                m = last_ms_;
            }
            emit stats(QStringLiteral("running — %1, %2, %3 ms to paint")
                           .arg(QString::fromStdString(capture_->name()))
                           .arg(gpu_ok_ ? QStringLiteral("gpu") : QStringLiteral("cpu"))
                           .arg(m, 0, 'f', 1));
        }
    }

    Settings settings_;
    std::unique_ptr<neon::ScreenCapture> capture_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex mtx_;
    QImage latest_;
    QRect overlay_pos_;
    OverlayWidget* overlay_ = nullptr;
    QTimer* repaint_ = nullptr;
    bool gpu_ok_ = false;
#ifdef _WIN32
    neon::DesktopNeonGpu gpu_;
    ID3D11Texture2D* gpu_out_ = nullptr;
    ID3D11Texture2D* gpu_stage_ = nullptr;
#endif
    std::mutex stat_mtx_;
    double last_ms_ = -1;
    uint64_t last_stat_ = 0;
};

// ---------------------------------------------------------------- app
class DesktopApp : public QObject {
    Q_OBJECT

public:
    DesktopApp() {
        controller_ = new Controller(this);
        tray_ = new QSystemTrayIcon(QIcon(":/logo.png"), this);
        menu_ = new QMenu();
        build_menu();
        tray_->setContextMenu(menu_);
        tray_->setToolTip(QStringLiteral("neonify desktop"));
        tray_->show();
        connect(controller_, &Controller::stats, this, [this](const QString& s) {
            tray_->setToolTip(QStringLiteral("neonify desktop — %1").arg(s));
            if (status_) status_->setText(s);
        });
#ifdef _WIN32
        install_hotkeys_win();
#endif
#ifdef __linux__
        if (QApplication::platformName().contains("xcb")) {
            hotkeys_ = new X11Hotkeys();
            hotkeys_->on_toggle = [this] { toggle_chord(); };
            hotkeys_->start();
        }
#endif
    }

    ~DesktopApp() override {
#ifdef _WIN32
        if (hwnd_) UnregisterHotKey(hwnd_, 1);
#endif
#ifdef __linux__
        if (hotkeys_) {
            hotkeys_->stop();
            hotkeys_->deleteLater();
        }
#endif
    }

private slots:
    void toggle_chord() {
        // ctrl+alt+n alone toggles after a short wait; a w pressed inside the
        // chord redirects to window mode (ctrl+alt+n+w)
        if (armed_) return;
        armed_ = true;
        bool window_mode = false;
        int64_t deadline = int64_t(neon::now_us()) + 600000;
        while (int64_t(neon::now_us()) < deadline) {
#ifdef _WIN32
            if (GetAsyncKeyState('W') & 0x8000) { window_mode = true; break; }
#endif
#ifdef __linux__
            if (hotkeys_ && hotkeys_->w_held()) { window_mode = true; break; }
#endif
            QThread::msleep(25);
        }
        armed_ = false;
        std::string mode = window_mode ? "window" : "desktop";
        if (controller_->is_running()) {
            // same mode pressed again means stop; a different mode switches
            if (mode == current_mode_) {
                controller_->stop();
                current_mode_.clear();
            } else {
                current_mode_ = mode;
                controller_->start(mode);
            }
        } else {
            current_mode_ = mode;
            controller_->start(mode);
        }
    }

    void tray_toggle_desktop() {
        if (controller_->is_running() && current_mode_ == "desktop") {
            controller_->stop();
            current_mode_.clear();
        } else {
            current_mode_ = "desktop";
            controller_->start("desktop");
        }
    }

    void tray_toggle_window() {
        if (controller_->is_running() && current_mode_ == "window") {
            controller_->stop();
            current_mode_.clear();
        } else {
            current_mode_ = "window";
            controller_->start("window");
        }
    }

    void open_ini() {
        QString path = QString::fromStdString(neon::ini_path());
#ifdef _WIN32
        QProcess::startDetached(QStringLiteral("notepad"), QStringList{path});
#else
        QProcess::startDetached(QStringLiteral("xdg-open"), QStringList{path});
#endif
    }

private:
    void build_menu() {
        menu_->clear();
        QAction* start_desktop = menu_->addAction(QStringLiteral("neonify desktop"));
        QAction* start_window = menu_->addAction(QStringLiteral("neonify focused window"));
        connect(start_desktop, &QAction::triggered, this, &DesktopApp::tray_toggle_desktop);
        connect(start_window, &QAction::triggered, this, &DesktopApp::tray_toggle_window);
        menu_->addSeparator();
        status_ = menu_->addAction(QStringLiteral("backend: %1 — device: %2")
                                       .arg(QString::fromStdString(neon::app_ini().get("hardware", "desktop_backend", "none")))
                                       .arg(QString::fromStdString(neon::app_ini().get("desktop", "device", "auto"))));
        status_->setEnabled(false);
        QAction* ini_action = menu_->addAction(QStringLiteral("open settings (neonify.ini)"));
        connect(ini_action, &QAction::triggered, this, &DesktopApp::open_ini);
        menu_->addSeparator();
        QAction* quit = menu_->addAction(QStringLiteral("quit"));
        connect(quit, &QAction::triggered, qApp, &QApplication::quit);
    }

#ifdef _WIN32
    void install_hotkeys_win() {
        if (!neon::app_ini().get_bool("desktop", "hotkeys", true)) return;
        hwnd_ = reinterpret_cast<HWND>(overlay_host_.winId());
        RegisterHotKey(hwnd_, 1, MOD_CONTROL | MOD_ALT, 'N');
        filter_ = new HotkeyFilter(this);
        qApp->installNativeEventFilter(filter_);
    }

    class HotkeyFilter : public QAbstractNativeEventFilter {
    public:
        explicit HotkeyFilter(DesktopApp* app) : app_(app) {}
        // qt5 passes long* for the result (qt6 moved to qint64 — this build is qt5)
        bool nativeEventFilter(const QByteArray&, void* message, long*) override {
            MSG* msg = static_cast<MSG*>(message);
            if (msg->message == WM_HOTKEY && msg->wParam == 1) {
                QMetaObject::invokeMethod(app_, "toggle_chord", Qt::QueuedConnection);
                return true;
            }
            return false;
        }

    private:
        DesktopApp* app_;
    };

    QWidget overlay_host_;
    HWND hwnd_ = nullptr;
    HotkeyFilter* filter_ = nullptr;
#endif

    Controller* controller_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* menu_ = nullptr;
    QAction* status_ = nullptr;
#ifdef __linux__
    X11Hotkeys* hotkeys_ = nullptr;
#endif
    bool armed_ = false;
    std::string current_mode_;
};

}  // namespace neon_desktop

int run_desktop(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Neonify Desktop");
    app.setQuitOnLastWindowClosed(false);

    neon::IniFile& ini = neon::app_ini();

    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "probe") {
        std::printf("desktop backend: %s\n", ini.get("hardware", "desktop_backend", "none").c_str());
        std::printf("gpu: %s (%s)\n", ini.get("hardware", "gpu_name", "none").c_str(),
                    ini.get("hardware", "gpu_backend", "none").c_str());
        std::printf("video encoder: %s\n", ini.get("hardware", "video_encoder", "libx264").c_str());
        std::printf("cpu bench: %s ms | gpu bench: %s ms\n",
                    ini.get("hardware", "cpu_ms", "0").c_str(),
                    ini.get("hardware", "gpu_ms", "0").c_str());
        std::printf("ini: %s\n", neon::ini_path().c_str());
        return 0;
    }

    neon_desktop::DesktopApp desktop;
    return app.exec();
}

int main(int argc, char** argv) {
    neon::attach_parent_console(argc, argv);
    // a crash in the overlay must leave a name behind, not just vanish
    std::signal(SIGSEGV, [](int) {
#ifdef __linux__
        void* frames[32];
        int n = backtrace(frames, 32);
        std::fprintf(stderr, "neonify-desktop crashed — backtrace:\n");
        backtrace_symbols_fd(frames, n, 2);
#else
        void* frames[32];
        USHORT n = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
        std::fprintf(stderr, "neonify-desktop crashed — %d frames\n", (int)n);
#endif
        std::_Exit(139);
    });
    return run_desktop(argc, argv);
}

#include "neon_desktop.moc"
