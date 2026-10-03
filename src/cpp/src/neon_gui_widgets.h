// NEONIFY native gui — center widgets: image canvas, waveforms, compares
#pragma once
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QScrollArea>
#include <QStyle>
#include <QVideoWidget>
#include <QFileInfo>
#include <QTimer>
#include <QProcess>
#include <cmath>
#include <QMediaPlayer>
#include <QMediaContent>
#include <QUrl>
#include <QFile>
#include <QDateTime>
#include <vector>

namespace neon_gui {

inline QString style_sheet();

// ---------------------------------------------------------------- colors
inline QColor C_BG()      { return QColor(0x0e, 0x0e, 0x0e); }
inline QColor C_PANEL()   { return QColor(0x16, 0x16, 0x16); }
inline QColor C_LINE()    { return QColor(0x2a, 0x2a, 0x2a); }
inline QColor C_TEXT()    { return QColor(0xe5, 0xe5, 0xe5); }
inline QColor C_DIM()     { return QColor(0x66, 0x66, 0x66); }

// ---------------------------------------------------------------- pcm decode
// ffmpeg -> mono f32 @ 8000hz; empty vector when ffmpeg or the file is missing
inline std::vector<float> decode_pcm(const QString& path) {
    std::vector<float> pcm;
    if (!QFile::exists(path)) return pcm;
    QProcess p;
    p.start(QStringLiteral("ffmpeg"),
            QStringList{"-v", "error", "-i", path, "-ac", "1", "-ar", "8000",
                        "-f", "f32le", "-"});
    if (!p.waitForStarted(4000)) return pcm;
    QByteArray all;
    while (p.waitForReadyRead(400)) {
        QByteArray chunk = p.readAllStandardOutput();
        all += chunk;
        if (all.size() > 64 * 1024 * 1024) break;
    }
    p.waitForFinished(8000);
    all += p.readAllStandardOutput();
    const float* f = reinterpret_cast<const float*>(all.constData());
    size_t n = all.size() / sizeof(float);
    pcm.assign(f, f + n);
    return pcm;
}

// min/max envelope, one pair per column
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

// ---------------------------------------------------------------- waveform
class WaveformWidget : public QWidget {
public:
    std::vector<float> lo, hi;
    double pos_frac = 0.0;
    bool playable = false;
    QString message;

    WaveformWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(56);
        setMouseTracking(true);
    }

    void set_pcm(const std::vector<float>& pcm) {
        pcm_envelope(pcm, std::max(64, width()), lo, hi);
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
        for (int x = 0; x < w && x < int(hi.size()); x++) {
            int y0 = mid - int(hi[size_t(x)] * float(mid - 4));
            int y1 = mid - int(lo[size_t(x)] * float(mid - 4));
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
// the picture always fits the view; wheel zooms, drag pans; resize re-fits
class ImageCanvas : public QWidget {
public:
    QPixmap pix;
    double zoom = 1.0;      // user zoom multiplier on top of fit
    QPointF pan;
    bool dragging = false;
    QPoint last;

    ImageCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
    }

    void set_pixmap(const QPixmap& pm) {
        pix = pm;
        zoom = 1.0;
        pan = QPointF(0, 0);
        update();
    }

    QRectF target_rect() const {
        if (pix.isNull()) return QRectF();
        QSize view = size();
        double fit = std::min(double(view.width()) / pix.width(),
                              double(view.height()) / pix.height());
        double f = fit * zoom;
        double w = pix.width() * f, h = pix.height() * f;
        return QRectF((view.width() - w) / 2.0 + pan.x(), (view.height() - h) / 2.0 + pan.y(), w, h);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        if (pix.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(target_rect(), pix, QRectF(pix.rect()));
    }
    void resizeEvent(QResizeEvent*) override { update(); }
    void wheelEvent(QWheelEvent* e) override {
        if (pix.isNull()) return;
        double f = (e->angleDelta().y() > 0) ? 1.12 : 0.89;
        zoom = std::max(0.2, std::min(8.0, zoom * f));
        update();
        e->accept();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) { dragging = true; last = e->pos(); setCursor(Qt::ClosedHandCursor); }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging) {
            pan += QPointF(e->pos() - last);
            last = e->pos();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging = false; setCursor(Qt::ArrowCursor); }
};

// ---------------------------------------------------------------- audio page
// waveform + play/pause + seek; playback errors are shown, never crash
class AudioWavePage : public QWidget {
public:
    WaveformWidget* wave = nullptr;
    QPushButton* play_btn = nullptr;
    QSlider* seek = nullptr;
    QLabel* time_lbl = nullptr;
    QMediaPlayer* player = nullptr;
    QString current;
    qint64 dur = 0;
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
        time_lbl = new QLabel("0:00", this);
        time_lbl->setStyleSheet("color:#666666;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(time_lbl);
        l->addLayout(ctrl);

        player = new QMediaPlayer(this);
        connect(play_btn, &QPushButton::clicked, this, [this] {
            if (current.isEmpty()) return;
            if (player->state() == QMediaPlayer::PlayingState) player->pause();
            else player->play();
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
        });
        connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
            wave->set_pos(dur > 0 ? double(ms) / double(dur) : 0.0);
            if (!user_seek) seek->setValue(int(ms));
            time_lbl->setText(fmt(ms));
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seek = true; });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seek = false;
            player->setPosition(seek->value());
        });
    }

    static QString fmt(qint64 ms) {
        int s = int(ms / 1000);
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }

    void load(const QString& path) {
        stop();
        current = path;
        wave->set_pcm(std::vector<float>());  // clear while decoding
        std::vector<float> pcm = decode_pcm(path);
        wave->set_pcm(pcm);
        play_btn->setEnabled(true);
        player->setMedia(QMediaContent(QUrl::fromLocalFile(path)));
    }

    void stop() {
        if (player) player->stop();
        play_btn->setText("Play");
    }
};

// ---------------------------------------------------------------- single viewer
class SingleViewer : public QStackedWidget {
public:
    ImageCanvas* canvas = nullptr;
    QVideoWidget* video = nullptr;
    AudioWavePage* audio_page = nullptr;
    QLabel* text_label = nullptr;

    SingleViewer(QWidget* parent = nullptr) : QStackedWidget(parent) {
        canvas = new ImageCanvas(this);
        addWidget(canvas);
        video = new QVideoWidget(this);
        video->setStyleSheet("background:#050505;");
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

    void show_video() {
        setCurrentWidget(video);
    }

    void show_audio() {
        setCurrentWidget(audio_page);
    }

    void show_text(const QString& t) {
        text_label->setText(t);
        setCurrentWidget(text_label);
    }
};

// ---------------------------------------------------------------- side by side
class SideBySideView : public QWidget {
public:
    QPixmap before, after;
    QString before_name, after_name;
    double zoom = 1.0;
    int pan_x = 0, pan_y = 0;
    bool dragging = false;
    QPoint last_pos;

    SideBySideView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(320, 240);
        setMouseTracking(true);
    }

    void set_pair(const QPixmap& b, const QString& bn, const QPixmap& a, const QString& an) {
        before = b; before_name = bn;
        after = a; after_name = an;
        zoom = 1.0; pan_x = pan_y = 0;
        update();
    }

    QRect scaled_rect(const QPixmap& pm, const QRect& area) const {
        if (pm.isNull()) return QRect();
        QSize s = pm.size().scaled(area.size(), Qt::KeepAspectRatio);
        int w = int(s.width() * zoom), h = int(s.height() * zoom);
        int x = area.x() + (area.width() - w) / 2 + pan_x;
        int y = area.y() + (area.height() - h) / 2 + pan_y;
        return QRect(x, y, w, h);
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

    void wheelEvent(QWheelEvent* e) override {
        zoom *= (e->angleDelta().y() > 0) ? 1.12 : 0.89;
        zoom = std::max(0.1, std::min(8.0, zoom));
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) { dragging = true; last_pos = e->pos(); }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging) {
            pan_x += e->pos().x() - last_pos.x();
            pan_y += e->pos().y() - last_pos.y();
            last_pos = e->pos();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging = false; }
};

// ---------------------------------------------------------------- video compare
class VideoCompare : public QWidget {
public:
    QMediaPlayer* pl[2] = {nullptr, nullptr};
    QVideoWidget* vw[2] = {nullptr, nullptr};
    QLabel* err[2] = {nullptr, nullptr};
    QSlider* seek = nullptr;
    QPushButton* play_btn = nullptr;
    QSlider* speed = nullptr;
    QLabel* speed_lbl = nullptr;
    QLabel* names[2] = {nullptr, nullptr};
    qint64 duration = 0;
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
            vw[i] = new QVideoWidget(side);
            vw[i]->setStyleSheet("background:#050505;");
            err[i] = new QLabel(QString(), side);
            err[i]->setAlignment(Qt::AlignCenter);
            err[i]->setStyleSheet("color:#666666;background:#0e0e0e;font-size:11px;padding:4px;");
            err[i]->hide();
            sl->addWidget(names[i]);
            sl->addWidget(vw[i], 1);
            sl->addWidget(err[i]);
            tops->addWidget(side);
        }
        root->addLayout(tops, 1);

        QHBoxLayout* ctrl = new QHBoxLayout;
        ctrl->setContentsMargins(6, 2, 6, 2);
        play_btn = new QPushButton("Play");
        seek = new QSlider(Qt::Horizontal);
        seek->setRange(0, 0);
        speed = new QSlider(Qt::Horizontal);
        speed->setRange(25, 200);
        speed->setValue(100);
        speed->setMaximumWidth(140);
        speed_lbl = new QLabel("1.00x");
        speed_lbl->setStyleSheet("color:#888888;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(new QLabel("speed"));
        ctrl->addWidget(speed);
        ctrl->addWidget(speed_lbl);
        root->addLayout(ctrl);

        for (int i = 0; i < 2; i++) {
            pl[i] = new QMediaPlayer(this);
            pl[i]->setVideoOutput(vw[i]);
            connect(pl[i], QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this, i](QMediaPlayer::Error) {
                QString m = pl[i]->errorString();
                err[i]->setText(m.isEmpty() ? QStringLiteral("cannot play this file") : m);
                err[i]->show();
            });
        }
        connect(play_btn, &QPushButton::clicked, this, [this] {
            if (pl[0]->state() == QMediaPlayer::PlayingState) { pl[0]->pause(); pl[1]->pause(); }
            else { pl[0]->play(); pl[1]->play(); }
        });
        connect(pl[0], &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State s) {
            play_btn->setText(s == QMediaPlayer::PlayingState ? "Pause" : "Play");
        });
        connect(pl[0], &QMediaPlayer::durationChanged, this, [this](qint64 d) {
            duration = d;
            seek->setRange(0, int(d));
        });
        connect(pl[0], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            if (qAbs(pl[1]->position() - pos) > 120) pl[1]->setPosition(pos);
            if (!user_seeking) seek->setValue(int(pos));
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            pl[0]->setPosition(seek->value());
            pl[1]->setPosition(seek->value());
        });
        connect(speed, &QSlider::valueChanged, this, [this](int v) {
            double r = v / 100.0;
            speed_lbl->setText(QString::number(r, 'f', 2) + "x");
            pl[0]->setPlaybackRate(r);
            pl[1]->setPlaybackRate(r);
        });
    }

    void load(const QString& a, const QString& b) {
        stop_all();
        for (int i = 0; i < 2; i++) err[i]->hide();
        pl[0]->setMedia(QMediaContent(QUrl::fromLocalFile(a)));
        pl[1]->setMedia(QMediaContent(QUrl::fromLocalFile(b)));
        names[0]->setText(QFileInfo(a).fileName());
        names[1]->setText(QFileInfo(b).fileName());
        seek->setValue(0);
    }

    void stop_all() {
        pl[0]->stop();
        pl[1]->stop();
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
            pos_lbl[0]->setText(AudioWavePage::fmt(pos));
            double f = duration > 0 ? double(pos) / double(duration) : 0.0;
            wave[0]->set_pos(f);
            wave[1]->set_pos(f);
        });
        connect(pl[1], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            pos_lbl[1]->setText(AudioWavePage::fmt(pos));
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            pl[0]->setPosition(seek->value());
            pl[1]->setPosition(seek->value());
        });
    }

    void load(const QString& a, const QString& b) {
        stop_all();
        std::vector<float> pa = decode_pcm(a);
        std::vector<float> pb = decode_pcm(b);
        wave[0]->set_pcm(pa);
        wave[1]->set_pcm(pb);
        pl[0]->setMedia(QMediaContent(QUrl::fromLocalFile(a)));
        pl[1]->setMedia(QMediaContent(QUrl::fromLocalFile(b)));
        names[0]->setText(QFileInfo(a).fileName());
        names[1]->setText(QFileInfo(b).fileName());
        seek->setValue(0);
    }

    void stop_all() {
        pl[0]->stop();
        pl[1]->stop();
    }
};

}  // namespace neon_gui
