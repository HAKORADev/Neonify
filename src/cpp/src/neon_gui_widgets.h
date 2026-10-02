// NEONIFY native gui — custom center widgets: synced side-by-side, video/audio compare
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
#include <cmath>
#include <QMediaPlayer>
#include <QMediaContent>
#include <QUrl>
#include <QFile>
#include <QDateTime>

namespace neon_gui {

inline QString style_sheet();

// ---- single media viewer (image zoom/pan, video player, text) --------------
class SingleViewer : public QStackedWidget {
public:
    QWidget* img_page = nullptr;
    QLabel* img_label = nullptr;
    QScrollArea* img_scroll = nullptr;
    QVideoWidget* video = nullptr;
    QLabel* text_label = nullptr;
    double zoom = 1.0;
    QPixmap pix;

    SingleViewer(QWidget* parent = nullptr) : QStackedWidget(parent) {
        img_page = new QWidget(this);
        QVBoxLayout* vl = new QVBoxLayout(img_page);
        vl->setContentsMargins(0, 0, 0, 0);
        img_scroll = new QScrollArea(img_page);
        img_scroll->setWidgetResizable(false);
        img_scroll->setAlignment(Qt::AlignCenter);
        img_scroll->setStyleSheet("QScrollArea{border:none;background:#111111;}");
        img_label = new QLabel;
        img_label->setAlignment(Qt::AlignCenter);
        img_label->setStyleSheet("background:#111111;");
        img_scroll->setWidget(img_label);
        vl->addWidget(img_scroll);
        addWidget(img_page);

        video = new QVideoWidget(this);
        video->setStyleSheet("background:#050505;");
        addWidget(video);

        text_label = new QLabel(this);
        text_label->setAlignment(Qt::AlignCenter);
        text_label->setWordWrap(true);
        text_label->setStyleSheet("background:#111111;color:#777777;font-size:14px;");
        addWidget(text_label);
        setCurrentWidget(text_label);
    }

    void show_image(const QPixmap& pm) {
        pix = pm;
        zoom = 1.0;
        apply_zoom();
        setCurrentWidget(img_page);
    }

    void apply_zoom() {
        if (pix.isNull()) return;
        QSize base = pix.size();
        QSize view = img_scroll->viewport()->size() - QSize(8, 8);
        double fit = std::min(1.0, std::min(double(view.width()) / std::max(1, base.width()),
                                            double(view.height()) / std::max(1, base.height())));
        double f = fit * zoom;
        img_label->setPixmap(pix.scaled(base.width() * int(std::max(0.05, f) * 100) / 100,
                                        base.height() * int(std::max(0.05, f) * 100) / 100,
                                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    void show_text(const QString& t) {
        text_label->setText(t);
        setCurrentWidget(text_label);
    }

    void wheelEvent(QWheelEvent* e) override {
        if (currentWidget() != img_page) return;
        if (e->modifiers() & Qt::ControlModifier) {
            zoom *= (e->angleDelta().y() > 0) ? 1.12 : 0.89;
            zoom = std::max(0.1, std::min(10.0, zoom));
            apply_zoom();
            e->accept();
            return;
        }
        QStackedWidget::wheelEvent(e);
    }
};

// ---- klarity-style side-by-side: shared zoom + pan, divider ----------------
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
        setStyleSheet("background:#0d0d0d;");
    }

    void set_pair(const QPixmap& b, const QString& bn, const QPixmap& a, const QString& an) {
        before = b; before_name = bn;
        after = a; after_name = an;
        zoom = 1.0; pan_x = pan_y = 0;
        update();
    }

    void set_zoom(double z) { zoom = std::max(0.1, std::min(10.0, z)); update(); }

    QRect scaled_rect(const QPixmap& pm, const QRect& area, int ox, int oy) const {
        if (pm.isNull()) return QRect();
        QSize s = pm.size().scaled(area.size(), Qt::KeepAspectRatio);
        int w = int(s.width() * zoom), h = int(s.height() * zoom);
        int x = area.x() + (area.width() - w) / 2 + pan_x + ox;
        int y = area.y() + (area.height() - h) / 2 + pan_y + oy;
        return QRect(x, y, w, h);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor(0x0d, 0x0d, 0x0d));
        int half = width() / 2;
        p.setPen(QPen(QColor(0xff, 0x22, 0x88), 2));
        p.drawLine(half, 0, half, height());
        if (before.isNull() && after.isNull()) {
            p.setPen(QColor(0x77, 0x77, 0x77));
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("pick a result and hit Compare"));
            return;
        }
        QRect L(0, 22, half, height() - 22), R(half, 22, width() - half, height() - 22);
        if (!before.isNull()) p.drawPixmap(scaled_rect(before, L, 0, 0), before);
        else { p.setPen(QColor(0x55, 0x55, 0x55)); p.drawText(L, Qt::AlignCenter, "original removed"); }
        if (!after.isNull()) p.drawPixmap(scaled_rect(after, R, 0, 0), after);
        else { p.setPen(QColor(0x55, 0x55, 0x55)); p.drawText(R, Qt::AlignCenter, "no result"); }
        p.setPen(QColor(0xff, 0x22, 0x88));
        QFont f = p.font(); f.setBold(true); f.setPointSize(10); p.setFont(f);
        p.drawText(QRect(8, 2, half - 16, 18), Qt::AlignLeft | Qt::AlignVCenter, before_name);
        p.drawText(QRect(half + 8, 2, width() - half - 16, 18), Qt::AlignLeft | Qt::AlignVCenter, after_name);
    }

    void wheelEvent(QWheelEvent* e) override {
        zoom *= (e->angleDelta().y() > 0) ? 1.12 : 0.89;
        zoom = std::max(0.1, std::min(10.0, zoom));
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

// ---- video compare: two players, sync length + speed + seek ----------------
class VideoCompare : public QWidget {
public:
    QMediaPlayer* pl[2] = {nullptr, nullptr};
    QVideoWidget* vw[2] = {nullptr, nullptr};
    QSlider* seek = nullptr;
    QPushButton* play_btn = nullptr;
    QSlider* speed = nullptr;
    QLabel* speed_lbl = nullptr;
    qint64 duration = 0;
    bool user_seeking = false;
    bool syncing = false;
    QLabel* names[2] = {nullptr, nullptr};

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
            names[i]->setStyleSheet("color:#ff22aa;background:#141414;font-size:11px;font-weight:bold;padding:2px;");
            vw[i] = new QVideoWidget(side);
            vw[i]->setStyleSheet("background:#050505;");
            sl->addWidget(names[i]);
            sl->addWidget(vw[i], 1);
            tops->addWidget(side);
        }
        root->addLayout(tops, 1);

        QHBoxLayout* ctrl = new QHBoxLayout;
        ctrl->setContentsMargins(6, 2, 6, 2);
        play_btn = new QPushButton("Play");
        play_btn->setStyleSheet(style_sheet());
        seek = new QSlider(Qt::Horizontal);
        seek->setRange(0, 0);
        seek->setStyleSheet(style_sheet());
        speed = new QSlider(Qt::Horizontal);
        speed->setRange(25, 200);
        speed->setValue(100);
        speed->setMaximumWidth(140);
        speed->setStyleSheet(style_sheet());
        speed_lbl = new QLabel("1.00x");
        speed_lbl->setStyleSheet("color:#aaaaaa;font-size:11px;");
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        ctrl->addWidget(new QLabel("speed", this));
        ctrl->addWidget(speed);
        ctrl->addWidget(speed_lbl);
        root->addLayout(ctrl);

        for (int i = 0; i < 2; i++) {
            pl[i] = new QMediaPlayer(this);
            pl[i]->setVideoOutput(vw[i]);
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
            syncing = true;
            if (qAbs(pl[1]->position() - pos) > 120) pl[1]->setPosition(pos);
            if (!user_seeking) seek->setValue(int(pos));
            syncing = false;
        });
        connect(pl[1], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            if (syncing) return;
            if (qAbs(pl[0]->position() - pos) > 2000) pl[1]->setPosition(pl[0]->position());
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

// ---- audio compare: two players + position + levels ------------------------
class AudioCompare : public QWidget {
public:
    QMediaPlayer* pl[2] = {nullptr, nullptr};
    QSlider* seek = nullptr;
    QPushButton* play_btn = nullptr;
    QLabel* names[2] = {nullptr, nullptr};
    QLabel* pos_lbl[2] = {nullptr, nullptr};
    QSlider* level[2] = {nullptr, nullptr};
    qint64 duration = 0;
    bool user_seeking = false;
    QTimer* meter_timer = nullptr;

    AudioCompare(QWidget* parent = nullptr) : QWidget(parent) {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 8, 16, 8);
        root->setSpacing(8);
        for (int i = 0; i < 2; i++) {
            QVBoxLayout* side = new QVBoxLayout;
            names[i] = new QLabel(i == 0 ? QStringLiteral("original") : QStringLiteral("result"), this);
            names[i]->setStyleSheet("color:#ff22aa;font-weight:bold;font-size:12px;");
            pos_lbl[i] = new QLabel("0:00", this);
            pos_lbl[i]->setStyleSheet("color:#888888;font-size:11px;");
            level[i] = new QSlider(Qt::Horizontal, this);
            level[i]->setRange(0, 100);
            level[i]->setValue(0);
            level[i]->setEnabled(false);
            level[i]->setStyleSheet(style_sheet());
            side->addWidget(names[i]);
            side->addWidget(level[i]);
            side->addWidget(pos_lbl[i]);
            root->addLayout(side, 1);
        }
        QHBoxLayout* ctrl = new QHBoxLayout;
        play_btn = new QPushButton("Play");
        play_btn->setStyleSheet(style_sheet());
        seek = new QSlider(Qt::Horizontal);
        seek->setRange(0, 0);
        seek->setStyleSheet(style_sheet());
        ctrl->addWidget(play_btn);
        ctrl->addWidget(seek, 1);
        root->addLayout(ctrl);
        root->addStretch(1);

        for (int i = 0; i < 2; i++) {
            pl[i] = new QMediaPlayer(this);
            connect(pl[i], &QMediaPlayer::durationChanged, this, [this](qint64 d) {
                duration = d;
                seek->setRange(0, int(d));
            });
        }
        connect(play_btn, &QPushButton::clicked, this, [this] {
            if (pl[0]->state() == QMediaPlayer::PlayingState) { pl[0]->pause(); pl[1]->pause(); }
            else { pl[0]->play(); pl[1]->play(); }
        });
        connect(pl[0], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            if (!user_seeking) seek->setValue(int(pos));
            if (qAbs(pl[1]->position() - pos) > 120) pl[1]->setPosition(pos);
            pos_lbl[0]->setText(fmt(pos));
        });
        connect(pl[1], &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            pos_lbl[1]->setText(fmt(pos));
        });
        connect(seek, &QSlider::sliderPressed, this, [this] { user_seeking = true; });
        connect(seek, &QSlider::sliderReleased, this, [this] {
            user_seeking = false;
            pl[0]->setPosition(seek->value());
            pl[1]->setPosition(seek->value());
        });
        meter_timer = new QTimer(this);
        connect(meter_timer, &QTimer::timeout, this, [this] {
            for (int i = 0; i < 2; i++) {
                int v = (pl[i]->state() == QMediaPlayer::PlayingState)
                            ? int(50 + 45 * std::sin(double(pl[i]->position()) / 90.0))
                            : 0;
                level[i]->setValue(v);
            }
        });
        meter_timer->start(60);
    }

    static QString fmt(qint64 ms) {
        int s = int(ms / 1000);
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }

    void load(const QString& a, const QString& b) {
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
