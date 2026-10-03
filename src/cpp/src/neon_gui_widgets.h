// NEONIFY native gui — center widgets: fixed image canvas, video player,
// waveforms, compares. previews render through ffmpeg, same as the engine.
// audio playback is our own pcm engine (decode -> QAudioOutput): the playhead
// comes from bytes written, so pause freezes exactly and 0.5x glides — no
// system media backend involved, every ffmpeg-decodable format plays.
#pragma once
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QPixmap>
#include <QImage>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QStackedWidget>
#include <QScrollArea>
#include <QStyle>
#include <QFileInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QByteArray>
#include <QDateTime>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QIODevice>
#include <cmath>
#include <QFile>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

#include "neon_common.h"
#include "neon_video.h"

namespace neon_gui {

inline QString style_sheet();

// ---------------------------------------------------------------- colors
inline QColor C_BG()      { return QColor(0x0e, 0x0e, 0x0e); }
inline QColor C_PANEL()   { return QColor(0x16, 0x16, 0x16); }
inline QColor C_LINE()    { return QColor(0x2a, 0x2a, 0x2a); }
inline QColor C_TEXT()    { return QColor(0xe5, 0xe5, 0xe5); }
inline QColor C_DIM()     { return QColor(0x66, 0x66, 0x66); }

// ---------------------------------------------------------------- image decode
// the engine reads images with opencv — the gui previews do the same, so the
// gui can open exactly what the engine opens (jpg included, static qt has no
// qjpeg plugin)
inline QImage load_image_robust(const QString& path) {
    std::vector<uint8_t> bytes = neon::read_file_bytes(path.toStdString());
    if (bytes.empty()) return QImage();
    cv::Mat img = cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (!img.empty()) {
        QImage view(img.data, img.cols, img.rows, int(img.step), QImage::Format_BGR888);
        QImage own = view.copy();
        if (!own.isNull()) return own;
    }
    QImage via_qt = QImage::fromData(bytes.data(), int(bytes.size()));
    return via_qt;
}

// ---------------------------------------------------------------- pcm decode
// s16le interleaved at an arbitrary rate/channels for the playback engine,
// mono f32 @ 8000hz for the waveform. empty byte array when ffmpeg or the
// file is missing
inline QByteArray decode_pcm_bytes(const QString& path, int sr, int ch, const char* fmt) {
    QByteArray all;
    if (!QFile::exists(path)) return all;
    neon::Proc p;
    if (!p.spawn({"ffmpeg", "-v", "error", "-i", path.toUtf8().constData(),
                  "-ac", std::to_string(ch), "-ar", std::to_string(sr), "-f", fmt, "-"},
                 true, false))
        return all;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p.out)) > 0) {
        all.append(buf, int(n));
        if (all.size() > 192 * 1024 * 1024) break;
    }
    p.wait_close();
    return all;
}

inline QByteArray decode_wave_pcm(const QString& path) {
    return decode_pcm_bytes(path, 8000, 1, "f32le");
}

inline void pcm_envelope(const std::vector<float>& pcm, int columns,
                         std::vector<float>& lo, std::vector<float>& hi) {
    lo.assign(size_t(columns), 0.f);
    hi.assign(size_t(columns), 0.f);
    if (pcm.empty() || columns <= 0) return;
    double per = double(pcm.size()) / double(columns);
    for (int c = 0; c < columns; c++) {
        size_t a = size_t(c * per), b = size_t((c + 1) * per);
        if (b <= a) b = a + 1;
        if (b > pcm.size()) b = pcm.size();
        float mn = 1.f, mx = -1.f;
        for (size_t i = a; i < b; i++) {
            float v = pcm[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        lo[size_t(c)] = mn;
        hi[size_t(c)] = mx;
    }
}

inline std::vector<float> bytes_to_pcm(const QByteArray& all) {
    const float* f = reinterpret_cast<const float*>(all.constData());
    size_t n = size_t(all.size()) / sizeof(float);
    return std::vector<float>(f, f + n);
}

// ---------------------------------------------------------------- playback engine
// s16 stereo 44100 decoded once in the background; playback goes through
// QAudioOutput push mode. the playhead is derived from processedUSecs, so it
// never lags, never jumps back on pause, and speed changes re-base cleanly.
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr) : QObject(parent) {
        feed = new QTimer(this);
        feed->setInterval(25);
        feed->setTimerType(Qt::PreciseTimer);
        connect(feed, &QTimer::timeout, this, &AudioEngine::pump);
    }

    ~AudioEngine() override {
        alive = false;
        if (dec.joinable()) dec.join();
        teardown_output();
    }

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    qint64 duration_ms() const { return dur_ms; }
    bool ready() const { return have_pcm; }
    QString error() const { return err; }

    void load(const QString& path) {
        stop();
        have_pcm = false;
        dur_ms = 0;
        err.clear();
        alive = true;
        load_gen++;
        uint64_t gen = load_gen;
        if (dec.joinable()) dec.join();
        dec = std::thread([this, path, gen] {
            QByteArray pcm = decode_pcm_bytes(path, 44100, 2, "s16le");
            if (!alive) return;
            QMetaObject::invokeMethod(this, [this, pcm, gen] {
                if (gen != load_gen || pcm.isEmpty()) {
                    if (gen == load_gen && pcm.isEmpty()) {
                        err = QStringLiteral("cannot decode this file (ffmpeg needed)");
                        emit failed(err);
                    }
                    return;
                }
                src = pcm;
                cache.clear();
                have_pcm = true;
                dur_ms = qint64(double(src.size() / 4) / 44100.0 * 1000.0);
                emit decoded();
            }, Qt::QueuedConnection);
        });
    }

    void play() {
        if (!have_pcm || playing) return;
        if (pos_ms() >= dur_ms - 30) base_ms = 0;
        start_output(base_ms);
        playing = true;
        emit stateChanged();
    }

    void pause() {
        if (!playing) return;
        base_ms = pos_ms();
        if (out) out->suspend();
        playing = false;
        emit stateChanged();
    }

    void toggle() { playing ? pause() : play(); }

    void stop() {
        teardown_output();
        playing = false;
        base_ms = 0;
        emit stateChanged();
    }

    void seek_ms(qint64 ms) {
        bool was = playing;
        base_ms = std::max<qint64>(0, std::min(ms, dur_ms));
        if (was) start_output(base_ms);
        else if (out) teardown_output();
    }

    void set_speed(double s) {
        if (s == speed) return;
        double at = playing ? pos_ms() : base_ms;
        speed = s;
        if (playing) start_output(at);
    }

    qint64 pos_ms() const {
        if (!have_pcm) return base_ms;
        if (!playing || !out) return base_ms;
        double dev_us = double(out->processedUSecs());
        double media = base_ms + (dev_us - base_us) / 1000.0 / speed;
        return qint64(std::min(media, double(dur_ms)));
    }

signals:
    void decoded();
    void failed(const QString& msg);
    void stateChanged();
    void finished();

private slots:
    void pump() {
        if (!out || !playing || !have_pcm) return;
        const QByteArray& buf = buffer_for(speed);
        qint64 dur_us = qint64(double(dur_ms) * 1000.0 * speed);
        int free_b = out->bytesFree();
        while (free_b > 0 && fed_us < dur_us) {
            qint64 byte_at = qint64(double(fed_us) / 1e6 * 44100.0 * speed) * 4;
            if (byte_at >= buf.size()) break;
            int chunk = int(std::min<qint64>(std::min<qint64>(free_b, 16384), buf.size() - byte_at));
            if (!dev) break;
            qint64 wrote = dev->write(buf.constData() + byte_at, chunk);
            if (wrote <= 0) break;
            fed_us += qint64(double(wrote / 4) / (44100.0 * speed) * 1e6);
            free_b -= int(wrote);
        }
        if (fed_us >= dur_us && out->processedUSecs() >= dur_us + 60000) {
            base_ms = dur_ms;
            pause();
            emit finished();
        }
    }

private:
    const QByteArray& buffer_for(double s) {
        auto it = cache.find(s);
        if (it != cache.end()) return it->second;
        if (s == 1.0) return cache.emplace(s, src).first->second;
        const int16_t* in = reinterpret_cast<const int16_t*>(src.constData());
        size_t frames_in = size_t(src.size()) / 4;
        size_t frames_out = size_t(double(frames_in) * s);
        QByteArray resampled;
        resampled.resize(int(frames_out * 4));
        int16_t* o = reinterpret_cast<int16_t*>(resampled.data());
        for (size_t i = 0; i < frames_out; i++) {
            double t = double(i) / s;
            size_t i0 = size_t(t);
            size_t i1 = std::min(i0 + 1, frames_in - 1);
            double f = t - double(i0);
            for (int c = 0; c < 2; c++) {
                double a = double(in[i0 * 2 + c]);
                double b = double(in[i1 * 2 + c]);
                double v = a + (b - a) * f;
                o[i * 2 + c] = int16_t(std::max(-32768.0, std::min(32767.0, v)));
            }
        }
        return cache.emplace(s, resampled).first->second;
    }

    void teardown_output() {
        if (feed) feed->stop();
        if (out) {
            out->stop();
            delete out;
            out = nullptr;
            dev = nullptr;
        }
        base_us = 0;
        fed_us = 0;
    }

    void start_output(qint64 from_ms) {
        teardown_output();
        if (!have_pcm) return;
        QAudioFormat fmt;
        fmt.setSampleRate(44100);
        fmt.setChannelCount(2);
        fmt.setSampleSize(16);
        fmt.setCodec("audio/pcm");
        fmt.setByteOrder(QAudioFormat::LittleEndian);
        fmt.setSampleType(QAudioFormat::SignedInt);
        out = new QAudioOutput(fmt, this);
        out->setBufferSize(44100 * 4);
        base_us = 0;
        fed_us = qint64(double(from_ms) / 1000.0 * 1000.0 * speed);
        base_ms = from_ms;
        buffer_for(speed);
        dev = out->start();
        playing = true;
        feed->start();
    }

public:
    bool is_playing() const { return playing; }

private:
    QTimer* feed = nullptr;
    QAudioOutput* out = nullptr;
    QIODevice* dev = nullptr;
    QByteArray src;
    std::map<double, QByteArray> cache;
    std::thread dec;
    std::atomic<bool> alive{false};
    uint64_t load_gen = 0;
    bool have_pcm = false;
    bool playing = false;
    double speed = 1.0;
    qint64 dur_ms = 0;
    qint64 base_ms = 0;
    qint64 base_us = 0;
    qint64 fed_us = 0;
    QString err;
};

// ---------------------------------------------------------------- waveform
// envelope is computed at a fixed column count so the shape never depends on
// when the widget was laid out (the first-show glitch). the playhead is drawn
// with subpixel antialiasing at 60fps for a buttery slide.
class WaveformWidget : public QWidget {
public:
    static constexpr int ENV_COLS = 2048;
    std::vector<float> lo, hi;
    double pos_frac = 0.0;
    bool playable = false;
    QString message;

    WaveformWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(56);
        setMouseTracking(true);
    }

    void set_pcm(const std::vector<float>& pcm) {
        pcm_envelope(pcm, ENV_COLS, lo, hi);
        playable = !pcm.empty();
        message = pcm.empty() ? QStringLiteral("waveform needs ffmpeg") : QString();
        pos_frac = 0.0;
        update();
    }

    void set_pos(double frac) {
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        pos_frac = frac;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        if (!message.isEmpty()) {
            p.setPen(C_DIM());
            p.drawText(rect(), Qt::AlignCenter, message);
            return;
        }
        if (lo.empty()) return;
        int w = width(), h = height(), mid = h / 2;
        p.setPen(QPen(C_LINE(), 1));
        p.drawLine(0, mid, w, mid);
        p.setPen(QPen(C_TEXT(), 1));
        int cols = int(lo.size());
        for (int x = 0; x < w; x++) {
            int c = x * cols / std::max(1, w);
            float mn = lo[size_t(c)], mx = hi[size_t(c)];
            int y0 = mid - int(mx * float(mid - 4));
            int y1 = mid - int(mn * float(mid - 4));
            if (y1 < y0) std::swap(y0, y1);
            p.drawLine(x, y0, x, std::max(y1, y0 + 1));
        }
        double px = pos_frac * double(w);
        p.setPen(Qt::NoPen);
        p.fillRect(QRectF(px - 1.0, 0, 2.0, h), QColor(0xff, 0xff, 0xff, 40));
        p.setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
        p.drawLine(QLineF(px, 0, px, h));
    }
};

// ---------------------------------------------------------------- image canvas
// fixed walls: the picture always fits inside the frame, no zoom, no pan
class ImageCanvas : public QWidget {
public:
    QPixmap pix;

    ImageCanvas(QWidget* parent = nullptr) : QWidget(parent) {}

    void set_pixmap(const QPixmap& pm) {
        pix = pm;
        update();
    }

    QRectF target_rect() const {
        if (pix.isNull()) return QRectF();
        QSize view = size();
        double fit = std::min(double(view.width() - 16) / pix.width(),
                              double(view.height() - 16) / pix.height());
        if (fit > 1.0) fit = 1.0;
        double w = pix.width() * fit, h = pix.height() * fit;
        return QRectF((view.width() - w) / 2.0, (view.height() - h) / 2.0, w, h);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        if (pix.isNull()) return;
        QRectF r = target_rect();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(r, pix, QRectF(pix.rect()));
        p.setPen(QPen(QColor(0x30, 0x30, 0x30), 1));
        p.drawRect(r.adjusted(-1, -1, 0, 0));
    }
    void resizeEvent(QResizeEvent*) override { update(); }
};

// ---------------------------------------------------------------- video canvas
// ffmpeg decodes frames into a small ring; a timer displays them at the
// file's fps times the chosen speed. no system video backends involved.
class VideoCanvas : public QWidget {
    Q_OBJECT

public:
    QString path;
    neon::VideoInfo info;
    bool ok = false;
    QString error;

    QImage cur;
    long long cur_frame = 0;
    bool playing = false;
    double speed = 1.0;
    bool reached_end = false;

    VideoCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        tick = new QTimer(this);
        tick->setTimerType(Qt::PreciseTimer);
        connect(tick, &QTimer::timeout, this, &VideoCanvas::advance);
    }

    ~VideoCanvas() override { stop_decoder(); }

    double fps() const { return info.fps > 0 ? info.fps : 25.0; }
    qint64 total_ms() const { return qint64(info.dur * 1000.0); }
    qint64 position_ms() const { return qint64(double(cur_frame) * 1000.0 / fps()); }

    bool load(const QString& p) {
        stop();
        path = p;
        error.clear();
        info = neon::probe_video(path.toUtf8().constData());
        ok = info.ok;
        cur = QImage();
        cur_frame = 0;
        reached_end = false;
        if (!ok) error = QStringLiteral("cannot open this video (ffmpeg needed)");
        update();
        emit state_changed();
        return ok;
    }

    void play() {
        if (!ok) return;
        if (reached_end) { seek_ms(0); reached_end = false; }
        playing = true;
        start_decoder(double(cur_frame) / fps());
        int iv = int(1000.0 / (fps() * std::max(0.1, speed)));
        tick->start(std::max(10, iv));
        emit state_changed();
    }

    void pause() {
        playing = false;
        tick->stop();
        stop_decoder();
        emit state_changed();
    }

    void stop() {
        playing = false;
        tick->stop();
        stop_decoder();
        cur_frame = 0;
        reached_end = false;
        cur = QImage();
        emit state_changed();
    }

    void set_speed(double s) {
        speed = s;
        if (playing) {
            int iv = int(1000.0 / (fps() * std::max(0.1, speed)));
            tick->start(std::max(10, iv));
        }
    }

    void seek_ms(qint64 ms) {
        if (!ok) return;
        bool was_playing = playing;
        pause();
        cur_frame = std::min<long long>(std::max(0LL, qint64(double(ms) / 1000.0 * fps())),
                                        std::max<long long>(0, info.n - 1));
        reached_end = false;
        if (was_playing) play();
        else fetch_one(cur_frame);
        emit state_changed();
    }

signals:
    void state_changed();
    void decode_failed(const QString& msg);

public slots:
    void play_pause_slot() {
        if (playing) pause();
        else play();
    }

    void advance() {
        if (!playing || !ok) return;
        QImage next;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (ring.empty()) {
                if (!dec_busy) {
                    reached_end = true;
                    playing = false;
                    tick->stop();
                }
                return;
            }
            next = ring.front();
            ring.pop_front();
        }
        cur = next;
        cur_frame++;
        cv_full.notify_all();
        update();
        emit state_changed();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x05, 0x05, 0x05));
        if (!ok) {
            p.setPen(C_DIM());
            p.drawText(rect(), Qt::AlignCenter, error);
            return;
        }
        if (cur.isNull()) {
            p.setPen(C_DIM());
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("ready \xe2\x80\x94 press play"));
            return;
        }
        QSize view = size();
        double fit = std::min(double(view.width() - 12) / cur.width(),
                              double(view.height() - 12) / cur.height());
        double w = cur.width() * fit, h = cur.height() * fit;
        QRectF r((view.width() - w) / 2.0, (view.height() - h) / 2.0, w, h);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, cur);
        p.setPen(QPen(QColor(0x30, 0x30, 0x30), 1));
        p.drawRect(r.adjusted(-1, -1, 0, 0));
    }
    void resizeEvent(QResizeEvent*) override { update(); }

private:
    QTimer* tick = nullptr;
    std::thread dec;
    std::mutex mtx;
    std::condition_variable cv_full;
    std::deque<QImage> ring;
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> dec_busy{false};

    void stop_decoder() {
        stop_flag = true;
        cv_full.notify_all();
        if (dec.joinable()) dec.join();
        stop_flag = false;
        dec_busy = false;
        std::lock_guard<std::mutex> lk(mtx);
        ring.clear();
    }

    void start_decoder(double from_sec) {
        stop_decoder();
        dec_busy = true;
        dec = std::thread([this, from_sec] { decode_loop(from_sec); });
    }

    void fetch_one(long long frame_no) {
        stop_decoder();
        dec_busy = true;
        dec = std::thread([this, frame_no] {
            QImage img = grab_frame(frame_no);
            if (!img.isNull()) {
                std::lock_guard<std::mutex> lk(mtx);
                ring.push_back(img);
            }
            QMetaObject::invokeMethod(this, [this] {
                std::lock_guard<std::mutex> lk2(mtx);
                if (!ring.empty()) {
                    cur = ring.front();
                    ring.pop_front();
                    update();
                }
            }, Qt::QueuedConnection);
            dec_busy = false;
        });
    }

    QImage grab_frame(long long frame_no) {
        double sec = double(frame_no) / fps();
        char ss[32];
        std::snprintf(ss, sizeof(ss), "%.3f", sec);
        neon::Proc p;
        if (!p.spawn({"ffmpeg", "-hide_banner", "-loglevel", "error",
                      "-ss", ss, "-i", path.toUtf8().constData(),
                      "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgr24", "-"},
                     true, false))
            return QImage();
        size_t need = size_t(info.w) * size_t(info.h) * 3;
        std::vector<unsigned char> buf(need);
        size_t got = 0;
        while (got < need) {
            size_t n = std::fread(buf.data() + got, 1, need - got, p.out);
            if (n == 0) break;
            got += n;
        }
        p.wait_close();
        if (got < need) return QImage();
        QImage img(buf.data(), info.w, info.h, int(info.w * 3), QImage::Format_BGR888);
        return img.copy();
    }

    void decode_loop(double start_sec) {
        char ss[32];
        std::snprintf(ss, sizeof(ss), "%.3f", std::max(0.0, start_sec));
        std::vector<std::string> av = {"ffmpeg", "-hide_banner", "-loglevel", "error"};
        if (start_sec > 0.01) av.push_back("-ss"), av.push_back(ss);
        av.push_back("-i");
        av.push_back(path.toUtf8().constData());
        av.push_back("-f"); av.push_back("rawvideo");
        av.push_back("-pix_fmt"); av.push_back("bgr24");
        av.push_back("-");
        neon::Proc p;
        if (!p.spawn(av, true, false)) {
            QMetaObject::invokeMethod(this, [this] { emit decode_failed(QStringLiteral("cannot spawn ffmpeg")); },
                                      Qt::QueuedConnection);
            dec_busy = false;
            return;
        }
        size_t need = size_t(info.w) * size_t(info.h) * 3;
        std::vector<unsigned char> buf(need);
        size_t cap = size_t(std::max(6.0, fps() * 2.0));
        while (!stop_flag) {
            size_t got = 0;
            while (got < need) {
                size_t n = std::fread(buf.data() + got, 1, need - got, p.out);
                if (n == 0) break;
                got += n;
            }
            if (got < need) break;
            QImage img(buf.data(), info.w, info.h, int(info.w * 3), QImage::Format_BGR888);
            QImage own = img.copy();
            {
                std::unique_lock<std::mutex> lk(mtx);
                cv_full.wait(lk, [this, cap] { return stop_flag || ring.size() < cap; });
                if (stop_flag) break;
                ring.push_back(own);
            }
        }
        std::fclose(p.out);
        p.out = nullptr;
        p.wait_close();
        dec_busy = false;
    }
};

static QString fmt_clock(qint64 ms) {
    int s = int(ms / 1000);
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// ---------------------------------------------------------------- video page
// canvas + real controls: play, seek with live time, speed, full duration
class VideoPlayerPage : public QWidget {
    Q_OBJECT

public:
    VideoCanvas* canvas = nullptr;
    QPushButton* play_btn = nullptr;
    QSlider* seek = nullptr;
    QComboBox* speed_combo = nullptr;
    QLabel* time_lbl = nullptr;
    bool user_seeking = false;

    VideoPlayerPage(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(2);
        canvas = new VideoCanvas(this);
        l->addWidget(canvas, 1);
        QHBoxLayout* ctrl = new QHBoxLayout;
        ctrl->setContentsMargins(8, 2, 8, 4);
        play_btn = new QPushButton("Play", this);
        seek = new QSlider(Qt::Horizontal, this);
        seek->setRange(0, 0);
        speed_combo = new QComboBox(this);
        for (double s : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
            speed_combo->addItem(QString::number(s, 'f', 2) + "x", s);
        speed_combo->setCurrentIndex(3);
        time_lbl = new QLabel("0:00 / 0:00", this);
        time_lbl->setStyleSheet("color:#888888;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(new QLabel("speed", this));
        ctrl->addWidget(speed_combo);
        ctrl->addWidget(time_lbl);
        l->addLayout(ctrl);

        connect(play_btn, &QPushButton::clicked, canvas, &VideoCanvas::play_pause_slot);
        connect(canvas, &VideoCanvas::state_changed, this, &VideoPlayerPage::sync_ui);
        connect(speed_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            canvas->set_speed(speed_combo->currentData().toDouble());
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderMoved, this, [this](int v) {
            time_lbl->setText(QString("%1 / %2").arg(fmt_clock(v))
                                  .arg(fmt_clock(std::max<qint64>(0, canvas->total_ms()))));
            double f = canvas->total_ms() > 0 ? double(v) / double(canvas->total_ms()) : 0.0;
            (void)f;
        });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            canvas->seek_ms(seek->value());
        });
        QTimer* poll = new QTimer(this);
        poll->setInterval(33);
        connect(poll, &QTimer::timeout, this, &VideoPlayerPage::sync_ui);
        poll->start();
    }

    void sync_ui() {
        if (user_seeking) {
            play_btn->setText(canvas->playing ? "Pause" : "Play");
            return;
        }
        qint64 pos = canvas->position_ms();
        qint64 total = std::max<qint64>(0, canvas->total_ms());
        seek->blockSignals(true);
        seek->setRange(0, int(total));
        seek->setValue(int(std::min(pos, total)));
        seek->blockSignals(false);
        time_lbl->setText(QString("%1 / %2").arg(fmt_clock(pos)).arg(fmt_clock(total)));
        play_btn->setText(canvas->playing ? "Pause" : "Play");
    }

    void load(const QString& path) {
        canvas->load(path);
        sync_ui();
    }
    void play() { canvas->play(); }
    void pause() { canvas->pause(); }
    void stop() { canvas->stop(); }
};

// ---------------------------------------------------------------- audio page
// waveform + play/pause + seek + speed through the pcm engine
class AudioWavePage : public QWidget {
    Q_OBJECT

public:
    WaveformWidget* wave = nullptr;
    QPushButton* play_btn = nullptr;
    QSlider* seek = nullptr;
    QComboBox* speed_combo = nullptr;
    QLabel* time_lbl = nullptr;
    AudioEngine* engine = nullptr;
    QString current;
    bool user_seek = false;

    AudioWavePage(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* l = new QVBoxLayout(this);
        l->setContentsMargins(12, 10, 12, 10);
        l->setSpacing(6);
        wave = new WaveformWidget(this);
        l->addWidget(wave, 1);
        QHBoxLayout* ctrl = new QHBoxLayout;
        play_btn = new QPushButton("Play", this);
        seek = new QSlider(Qt::Horizontal, this);
        seek->setRange(0, 0);
        speed_combo = new QComboBox(this);
        for (double s : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
            speed_combo->addItem(QString::number(s, 'f', 2) + "x", s);
        speed_combo->setCurrentIndex(3);
        time_lbl = new QLabel("0:00 / 0:00", this);
        time_lbl->setStyleSheet("color:#666666;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(new QLabel("speed", this));
        ctrl->addWidget(speed_combo);
        ctrl->addWidget(time_lbl);
        l->addLayout(ctrl);

        engine = new AudioEngine(this);
        connect(play_btn, &QPushButton::clicked, engine, &AudioEngine::toggle);
        connect(engine, &AudioEngine::stateChanged, this, [this] {
            play_btn->setText(engine->is_playing() ? "Pause" : "Play");
        });
        connect(engine, &AudioEngine::decoded, this, [this] {
            wave->message.clear();
            seek->setRange(0, int(engine->duration_ms()));
            play_btn->setEnabled(true);
        });
        connect(engine, &AudioEngine::failed, this, [this](const QString& m) {
            wave->message = m;
            wave->update();
            play_btn->setEnabled(false);
        });
        connect(speed_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            engine->set_speed(speed_combo->currentData().toDouble());
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seek = true; });
        connect(seek, &QSlider::sliderMoved, this, [this](int v) {
            time_lbl->setText(QString("%1 / %2").arg(fmt_clock(v))
                                  .arg(fmt_clock(std::max<qint64>(0, engine->duration_ms()))));
            double f = engine->duration_ms() > 0 ? double(v) / double(engine->duration_ms()) : 0.0;
            wave->set_pos(f);
        });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seek = false;
            engine->seek_ms(seek->value());
        });
        QTimer* smooth = new QTimer(this);
        smooth->setInterval(16);
        smooth->setTimerType(Qt::PreciseTimer);
        smooth->start();
        connect(smooth, &QTimer::timeout, this, [this] {
            if (user_seek || !engine->ready()) return;
            qint64 pos = engine->pos_ms();
            qint64 dur = engine->duration_ms();
            double f = dur > 0 ? double(pos) / double(dur) : 0.0;
            wave->set_pos(f);
            if (!user_seek) {
                seek->blockSignals(true);
                seek->setValue(int(std::min<qint64>(pos, dur)));
                seek->blockSignals(false);
            }
            time_lbl->setText(QString("%1 / %2").arg(fmt_clock(pos)).arg(fmt_clock(dur)));
        });
    }

    void refresh_time(qint64 ms) {
        time_lbl->setText(QString("%1 / %2").arg(fmt_clock(ms))
                              .arg(fmt_clock(std::max<qint64>(0, engine->duration_ms()))));
    }

    void load(const QString& path) {
        current = path;
        wave->set_pcm(std::vector<float>());
        wave->message = QStringLiteral("decoding\xe2\x80\xa6");
        wave->update();
        play_btn->setEnabled(false);
        seek->setRange(0, 0);
        refresh_time(0);
        engine->load(path);
        if (wave_dec.joinable()) wave_dec.join();
        wave_dec = std::thread([this, path] {
            QByteArray wave_pcm = decode_wave_pcm(path);
            if (!wave_alive) return;
            QMetaObject::invokeMethod(this, [this, wave_pcm] {
                wave->set_pcm(bytes_to_pcm(wave_pcm));
            }, Qt::QueuedConnection);
        });
    }

    void stop() {
        engine->stop();
        play_btn->setText("Play");
    }

private:
    std::thread wave_dec;
    std::atomic<bool> wave_alive{true};

public:
    ~AudioWavePage() override {
        wave_alive = false;
        if (wave_dec.joinable()) wave_dec.join();
    }
};

// ---------------------------------------------------------------- single viewer
class SingleViewer : public QStackedWidget {
public:
    ImageCanvas* canvas = nullptr;
    VideoPlayerPage* video = nullptr;
    AudioWavePage* audio_page = nullptr;
    QLabel* text_label = nullptr;

    SingleViewer(QWidget* parent = nullptr) : QStackedWidget(parent) {
        canvas = new ImageCanvas(this);
        addWidget(canvas);
        video = new VideoPlayerPage(this);
        addWidget(video);
        audio_page = new AudioWavePage(this);
        addWidget(audio_page);
        text_label = new QLabel(this);
        text_label->setAlignment(Qt::AlignCenter);
        text_label->setWordWrap(true);
        text_label->setStyleSheet("background:#0e0e0e;color:#666666;font-size:13px;");
        addWidget(text_label);
        setCurrentIndex(0);
    }

    void show_image(const QPixmap& pm) {
        canvas->set_pixmap(pm);
        setCurrentWidget(canvas);
    }

    void show_video(const QString& path) {
        video->load(path);
        setCurrentWidget(video);
    }

    void show_audio() {
        setCurrentWidget(audio_page);
    }

    void show_text(const QString& t) {
        text_label->setText(t);
        setCurrentWidget(text_label);
    }

    void reset() {
        canvas->set_pixmap(QPixmap());
        video->stop();
        audio_page->stop();
        audio_page->current.clear();
        audio_page->wave->set_pcm(std::vector<float>());
        audio_page->refresh_time(0);
        setCurrentIndex(0);
    }
};

// ---------------------------------------------------------------- side by side
class SideBySideView : public QWidget {
public:
    QPixmap before, after;
    QString before_name, after_name;

    SideBySideView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(320, 240);
    }

    void set_pair(const QPixmap& b, const QString& bn, const QPixmap& a, const QString& an) {
        before = b; before_name = bn;
        after = a; after_name = an;
        update();
    }

    QRect scaled_rect(const QPixmap& pm, const QRect& area) const {
        if (pm.isNull()) return QRect();
        QSize s = pm.size().scaled(area.size(), Qt::KeepAspectRatio);
        int x = area.x() + (area.width() - s.width()) / 2;
        int y = area.y() + (area.height() - s.height()) / 2;
        return QRect(x, y, s.width(), s.height());
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        int half = width() / 2;
        p.setPen(QPen(C_LINE(), 1));
        p.drawLine(half, 0, half, height());
        QRect L(0, 20, half, height() - 20), R(half, 20, width() - half, height() - 20);
        if (!before.isNull()) p.drawPixmap(scaled_rect(before, L), before);
        else { p.setPen(C_DIM()); p.drawText(L, Qt::AlignCenter, "original removed"); }
        if (!after.isNull()) p.drawPixmap(scaled_rect(after, R), after);
        else { p.setPen(C_DIM()); p.drawText(R, Qt::AlignCenter, "no result"); }
        p.setPen(C_TEXT());
        QFont f = p.font(); f.setPointSize(9); p.setFont(f);
        p.drawText(QRect(8, 2, half - 16, 16), Qt::AlignLeft | Qt::AlignVCenter, before_name);
        p.drawText(QRect(half + 8, 2, width() - half - 16, 16), Qt::AlignLeft | Qt::AlignVCenter, after_name);
    }
};

// ---------------------------------------------------------------- video compare
class VideoCompare : public QWidget {
public:
    VideoCanvas* cv[2] = {nullptr, nullptr};
    QSlider* seek = nullptr;
    QPushButton* play_btn = nullptr;
    QComboBox* speed_combo = nullptr;
    QLabel* time_lbl = nullptr;
    QLabel* names[2] = {nullptr, nullptr};
    bool user_seeking = false;

    VideoCompare(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(2);
        QHBoxLayout* tops = new QHBoxLayout;
        for (int i = 0; i < 2; i++) {
            QWidget* side = new QWidget(this);
            QVBoxLayout* sl = new QVBoxLayout(side);
            sl->setContentsMargins(0, 0, 0, 0);
            sl->setSpacing(0);
            names[i] = new QLabel(i == 0 ? QStringLiteral("original") : QStringLiteral("result"), side);
            names[i]->setAlignment(Qt::AlignCenter);
            names[i]->setStyleSheet("color:#e5e5e5;background:#161616;font-size:11px;padding:2px;");
            cv[i] = new VideoCanvas(side);
            sl->addWidget(names[i]);
            sl->addWidget(cv[i], 1);
            tops->addWidget(side);
        }
        root->addLayout(tops, 1);

        QHBoxLayout* ctrl = new QHBoxLayout;
        ctrl->setContentsMargins(6, 2, 6, 2);
        play_btn = new QPushButton("Play");
        seek = new QSlider(Qt::Horizontal);
        seek->setRange(0, 0);
        speed_combo = new QComboBox;
        for (double s : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
            speed_combo->addItem(QString::number(s, 'f', 2) + "x", s);
        speed_combo->setCurrentIndex(3);
        time_lbl = new QLabel("0:00 / 0:00");
        time_lbl->setStyleSheet("color:#888888;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(new QLabel("speed"));
        ctrl->addWidget(speed_combo);
        ctrl->addWidget(time_lbl);
        root->addLayout(ctrl);

        connect(play_btn, &QPushButton::clicked, this, [this] {
            bool any_playing = cv[0]->playing || cv[1]->playing;
            if (any_playing) { cv[0]->pause(); cv[1]->pause(); }
            else {
                if (cv[0]->reached_end || cv[1]->reached_end) {
                    cv[0]->seek_ms(0);
                    cv[1]->seek_ms(0);
                }
                cv[0]->play();
                cv[1]->play();
            }
        });
        for (int i = 0; i < 2; i++) {
            connect(cv[i], &VideoCanvas::state_changed, this, &VideoCompare::sync_state);
            connect(cv[i], &VideoCanvas::decode_failed, this, [this](const QString& m) {
                time_lbl->setText(m);
            });
        }
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderMoved, this, [this](int v) {
            time_lbl->setText(QString("%1 / %2").arg(fmt_clock(v))
                                  .arg(fmt_clock(std::max<qint64>(0, cv[0]->total_ms()))));
        });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            cv[0]->seek_ms(seek->value());
            cv[1]->seek_ms(seek->value());
        });
        connect(speed_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            double s = speed_combo->currentData().toDouble();
            cv[0]->set_speed(s);
            cv[1]->set_speed(s);
        });
    }

    void sync_state() {
        if (user_seeking) return;
        qint64 pos = cv[0]->position_ms();
        qint64 total = std::max(cv[0]->total_ms(), cv[1]->total_ms());
        seek->blockSignals(true);
        seek->setRange(0, int(std::max<qint64>(0, total)));
        seek->setValue(int(pos));
        seek->blockSignals(false);
        time_lbl->setText(QString("%1 / %2").arg(fmt_clock(pos))
                              .arg(fmt_clock(std::max<qint64>(0, total))));
        play_btn->setText((cv[0]->playing || cv[1]->playing) ? "Pause" : "Play");
    }

    void load(const QString& a, const QString& b) {
        stop_all();
        cv[0]->load(a);
        cv[1]->load(b);
        names[0]->setText(QFileInfo(a).fileName());
        names[1]->setText(QFileInfo(b).fileName());
        seek->setValue(0);
        sync_state();
    }

    void stop_all() {
        cv[0]->pause();
        cv[1]->pause();
    }
};

// ---------------------------------------------------------------- audio compare
class AudioCompare : public QWidget {
public:
    WaveformWidget* wave[2] = {nullptr, nullptr};
    QSlider* seek = nullptr;
    QPushButton* play_btn = nullptr;
    QLabel* names[2] = {nullptr, nullptr};
    QLabel* pos_lbl[2] = {nullptr, nullptr};
    AudioEngine* eng[2] = {nullptr, nullptr};
    bool user_seeking = false;

    AudioCompare(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 10, 16, 10);
        root->setSpacing(8);
        for (int i = 0; i < 2; i++) {
            QVBoxLayout* side = new QVBoxLayout;
            names[i] = new QLabel(i == 0 ? QStringLiteral("original") : QStringLiteral("result"), this);
            names[i]->setStyleSheet("color:#e5e5e5;font-size:11px;");
            wave[i] = new WaveformWidget(this);
            pos_lbl[i] = new QLabel("0:00 / 0:00", this);
            pos_lbl[i]->setStyleSheet("color:#666666;font-size:11px;");
            side->addWidget(names[i]);
            side->addWidget(wave[i], 1);
            side->addWidget(pos_lbl[i]);
            root->addLayout(side, 1);
        }
        QHBoxLayout* ctrl = new QHBoxLayout;
        play_btn = new QPushButton("Play");
        seek = new QSlider(Qt::Horizontal);
        seek->setRange(0, 0);
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        root->addLayout(ctrl);

        for (int i = 0; i < 2; i++) {
            eng[i] = new AudioEngine(this);
            connect(eng[i], &AudioEngine::decoded, this, [this] {
                qint64 d = std::max(eng[0]->duration_ms(), eng[1]->duration_ms());
                if (d > 0) seek->setRange(0, int(d));
            });
            connect(eng[i], &AudioEngine::failed, this, [this, i](const QString& m) {
                wave[i]->message = m;
                wave[i]->update();
            });
        }
        connect(play_btn, &QPushButton::clicked, this, [this] {
            bool any = eng[0]->is_playing() || eng[1]->is_playing();
            if (any) { eng[0]->pause(); eng[1]->pause(); }
            else { eng[0]->play(); eng[1]->play(); }
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderMoved, this, [this](int v) {
            pos_lbl[0]->setText(QString("%1 / %2").arg(fmt_clock(v))
                                    .arg(fmt_clock(std::max<qint64>(0, seek->maximum()))));
        });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            eng[0]->seek_ms(seek->value());
            eng[1]->seek_ms(seek->value());
        });
        QTimer* smooth = new QTimer(this);
        smooth->setInterval(16);
        smooth->setTimerType(Qt::PreciseTimer);
        smooth->start();
        connect(smooth, &QTimer::timeout, this, [this] {
            if (user_seeking) return;
            qint64 pos = std::max(eng[0]->pos_ms(), eng[1]->pos_ms());
            qint64 dur = std::max(eng[0]->duration_ms(), eng[1]->duration_ms());
            seek->blockSignals(true);
            seek->setValue(int(std::min<qint64>(pos, dur)));
            seek->blockSignals(false);
            double f = dur > 0 ? double(pos) / double(dur) : 0.0;
            wave[0]->set_pos(f);
            wave[1]->set_pos(f);
            pos_lbl[0]->setText(QString("%1 / %2").arg(fmt_clock(pos)).arg(fmt_clock(dur)));
            pos_lbl[1]->setText(QString("%1 / %2").arg(fmt_clock(pos)).arg(fmt_clock(dur)));
            play_btn->setText((eng[0]->is_playing() || eng[1]->is_playing()) ? "Pause" : "Play");
        });
    }

    void load(const QString& a, const QString& b) {
        stop_all();
        for (int i = 0; i < 2; i++) {
            wave[i]->message = QStringLiteral("decoding\xe2\x80\xa6");
            wave[i]->update();
        }
        names[0]->setText(QFileInfo(a).fileName());
        names[1]->setText(QFileInfo(b).fileName());
        seek->setValue(0);
        eng[0]->load(a);
        eng[1]->load(b);
        const QString paths[2] = {a, b};
        for (int i = 0; i < 2; i++) {
            if (wave_dec[i].joinable()) wave_dec[i].join();
            wave_dec[i] = std::thread([this, i, paths] {
                QByteArray pcm = decode_wave_pcm(paths[i]);
                if (!wave_alive) return;
                QMetaObject::invokeMethod(this, [this, i, pcm] {
                    wave[i]->set_pcm(bytes_to_pcm(pcm));
                }, Qt::QueuedConnection);
            });
        }
    }

    void stop_all() {
        eng[0]->stop();
        eng[1]->stop();
    }

    ~AudioCompare() override {
        wave_alive = false;
        for (int i = 0; i < 2; i++)
            if (wave_dec[i].joinable()) wave_dec[i].join();
    }

private:
    std::thread wave_dec[2];
    std::atomic<bool> wave_alive{true};
};

}  // namespace neon_gui
