// NEONIFY native gui — center widgets: fixed image canvas, video player,
// waveforms, compares. previews render through ffmpeg, same as the engine.
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
#include <cmath>
#include <QMediaPlayer>
#include <QMediaContent>
#include <QUrl>
#include <QFile>
#include <condition_variable>
#include <deque>
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

// ---------------------------------------------------------------- pcm decode
// ffmpeg -> mono f32 @ 8000hz; empty byte array when ffmpeg or the file is missing
inline QByteArray decode_pcm_bytes(const QString& path) {
    QByteArray all;
    if (!QFile::exists(path)) return all;
    neon::Proc p;
    if (!p.spawn({"ffmpeg", "-v", "error", "-i", path.toUtf8().constData(),
                  "-ac", "1", "-ar", "8000", "-f", "f32le", "-"}, true, false))
        return all;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p.out)) > 0) {
        all.append(buf, int(n));
        if (all.size() > 128 * 1024 * 1024) break;
    }
    p.wait_close();
    return all;
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

// ---------------------------------------------------------------- waveform
// envelope is computed at a fixed column count so the shape never depends on
// when the widget was laid out (the first-show glitch)
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
        int px = int(pos_frac * double(w));
        p.setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
        p.drawLine(px, 0, px, h);
        p.fillRect(px - 1, 0, 3, h, QColor(0xff, 0xff, 0xff, 40));
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
        if (reached_end) emit state_changed();
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

// ---------------------------------------------------------------- audio page
// waveform + play/pause + seek + speed; decode runs off the ui thread
class AudioWavePage : public QWidget {
public:
    WaveformWidget* wave = nullptr;
    QPushButton* play_btn = nullptr;
    QSlider* seek = nullptr;
    QComboBox* speed_combo = nullptr;
    QLabel* time_lbl = nullptr;
    QMediaPlayer* player = nullptr;
    QString current;
    qint64 dur = 0;
    qint64 last_pos = 0;
    QElapsedTimer last_pos_t;
    bool user_seek = false;
    std::thread dec_thread;
    std::atomic<bool> dec_alive{false};
    uint64_t load_gen = 0;

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
        for (double s : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
            speed_combo->addItem(QString::number(s, 'f', 2) + "x", s);
        speed_combo->setCurrentIndex(2);
        time_lbl = new QLabel("0:00 / 0:00", this);
        time_lbl->setStyleSheet("color:#666666;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(new QLabel("speed", this));
        ctrl->addWidget(speed_combo);
        ctrl->addWidget(time_lbl);
        l->addLayout(ctrl);

        player = new QMediaPlayer(this);
        connect(play_btn, &QPushButton::clicked, this, [this] {
            if (current.isEmpty()) return;
            if (player->state() == QMediaPlayer::PlayingState) player->pause();
            else player->play();
        });
        connect(speed_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            player->setPlaybackRate(speed_combo->currentData().toDouble());
        });
        connect(player, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State s) {
            play_btn->setText(s == QMediaPlayer::PlayingState ? "Pause" : "Play");
        });
        connect(player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
            play_btn->setEnabled(false);
            wave->message = QStringLiteral("playback unavailable (%1)").arg(player->errorString());
            wave->update();
        });
        connect(player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
            dur = d;
            seek->setRange(0, int(d));
            refresh_time(last_pos);
        });
        connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
            last_pos = ms;
            last_pos_t.restart();
            double f = dur > 0 ? double(ms) / double(dur) : 0.0;
            wave->set_pos(f);
            if (!user_seek) seek->setValue(int(ms));
            refresh_time(ms);
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seek = true; });
        connect(seek, &QSlider::sliderMoved, this, [this](int v) {
            refresh_time(v);
            double f = dur > 0 ? double(v) / double(dur) : 0.0;
            wave->set_pos(f);
        });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seek = false;
            player->setPosition(seek->value());
            last_pos = seek->value();
            last_pos_t.restart();
        });
        smooth = new QTimer(this);
        smooth->setInterval(33);
        smooth->start();
        connect(smooth, &QTimer::timeout, this, [this] {
            if (user_seek || player->state() != QMediaPlayer::PlayingState) return;
            double rate = std::max(0.1, speed_combo->currentData().toDouble());
            qint64 est = last_pos + qint64(double(last_pos_t.elapsed()) * rate);
            if (dur > 0 && est > dur) est = dur;
            double f = dur > 0 ? double(est) / double(dur) : 0.0;
            wave->set_pos(f);
            seek->blockSignals(true);
            seek->setValue(int(est));
            seek->blockSignals(false);
            refresh_time(est);
        });
    }

    ~AudioWavePage() override {
        dec_alive = false;
        if (dec_thread.joinable()) dec_thread.join();
    }

    static QString fmt(qint64 ms) {
        int s = int(ms / 1000);
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }

    void refresh_time(qint64 ms) {
        time_lbl->setText(QString("%1 / %2").arg(fmt(ms)).arg(fmt(std::max<qint64>(0, dur))));
    }

    void load(const QString& path) {
        stop();
        current = path;
        wave->set_pcm(std::vector<float>());
        wave->message = QStringLiteral("decoding\xe2\x80\xa6");
        wave->update();
        play_btn->setEnabled(true);
        player->setMedia(QMediaContent(QUrl::fromLocalFile(path)));
        dur = 0;
        last_pos = 0;
        seek->setRange(0, 0);
        refresh_time(0);
        dec_alive = true;
        load_gen++;
        uint64_t gen = load_gen;
        if (dec_thread.joinable()) dec_thread.join();
        dec_thread = std::thread([this, path, gen] {
            QByteArray pcm = decode_pcm_bytes(path);
            std::vector<float> v = bytes_to_pcm(pcm);
            QMetaObject::invokeMethod(this, [this, v, gen] {
                if (gen != load_gen) return;
                wave->set_pcm(v);
            }, Qt::QueuedConnection);
        });
    }

    void stop() {
        if (player) player->stop();
        play_btn->setText("Play");
    }

private:
    QTimer* smooth = nullptr;
};

// ---------------------------------------------------------------- single viewer
class SingleViewer : public QStackedWidget {
public:
    ImageCanvas* canvas = nullptr;
    VideoCanvas* video = nullptr;
    AudioWavePage* audio_page = nullptr;
    QLabel* text_label = nullptr;

    SingleViewer(QWidget* parent = nullptr) : QStackedWidget(parent) {
        canvas = new ImageCanvas(this);
        addWidget(canvas);
        video = new VideoCanvas(this);
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
        for (double s : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
            speed_combo->addItem(QString::number(s, 'f', 2) + "x", s);
        speed_combo->setCurrentIndex(2);
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
            time_lbl->setText(QString("%1 / %2").arg(AudioWavePage::fmt(v))
                                  .arg(AudioWavePage::fmt(std::max<qint64>(0, cv[0]->total_ms()))));
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
        time_lbl->setText(QString("%1 / %2").arg(AudioWavePage::fmt(pos))
                              .arg(AudioWavePage::fmt(std::max<qint64>(0, total))));
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
    QMediaPlayer* pl[2] = {nullptr, nullptr};
    QSlider* seek = nullptr;
    QPushButton* play_btn = nullptr;
    QLabel* names[2] = {nullptr, nullptr};
    QLabel* pos_lbl[2] = {nullptr, nullptr};
    qint64 duration = 0;
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
            pos_lbl[i] = new QLabel("0:00", this);
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
            pl[i] = new QMediaPlayer(this);
            connect(pl[i], &QMediaPlayer::durationChanged, this, [this](qint64 d) {
                duration = d;
                seek->setRange(0, int(d));
            });
            connect(pl[i], QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this, i](QMediaPlayer::Error) {
                wave[i]->message = QStringLiteral("playback unavailable (%1)").arg(pl[i]->errorString());
                wave[i]->update();
            });
        }
        connect(play_btn, &QPushButton::clicked, this, [this] {
            if (pl[0]->state() == QMediaPlayer::PlayingState) { pl[0]->pause(); pl[1]->pause(); }
            else { pl[0]->play(); pl[1]->play(); }
        });
        connect(pl[0], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            if (!user_seeking) seek->setValue(int(pos));
            if (qAbs(pl[1]->position() - pos) > 120) pl[1]->setPosition(pos);
            pos_lbl[0]->setText(QString("%1 / %2").arg(AudioWavePage::fmt(pos))
                                    .arg(AudioWavePage::fmt(std::max<qint64>(0, duration))));
            double f = duration > 0 ? double(pos) / double(duration) : 0.0;
            wave[0]->set_pos(f);
            wave[1]->set_pos(f);
        });
        connect(pl[1], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            pos_lbl[1]->setText(AudioWavePage::fmt(pos));
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderMoved, this, [this](int v) {
            pos_lbl[0]->setText(QString("%1 / %2").arg(AudioWavePage::fmt(v))
                                    .arg(AudioWavePage::fmt(std::max<qint64>(0, duration))));
        });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            pl[0]->setPosition(seek->value());
            pl[1]->setPosition(seek->value());
        });
    }

    void load(const QString& a, const QString& b) {
        stop_all();
        wave[0]->message = QStringLiteral("decoding\xe2\x80\xa6");
        wave[0]->update();
        wave[1]->message = QStringLiteral("decoding\xe2\x80\xa6");
        wave[1]->update();
        pl[0]->setMedia(QMediaContent(QUrl::fromLocalFile(a)));
        pl[1]->setMedia(QMediaContent(QUrl::fromLocalFile(b)));
        names[0]->setText(QFileInfo(a).fileName());
        names[1]->setText(QFileInfo(b).fileName());
        seek->setValue(0);
        alive = true;
        load_gen++;
        uint64_t gen = load_gen;
        QByteArray pa = a.toUtf8(), pb = b.toUtf8();
        if (dec_thread.joinable()) dec_thread.join();
        dec_thread = std::thread([this, pa, pb, gen] {
            QByteArray ra = decode_pcm_bytes(QString::fromUtf8(pa));
            QByteArray rb = decode_pcm_bytes(QString::fromUtf8(pb));
            std::vector<float> va = bytes_to_pcm(ra);
            std::vector<float> vb = bytes_to_pcm(rb);
            QMetaObject::invokeMethod(this, [this, va, vb, gen] {
                if (gen != load_gen) return;
                wave[0]->set_pcm(va);
                wave[1]->set_pcm(vb);
            }, Qt::QueuedConnection);
        });
    }

    void stop_all() {
        pl[0]->stop();
        pl[1]->stop();
    }

    ~AudioCompare() override {
        alive = false;
        if (dec_thread.joinable()) dec_thread.join();
    }

private:
    std::thread dec_thread;
    std::atomic<bool> alive{false};
    uint64_t load_gen = 0;
};

}  // namespace neon_gui
