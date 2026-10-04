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
#include <QFontMetrics>
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

// ---------------------------------------------------------------- two-tone progress
// the fill is white and the label is white, so the label used to vanish
// wherever the fill crossed it. the overlap is computed here: the label is
// drawn twice, clipped against the fill's rect it turns black, on the rest
// it stays white. a plain QWidget on purpose — QProgressBar stylesheet rules
// must not reach this paint.
class TwoToneBar : public QWidget {
    Q_OBJECT

public:
    TwoToneBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(16);
        setMinimumWidth(80);
    }

    void set_range(int lo, int hi) { lo_ = lo; hi_ = hi; val_ = lo; update(); }
    void set_value(int v) {
        val_ = std::max(lo_, std::min(hi_, v));
        update();
    }
    void set_display(const QString& t) {
        display_ = t;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = rect().adjusted(0.5, 0.5, -1.5, -1.5);
        p.setPen(QColor(0x24, 0x24, 0x24));
        p.setBrush(QColor(0x12, 0x12, 0x12));
        p.drawRoundedRect(r, 5, 5);
        double frac = (hi_ > lo_) ? double(val_ - lo_) / double(hi_ - lo_) : 0.0;
        QRectF chunk;
        if (frac > 0.0) {
            chunk = r.adjusted(1, 1, -1, -1);
            chunk.setWidth(std::max(chunk.width() * frac, chunk.height()));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xe5, 0xe5, 0xe5));
            p.drawRoundedRect(chunk, 4, 4);
        }
        QString t = display_;
        if (t.isEmpty()) t = QString::number(int(std::round(frac * 100.0))) + "%";
        QFontMetrics fm(font());
        QRectF tr((width() - (tw_ = fm.horizontalAdvance(t))) / 2.0,
                  (height() - fm.height()) / 2.0, tw_ + 2, fm.height());
        p.setFont(font());
        if (frac > 0.0 && chunk.isValid()) {
            // where the label crosses the white fill: black
            p.setPen(QColor(0x11, 0x11, 0x11));
            p.setClipRect(chunk);
            p.drawText(tr, Qt::AlignCenter, t);
            // everywhere else on the label: white
            QRegion outside(tr.toRect());
            outside = outside.subtracted(QRegion(chunk.toRect()));
            p.setClipRegion(outside);
            p.setPen(QColor(0xe5, 0xe5, 0xe5));
            p.drawText(tr, Qt::AlignCenter, t);
            p.setClipping(false);
        } else {
            p.setPen(QColor(0xe5, 0xe5, 0xe5));
            p.drawText(tr, Qt::AlignCenter, t);
        }
    }

private:
    int lo_ = 0, hi_ = 100, val_ = 0;
    int tw_ = 0;
    QString display_;
};

// ---------------------------------------------------------------- image decode
// the engine reads images with opencv — the gui previews do the same, so the
// gui can open exactly what the engine opens (jpg included, static qt has no
// qjpeg plugin)
inline QImage load_image_robust(const QString& path) {
    std::vector<uint8_t> bytes = neon::read_file_bytes(path.toStdString());
    if (bytes.empty()) return QImage();
    cv::Mat img;
    try {
        img = cv::imdecode(bytes, cv::IMREAD_COLOR);
    } catch (const cv::Exception&) {
        img = cv::Mat();
    }
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
    if (!p.spawn({"ffmpeg", "-nostdin", "-v", "error", "-i", path.toUtf8().constData(),
                  "-ac", std::to_string(ch), "-ar", std::to_string(sr), "-f", fmt, "-"},
                 true, false, true))
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
        if (playing) start_output(qint64(at));
    }

    qint64 pos_ms() const {
        if (!have_pcm) return base_ms;
        if (!playing || !out) return base_ms;
        double dev_us = double(out->processedUSecs());
        // the stretched buffer advances the media `speed` microseconds per
        // device microsecond; pause re-bases exactly because this only runs
        // while playing
        return qint64(std::min(base_ms + dev_us / 1000.0 * speed, double(dur_ms)));
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
        qint64 buf_frames = qint64(buf.size()) / 4;
        int free_b = out->bytesFree();
        while (free_b > 0 && fed_frames < buf_frames) {
            qint64 byte_at = fed_frames * 4;
            int chunk = int(std::min<qint64>(std::min<qint64>(free_b, 16384), buf.size() - byte_at));
            if (!dev) break;
            qint64 wrote = dev->write(buf.constData() + byte_at, chunk);
            if (wrote <= 0) break;
            fed_frames += wrote / 4;
            free_b -= int(wrote);
        }
        qint64 total_dev_us = qint64(double(dur_ms) * 1000.0 / speed);
        if (fed_frames >= buf_frames && out->processedUSecs() >= total_dev_us + 60000) {
            base_ms = dur_ms;
            pause();
            emit finished();
        }
    }

private:
    // playback law: stretch the source to `frames / speed` frames, play it
    // linearly at 44100. speed 0.5 doubles the samples (slower), speed 2 halves
    // them (faster) — the old code had this inverted, which is why slower
    // played faster. positions track written frames, so nothing jumps.
    const QByteArray& buffer_for(double s) {
        auto it = cache.find(s);
        if (it != cache.end()) return it->second;
        if (s == 1.0) return cache.emplace(s, src).first->second;
        const int16_t* in = reinterpret_cast<const int16_t*>(src.constData());
        size_t frames_in = size_t(src.size()) / 4;
        if (frames_in < 2) return cache.emplace(s, src).first->second;
        size_t frames_out = size_t(double(frames_in) / s);
        QByteArray resampled;
        resampled.resize(int(frames_out * 4));
        int16_t* o = reinterpret_cast<int16_t*>(resampled.data());
        for (size_t i = 0; i < frames_out; i++) {
            double t = double(i) * s;
            size_t i0 = size_t(t);
            if (i0 >= frames_in) i0 = frames_in - 1;
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
        fed_frames = 0;
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
        const QByteArray& buf = buffer_for(speed);
        qint64 buf_frames = qint64(buf.size()) / 4;
        fed_frames = std::min<qint64>(qint64(double(from_ms) / 1000.0 * 44100.0 / speed), buf_frames);
        base_ms = from_ms;
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
    qint64 fed_frames = 0;
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

// ---------------------------------------------------------------- zoom canvas
// one zoom/pan law for images and video frames. the media always fits the
// walls first. zoom ceiling scales with the media: every 100x100 pixel block
// buys another 2x, so a 4000x4000 shot reaches 40x (4000%) — sqrt(w*h)/100.
// the zoom anchor is the mouse point (ctrl+wheel or ctrl+up/down), panning is
// animated with an ease-out glide and arrow-key panning ramps 1% -> 10% of the
// viewport per second while held. nothing moves in instant jumps.
class ZoomCanvas : public QWidget {
    Q_OBJECT

public:
    ZoomCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        anim = new QTimer(this);
        anim->setInterval(16);
        anim->setTimerType(Qt::PreciseTimer);
        connect(anim, &QTimer::timeout, this, &ZoomCanvas::step_anim);
    }

    void set_media(const QImage& img) {
        bool resized = img.size() != media.size();
        media = img;
        if (!media.isNull()) message.clear();
        if (resized || zoom_ <= 0) fit();
        update();
    }

    bool has_media() const { return !media.isNull(); }

    QString message;

    double zoom_pct() const { return zoom_ * 100.0; }

    double max_zoom() const {
        if (media.isNull()) return 1.0;
        double m = std::sqrt(double(media.width()) * double(media.height())) / 100.0;
        return std::max(1.0, m);
    }

    double fit_zoom() const {
        if (media.isNull()) return 1.0;
        double fw = double(width() - 16) / media.width();
        double fh = double(height() - 16) / media.height();
        return std::min(fw, fh);
    }

    void fit() {
        zoom_ = std::max(0.01, fit_zoom());
        center_media();
        update();
        emit zoom_changed(zoom_pct());
        emit view_changed(zoom_);
    }

    void set_zoom_pct(double pct) {
        zoom_to_point(QPointF(width() / 2.0, height() / 2.0), pct / 100.0);
    }

    // the media point under widget_pos stays under widget_pos afterwards
    void zoom_to_point(QPointF widget_pos, double target) {
        if (media.isNull()) return;
        double lo = std::max(0.01, std::min(fit_zoom() * 0.5, 1.0));
        double hi = max_zoom();
        target = std::max(lo, std::min(hi, target));
        QPointF media_pt = (widget_pos - offset_) / zoom_;
        offset_ = widget_pos - media_pt * target;
        zoom_ = target;
        clamp_offset();
        update();
        emit zoom_changed(zoom_pct());
        emit view_changed(zoom_);
    }

    double zoom_factor() const { return zoom_; }
    QPointF view_offset() const { return offset_; }

    // a synced pane takes the leader's zoom and pan verbatim (compare mode:
    // both sides always look at the same spot); this path never re-emits
    void follow_view(double zoom, QPointF off) {
        if (media.isNull()) return;
        zoom_ = std::max(0.01, std::min(max_zoom(), zoom));
        offset_ = off;
        clamp_offset();
        update();
    }

signals:
    void zoom_changed(double pct);
    void view_changed(double zoom);

protected:
    QRectF media_rect() const {
        return QRectF(offset_, QSizeF(media.width() * zoom_, media.height() * zoom_));
    }

    void center_media() {
        if (media.isNull()) {
            offset_ = QPointF(0, 0);
            return;
        }
        offset_ = QPointF((width() - media.width() * zoom_) / 2.0,
                          (height() - media.height() * zoom_) / 2.0);
    }

    void clamp_offset() {
        if (media.isNull()) return;
        double mw = media.width() * zoom_, mh = media.height() * zoom_;
        double margin_x = std::max(0.0, (width() - mw) / 2.0);
        double margin_y = std::max(0.0, (height() - mh) / 2.0);
        if (mw <= width()) {
            offset_.setX((width() - mw) / 2.0);
        } else {
            offset_.setX(std::min(0.0, std::max(width() - mw, offset_.x())));
        }
        if (mh <= height()) {
            offset_.setY((height() - mh) / 2.0);
        } else {
            offset_.setY(std::min(0.0, std::max(height() - mh, offset_.y())));
        }
        (void)margin_x;
        (void)margin_y;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        if (media.isNull()) {
            if (!message.isEmpty()) {
                p.setPen(C_DIM());
                p.drawText(rect(), Qt::AlignCenter, message);
            }
            return;
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QRectF r = media_rect();
        p.drawImage(r, media, QRectF(media.rect()));
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.setPen(QPen(QColor(0x30, 0x30, 0x30), 1));
        p.drawRect(r.adjusted(-1, -1, 0, 0));
    }

    void resizeEvent(QResizeEvent*) override {
        if (zoom_ > 0 && !media.isNull()) {
            clamp_offset();
            update();
        }
    }

    void wheelEvent(QWheelEvent* e) override {
        if (media.isNull()) return;
        if (e->modifiers() & Qt::ControlModifier) {
            double step = std::pow(1.0018, double(e->angleDelta().y()));
            zoom_to_point(e->posF(), zoom_ * step);
        } else {
            pan_target_ += QPointF(0.0, -double(e->angleDelta().y()) * 0.6);
            glide();
            emit view_changed(zoom_);
        }
    }

    void keyPressEvent(QKeyEvent* e) override {
        QPointF dir;
        switch (e->key()) {
            case Qt::Key_Up: dir = QPointF(0, -1); break;
            case Qt::Key_Down: dir = QPointF(0, 1); break;
            case Qt::Key_Left: dir = QPointF(-1, 0); break;
            case Qt::Key_Right: dir = QPointF(1, 0); break;
            default: QWidget::keyPressEvent(e); return;
        }
        if (e->modifiers() & Qt::ControlModifier) {
            // zoom at the pointer (or viewport center when the mouse is away)
            double step = e->key() == Qt::Key_Up ? 1.12 : (e->key() == Qt::Key_Down ? 1.0 / 1.12 : 1.0);
            if (step != 1.0) {
                QPointF anchor = mapFromGlobal(QCursor::pos());
                if (!rect().contains(anchor.toPoint())) anchor = QPointF(width() / 2.0, height() / 2.0);
                zoom_to_point(anchor, zoom_ * step);
            }
            return;
        }
        pan_dir_ = dir;
        hold_start_ = std::chrono::steady_clock::now();
        if (!anim->isActive()) anim->start();
    }

    void keyReleaseEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down ||
            e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
            pan_dir_ = QPointF();
        }
        QWidget::keyReleaseEvent(e);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_ = true;
            drag_from_ = e->localPos();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) {
            QPointF d = e->localPos() - drag_from_;
            drag_from_ = e->localPos();
            offset_ += d;
            clamp_offset();
            update();
            emit view_changed(zoom_);
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_ = false;
            setCursor(Qt::ArrowCursor);
        }
    }

private slots:
    // one easing law for everything: the visible offset chases its target with
    // an exponential glide, so wheel pans and key pans arrive smoothly
    void glide() {
        if (!anim->isActive()) anim->start();
    }

    void step_anim() {
        bool moved = false;
        if (!pan_dir_.isNull() && !media.isNull()) {
            double held = std::chrono::duration<double>(std::chrono::steady_clock::now() - hold_start_).count();
            double frac = 0.01 + 0.09 * std::min(1.0, held / 1.2);  // 1% -> 10% of viewport
            pan_target_ += QPointF(pan_dir_.x() * width() * frac * 0.016,
                                   pan_dir_.y() * height() * frac * 0.016);
            moved = true;
        }
        if (pan_target_ != QPointF()) {
            QPointF want = base_offset_ + pan_target_;
            QPointF next = offset_ + (want - offset_) * 0.30;
            if ((want - next).manhattanLength() < 0.5) next = want;
            offset_ = next;
            clamp_offset();
            moved = true;
            emit view_changed(zoom_);
        }
        if (!moved) {
            anim->stop();
            base_offset_ = offset_;
            pan_target_ = QPointF();
            return;
        }
        update();
    }

protected:
    QImage media;
    double zoom_ = 0.0;
    QPointF offset_;
    QPointF base_offset_;
    QPointF pan_target_;
    QPointF pan_dir_;
    std::chrono::steady_clock::time_point hold_start_;
    bool dragging_ = false;
    QPointF drag_from_;
    QTimer* anim = nullptr;
};

// ---------------------------------------------------------------- zoom bar
// the zoom widget: minus, the live percentage, plus, fit and 1:1
class ZoomBar : public QWidget {
public:
    ZoomBar(QWidget* parent = nullptr) : QWidget(parent) {
        QHBoxLayout* l = new QHBoxLayout(this);
        l->setContentsMargins(8, 0, 8, 0);
        l->setSpacing(6);
        out_btn = new QPushButton("-", this);
        in_btn = new QPushButton("+", this);
        fit_btn = new QPushButton("fit", this);
        orig_btn = new QPushButton("100%", this);
        label = new QLabel("100%", this);
        label->setStyleSheet("color:#888888;font-size:11px;min-width:52px;");
        label->setAlignment(Qt::AlignCenter);
        for (QPushButton* b : {out_btn, in_btn, fit_btn, orig_btn}) {
            b->setStyleSheet("QPushButton{background:#1d1d1d;color:#cccccc;border:1px solid #2f2f2f;"
                             "border-radius:4px;padding:2px 10px;font-size:11px;}"
                             "QPushButton:hover{color:#ffffff;border-color:#4a4a4a;}");
            b->setFixedHeight(22);
        }
        l->addWidget(out_btn);
        l->addWidget(label, 1);
        l->addWidget(in_btn);
        l->addWidget(fit_btn);
        l->addWidget(orig_btn);
    }

    void bind(ZoomCanvas* c) {
        canvas = c;
        connect(c, &ZoomCanvas::zoom_changed, this, [this](double pct) {
            label->setText(QString::number(int(pct + 0.5)) + "%");
        });
        connect(out_btn, &QPushButton::clicked, this, [this] {
            canvas->zoom_to_point(QPointF(canvas->width() / 2.0, canvas->height() / 2.0),
                                  canvas->zoom_pct() / 100.0 / 1.25);
        });
        connect(in_btn, &QPushButton::clicked, this, [this] {
            canvas->zoom_to_point(QPointF(canvas->width() / 2.0, canvas->height() / 2.0),
                                  canvas->zoom_pct() / 100.0 * 1.25);
        });
        connect(fit_btn, &QPushButton::clicked, this, [this] { canvas->fit(); });
        connect(orig_btn, &QPushButton::clicked, this, [this] { canvas->set_zoom_pct(100.0); });
    }

    // compare views drive the label from a follower canvas too (the follower
    // path carries the zoom factor, not the percentage)
    void reflect(double factor) {
        label->setText(QString::number(int(factor * 100.0 + 0.5)) + "%");
    }

private:
    ZoomCanvas* canvas = nullptr;
    QPushButton* out_btn = nullptr;
    QPushButton* in_btn = nullptr;
    QPushButton* fit_btn = nullptr;
    QPushButton* orig_btn = nullptr;
    QLabel* label = nullptr;
};

// ---------------------------------------------------------------- video canvas
// ffmpeg decodes frames into a small ring; the zoom canvas displays them at
// the file's fps times the chosen speed. no system video backends involved.
class VideoCanvas : public ZoomCanvas {
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

    VideoCanvas(QWidget* parent = nullptr) : ZoomCanvas(parent) {
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
        if (!ok) {
            error = QStringLiteral("cannot open this video (ffmpeg needed)");
            message = error;
        } else {
            message = QStringLiteral("ready \xe2\x80\x94 press play");
        }
        set_media(QImage());
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
        message = QStringLiteral("ready \xe2\x80\x94 press play");
        set_media(QImage());
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
        set_media(cur);
        emit state_changed();
    }

protected:
    void resizeEvent(QResizeEvent*) override { ZoomCanvas::resizeEvent(nullptr); }

private:
    QTimer* tick = nullptr;
    std::thread dec;
    std::mutex mtx;
    std::condition_variable cv_full;
    std::deque<QImage> ring;
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> dec_busy{false};
    // the decoder's children live here, not on the thread's stack, so a stop
    // can kill them from outside — a blocked fread or wait wakes with EOF and
    // the join below returns in milliseconds instead of freezing the gui
    neon::Proc dec_proc;
    neon::Proc grab_proc;

    void stop_decoder() {
        {
            // stop_flag must change under the mutex the decoder waits on — a
            // plain atomic store racing the predicate check is the lost wakeup
            // that froze the whole app on pause
            std::lock_guard<std::mutex> lk(mtx);
            stop_flag = true;
        }
        cv_full.notify_all();
        dec_proc.kill();
        grab_proc.kill();
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
                    set_media(cur);
                }
            }, Qt::QueuedConnection);
            dec_busy = false;
        });
    }

    QImage grab_frame(long long frame_no) {
        double sec = double(frame_no) / fps();
        char ss[32];
        std::snprintf(ss, sizeof(ss), "%.3f", sec);
        if (!grab_proc.spawn({"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                              "-ss", ss, "-i", path.toUtf8().constData(),
                              "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgr24", "-"},
                             true, false, true))
            return QImage();
        size_t need = size_t(info.w) * size_t(info.h) * 3;
        std::vector<unsigned char> buf(need);
        size_t got = 0;
        while (got < need) {
            size_t n = std::fread(buf.data() + got, 1, need - got, grab_proc.out);
            if (n == 0) break;
            got += n;
        }
        grab_proc.wait_close(5000);
        if (got < need) return QImage();
        QImage img(buf.data(), info.w, info.h, int(info.w * 3), QImage::Format_BGR888);
        return img.copy();
    }

    void decode_loop(double start_sec) {
        char ss[32];
        std::snprintf(ss, sizeof(ss), "%.3f", std::max(0.0, start_sec));
        std::vector<std::string> av = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error"};
        if (start_sec > 0.01) av.push_back("-ss"), av.push_back(ss);
        av.push_back("-i");
        av.push_back(path.toUtf8().constData());
        av.push_back("-f"); av.push_back("rawvideo");
        av.push_back("-pix_fmt"); av.push_back("bgr24");
        av.push_back("-");
        if (!dec_proc.spawn(av, true, false, true)) {
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
                size_t n = std::fread(buf.data() + got, 1, need - got, dec_proc.out);
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
        std::fclose(dec_proc.out);
        dec_proc.out = nullptr;
        dec_proc.wait_close(1000);
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
        ZoomBar* video_bar = new ZoomBar(this);
        video_bar->setFixedHeight(28);
        video_bar->bind(canvas);
        l->addWidget(video_bar);
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
    ZoomCanvas* canvas = nullptr;
    VideoPlayerPage* video = nullptr;
    AudioWavePage* audio_page = nullptr;
    QLabel* text_label = nullptr;

    SingleViewer(QWidget* parent = nullptr) : QStackedWidget(parent) {
        QWidget* image_page = new QWidget(this);
        QVBoxLayout* il = new QVBoxLayout(image_page);
        il->setContentsMargins(0, 0, 0, 0);
        il->setSpacing(0);
        canvas = new ZoomCanvas(image_page);
        il->addWidget(canvas, 1);
        ZoomBar* image_bar = new ZoomBar(image_page);
        image_bar->setFixedHeight(28);
        image_bar->bind(canvas);
        il->addWidget(image_bar);
        addWidget(image_page);
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

    void show_image(const QImage& img) {
        canvas->set_media(img);
        setCurrentIndex(0);
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
        setCurrentIndex(3);
    }

    void reset() {
        canvas->set_media(QImage());
        video->stop();
        audio_page->stop();
        audio_page->current.clear();
        audio_page->wave->set_pcm(std::vector<float>());
        audio_page->refresh_time(0);
        setCurrentIndex(0);
    }
};

// ---------------------------------------------------------------- side by side
// the image compare lives under the same zoom law as the single preview:
// two canvases, one zoom bar, and whichever pane the user moves, the other
// follows — both sides always show the same spot at the same magnification
class SideBySideView : public QWidget {
public:
    ZoomCanvas* cv[2] = {nullptr, nullptr};
    QLabel* names[2] = {nullptr, nullptr};

    SideBySideView(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(2);
        QHBoxLayout* tops = new QHBoxLayout;
        tops->setSpacing(1);
        for (int i = 0; i < 2; i++) {
            QWidget* side = new QWidget(this);
            QVBoxLayout* sl = new QVBoxLayout(side);
            sl->setContentsMargins(0, 0, 0, 0);
            sl->setSpacing(0);
            names[i] = new QLabel(i == 0 ? QStringLiteral("original") : QStringLiteral("result"), side);
            names[i]->setAlignment(Qt::AlignCenter);
            names[i]->setStyleSheet("color:#e5e5e5;background:#161616;font-size:11px;padding:2px;");
            cv[i] = new ZoomCanvas(side);
            sl->addWidget(names[i]);
            sl->addWidget(cv[i], 1);
            tops->addWidget(side);
        }
        root->addLayout(tops, 1);
        ZoomBar* zbar = new ZoomBar(this);
        zbar->setFixedHeight(28);
        zbar->bind(cv[0]);
        root->addWidget(zbar);
        for (int i = 0; i < 2; i++) {
            connect(cv[i], &ZoomCanvas::view_changed, this, [this, i, zbar](double zoom) {
                if (syncing_) return;
                syncing_ = true;
                cv[1 - i]->follow_view(zoom, cv[i]->view_offset());
                syncing_ = false;
                zbar->reflect(zoom);
            });
        }
    }

    void set_pair(const QImage& b, const QString& bn, const QImage& a, const QString& an) {
        cv[0]->set_media(b);
        cv[0]->message = b.isNull() ? QStringLiteral("original removed") : QString();
        cv[1]->set_media(a);
        cv[1]->message = a.isNull() ? QStringLiteral("no result") : QString();
        names[0]->setText(bn);
        names[1]->setText(an);
    }

private:
    bool syncing_ = false;
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
    ZoomBar* zbar = nullptr;
    bool user_seeking = false;
    bool syncing_zoom = false;

    VideoCompare(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(2);
        QHBoxLayout* tops = new QHBoxLayout;
        tops->setSpacing(1);
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
        // the zoom widget the single preview has, wired to both panes: zoom
        // or pan one side and the other rides along frame after frame
        zbar = new ZoomBar(this);
        zbar->setFixedHeight(28);
        zbar->bind(cv[0]);
        root->addWidget(zbar);
        for (int i = 0; i < 2; i++) {
            connect(cv[i], &ZoomCanvas::view_changed, this, [this, i](double zoom) {
                if (syncing_zoom) return;
                syncing_zoom = true;
                cv[1 - i]->follow_view(zoom, cv[i]->view_offset());
                syncing_zoom = false;
                zbar->reflect(zoom);
            });
        }

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
