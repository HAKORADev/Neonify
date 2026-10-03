// NEONIFY native gui — left inputs, right results, center viewer/compare,
// bottom settings (0-100, the app converts internally). dark + white.
#include "neon_common.h"
#include "neon_image.h"
#include "neon_audio.h"
#include "neon_video.h"
#include "neon_mesh.h"
#include "neon_options.h"
#include "neon_gui_widgets.h"

#include <QtWidgets>
#include <QMediaPlayer>
#include <QMediaContent>
#include <QUrl>
#include <QFileInfo>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QMimeData>
#include <atomic>

#ifdef NEONIFY_STATIC_QT
#include <QtPlugin>
#include "neon_plugins.h"
#endif

namespace neon_gui {

inline QString style_sheet() {
    return QString(
        "QPushButton{background:#1d1d1d;color:#e5e5e5;border:1px solid #2f2f2f;"
        "border-radius:5px;padding:5px 12px;font-size:12px;}"
        "QPushButton:hover{background:#262626;border-color:#4a4a4a;}"
        "QPushButton:disabled{color:#555555;border-color:#242424;}"
        "QPushButton#accent{background:#e5e5e5;border:none;color:#111111;font-weight:bold;}"
        "QPushButton#accent:hover{background:#ffffff;}"
        "QComboBox{background:#1d1d1d;color:#e5e5e5;border:1px solid #2f2f2f;border-radius:5px;padding:4px 8px;}"
        "QComboBox QAbstractItemView{background:#1d1d1d;color:#e5e5e5;selection-background-color:#3a3a3a;}"
        "QSlider::groove:horizontal{height:4px;background:#2f2f2f;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;border-radius:6px;background:#e5e5e5;}"
        "QSlider::sub-page:horizontal{background:#6a6a6a;border-radius:2px;}"
        "QCheckBox{color:#bbbbbb;font-size:12px;}"
        "QSpinBox{background:#1d1d1d;color:#e5e5e5;border:1px solid #2f2f2f;border-radius:5px;padding:3px;}"
        "QLabel{color:#bbbbbb;font-size:12px;background:transparent;}"
        "QListWidget{background:#121212;border:1px solid #242424;border-radius:6px;color:#cccccc;}"
        "QListWidget::item{padding:2px;border-bottom:1px solid #1c1c1c;}"
        "QScrollArea{border:none;background:transparent;}"
        "QScrollArea>QWidget>QWidget{background:transparent;}"
        "QScrollBar:vertical{background:#121212;width:10px;}"
        "QScrollBar::handle:vertical{background:#3a3a3a;border-radius:4px;min-height:24px;}"
        "QProgressBar{background:#121212;border:1px solid #242424;border-radius:5px;"
        "color:#e5e5e5;text-align:center;height:16px;}"
        "QProgressBar::chunk{background:#e5e5e5;border-radius:4px;}"
        "QMessageBox{background:#161616;}"
        "QDialog{background:#161616;}"
        "QDoubleSpinBox{background:#1d1d1d;color:#e5e5e5;border:1px solid #2f2f2f;border-radius:4px;padding:3px;}"
    );
}

inline QString panel_style() {
    return QString("QFrame#panel{background:#161616;border:1px solid #242424;border-radius:8px;}");
}

enum Kind { KImage, KVideo, KAudio, KMesh, KRelief, KUnknown };

inline Kind detect_kind(const QString& path) {
    QString e = QFileInfo(path).suffix().toLower();
    if (e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "webp" || e == "tif" || e == "tiff")
        return KImage;
    if (e == "mp4" || e == "avi" || e == "mkv" || e == "mov" || e == "webm" || e == "gif")
        return KVideo;
    if (e == "wav" || e == "mp3" || e == "flac" || e == "ogg" || e == "m4a" || e == "aac" || e == "wma")
        return KAudio;
    if (e == "obj" || e == "ply" || e == "stl")
        return KMesh;
    return KUnknown;
}

inline QString kind_name(Kind k) {
    switch (k) {
        case KImage: return QStringLiteral("image");
        case KVideo: return QStringLiteral("video");
        case KAudio: return QStringLiteral("audio");
        case KMesh: return QStringLiteral("3d mesh");
        case KRelief: return QStringLiteral("relief");
        default: return QStringLiteral("file");
    }
}

inline QString kind_color(Kind k) {
    switch (k) {
        case KImage: return QStringLiteral("#e5e5e5");
        case KVideo: return QStringLiteral("#aaaaaa");
        case KAudio: return QStringLiteral("#cccccc");
        case KMesh: case KRelief: return QStringLiteral("#dddddd");
        default: return QStringLiteral("#888888");
    }
}

// ---- input row: name + kind badge + Preview + Delete -----------------------
class InputRow : public QWidget {
public:
    QString path;
    Kind kind;
    QPushButton* prev_btn = nullptr;
    QPushButton* del_btn = nullptr;

    InputRow(const QString& p, Kind k, QWidget* parent = nullptr)
        : QWidget(parent), path(p), kind(k) {
        QHBoxLayout* l = new QHBoxLayout(this);
        l->setContentsMargins(6, 2, 4, 2);
        l->setSpacing(4);
        QLabel* name_lbl = new QLabel(QFileInfo(p).fileName(), this);
        name_lbl->setStyleSheet("color:#cccccc;font-size:12px;background:transparent;");
        name_lbl->setToolTip(p);
        QLabel* badge = new QLabel(kind_name(k), this);
        badge->setStyleSheet(QString("color:%1;font-size:10px;background:transparent;").arg(kind_color(k)));
        prev_btn = new QPushButton("Preview", this);
        del_btn = new QPushButton("Delete", this);
        for (QPushButton* b : {prev_btn, del_btn}) {
            b->setStyleSheet("QPushButton{background:#222222;color:#aaaaaa;border:1px solid #2f2f2f;"
                             "border-radius:4px;padding:2px 8px;font-size:11px;}"
                             "QPushButton:hover{color:#ffffff;border-color:#4a4a4a;}");
        }
        l->addWidget(name_lbl, 1);
        l->addWidget(badge);
        l->addWidget(prev_btn);
        l->addWidget(del_btn);
    }
};

// ---- result row: name + kind badge + Preview + Compare ---------------------
class ResultRow : public QWidget {
public:
    QString input_path;
    QString output_path;
    Kind kind;
    QPushButton* prev_btn = nullptr;
    QPushButton* cmp_btn = nullptr;

    ResultRow(const QString& in, const QString& out, Kind k, QWidget* parent = nullptr)
        : QWidget(parent), input_path(in), output_path(out), kind(k) {
        QHBoxLayout* l = new QHBoxLayout(this);
        l->setContentsMargins(6, 2, 4, 2);
        l->setSpacing(4);
        QLabel* name_lbl = new QLabel(QFileInfo(out).fileName(), this);
        name_lbl->setStyleSheet("color:#cccccc;font-size:12px;background:transparent;");
        name_lbl->setToolTip(out);
        QLabel* badge = new QLabel(kind_name(k), this);
        badge->setStyleSheet(QString("color:%1;font-size:10px;background:transparent;").arg(kind_color(k)));
        prev_btn = new QPushButton("Preview", this);
        cmp_btn = new QPushButton("Compare", this);
        for (QPushButton* b : {prev_btn, cmp_btn}) {
            b->setStyleSheet("QPushButton{background:#222222;color:#aaaaaa;border:1px solid #2f2f2f;"
                             "border-radius:4px;padding:2px 8px;font-size:11px;}"
                             "QPushButton:hover{color:#ffffff;border-color:#4a4a4a;}");
        }
        l->addWidget(name_lbl, 1);
        l->addWidget(badge);
        l->addWidget(prev_btn);
        l->addWidget(cmp_btn);
    }
};

// ---- processing worker: sequential engine runs on a thread -----------------
class ProcessingWorker : public QThread {
    Q_OBJECT

public:
    QStringList inputs;
    neon::Options opts;
    std::atomic<bool> stop_flag{false};

    ProcessingWorker(QObject* parent = nullptr) : QThread(parent) {}

    static QString make_output(const QString& inp, Kind k, const neon::Options& o) {
        std::string inps = inp.toStdString();
        std::string tag;
        std::string ext = "";
        std::string::size_type dot = inps.find_last_of('.');
        if (dot != std::string::npos) ext = inps.substr(dot);
        if (k == KImage) { tag = o.palette; }
        else if (k == KVideo) { tag = o.palette; ext = ".mp4"; }
        else if (k == KAudio) { tag = o.profile.empty() ? std::string("slash") : o.profile; ext = ".wav"; }
        else if (k == KMesh || k == KRelief) { tag = o.palette + "3d"; ext = o.turntable > 0 ? ".mp4" : ".png"; }
        return QString::fromStdString(neon::default_output_for(inps, tag, ext, o.next_to_input));
    }

    void run() override {
        for (const QString& q : inputs) {
            if (stop_flag) break;
            Kind k = detect_kind(q);
            if (k == KUnknown) continue;
            std::string inp = q.toStdString();
            std::string out = make_output(q, k, opts).toStdString();
            neon::StageTracker tr(true, false);
            tr.quiet = true;
            tr.hook = [this](const std::string& label, double frac) {
                emit progress_signal(QString::fromStdString(label), frac);
            };
            try {
                if (k == KImage) {
                    neon::neonize_image_file(inp, out, opts.palette, opts.glow,
                                             opts.threshold, opts.env, &tr, nullptr, opts.keep_inside);
                } else if (k == KVideo) {
                    neon::process_video_file(inp, out, opts.palette, opts.glow, opts.threshold,
                                             opts.env, opts.profile, opts.spatial,
                                             opts.neon_audio, opts.advanced, &tr, opts.keep_inside);
                } else if (k == KAudio) {
                    std::string prof = opts.profile.empty() ? "slash" : opts.profile;
                    neon::neonize_audio_file(inp, out, prof, opts.glow, opts.advanced, &tr);
                } else if (k == KMesh) {
                    neon::neonize_mesh_file(inp, out, opts.palette, opts.glow,
                                            opts.turntable, opts.azimuth, opts.elevation, &tr);
                } else if (k == KRelief) {
                    neon::neonize_relief_file(inp, out, opts.palette, opts.glow, opts.depth,
                                              opts.turntable,
                                              opts.azimuth, opts.elevation, &tr, opts.export_mesh);
                }
                tr.finish();
                emit file_done(q, QString::fromStdString(out), int(k));
            } catch (const std::exception& e) {
                QString msg = QString::fromLocal8Bit(e.what());
                if (msg.trimmed().isEmpty())
                    msg = QStringLiteral("processing failed for %1").arg(QFileInfo(q).fileName());
                emit failed(msg);
            } catch (...) {
                emit failed(QStringLiteral("processing failed for %1").arg(QFileInfo(q).fileName()));
            }
        }
        emit all_done();
    }

signals:
    void progress_signal(const QString& label, double frac);
    void file_done(const QString& in, const QString& out, int kind);
    void failed(const QString& msg);
    void all_done();
};

// ---- 3d preview worker: 10s 360 turntable at preview quality ----------------
class Preview3DWorker : public QThread {
    Q_OBJECT

public:
    QString input;

    Preview3DWorker(const QString& inp, QObject* parent = nullptr)
        : QThread(parent), input(inp) {}

    void run() override {
        try {
            std::string inp = input.toStdString();
            neon::Mesh mesh;
            Kind k = detect_kind(input);
            if (k == KMesh) {
                mesh = neon::load_mesh(inp);
            } else {
                cv::Mat img = neon::imread_robust(inp, cv::IMREAD_COLOR);
                if (img.empty()) throw std::runtime_error("cannot read image: " + inp);
                neon::image_relief_mesh(img, 110, 0.85f, mesh);
            }
            neon::normalize_mesh(mesh);
            std::string base = QFileInfo(input).fileName().toStdString();
            std::string::size_type dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            std::string tmpdir = neon::results_dir() + "/previews";
            neon::make_dir(tmpdir);
            std::string out = tmpdir + "/" + base + "_360_" +
                              std::to_string(QDateTime::currentMSecsSinceEpoch()) + ".mp4";
            if (!neon::render_turntable_pipe(out, mesh, 240, 30.f, 20.f, "electric", 1.f, 24, 640, 480, [](int) {}))
                throw std::runtime_error("preview encode failed");
            emit preview_done(QString::fromStdString(out));
        } catch (const std::exception& e) {
            QString msg = QString::fromLocal8Bit(e.what());
            emit preview_failed(msg.isEmpty() ? QStringLiteral("3d preview failed") : msg);
        } catch (...) {
            emit preview_failed(QStringLiteral("3d preview failed"));
        }
    }

signals:
    void preview_done(const QString& mp4);
    void preview_failed(const QString& msg);
};

// ---- advanced audio dialog (schema-driven, profile aware) -------------------
class AdvancedAudioDialog : public QDialog {
public:
    std::map<std::string, float> overrides;
    QComboBox* profile_combo = nullptr;
    QLabel* hint = nullptr;
    QWidget* rows = nullptr;
    QVBoxLayout* rows_lay = nullptr;
    std::vector<QDoubleSpinBox*> spins;

    AdvancedAudioDialog(const QString& prof, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Advanced audio settings");
        setMinimumWidth(480);
        QVBoxLayout* lay = new QVBoxLayout(this);
        QHBoxLayout* prow = new QHBoxLayout;
        prow->addWidget(new QLabel("Profile", this));
        profile_combo = new QComboBox(this);
        int pc = int(sizeof(neon::AUDIO_PROFILES) / sizeof(neon::AUDIO_PROFILES[0]));
        for (int i = 0; i < pc; i++)
            profile_combo->addItem(QString("%1 — %2")
                                       .arg(neon::AUDIO_PROFILES[i])
                                       .arg(QString::fromLatin1(neon::PROFILE_DESCRIPTIONS(neon::AUDIO_PROFILES[i]))),
                                   QString::fromLatin1(neon::AUDIO_PROFILES[i]));
        int idx = profile_combo->findData(prof);
        if (idx >= 0) profile_combo->setCurrentIndex(idx);
        prow->addWidget(profile_combo, 1);
        lay->addLayout(prow);
        hint = new QLabel(QString(), this);
        hint->setWordWrap(true);
        hint->setStyleSheet("color:#888888;font-size:11px;");
        lay->addWidget(hint);
        rows = new QWidget(this);
        rows_lay = new QVBoxLayout(rows);
        rows_lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(rows);
        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        lay->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, [this] {
            for (QDoubleSpinBox* sp : spins)
                overrides[sp->property("key").toString().toStdString()] = float(sp->value());
            accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(profile_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { rebuild(); });
        rebuild();
    }

    void rebuild() {
        for (QDoubleSpinBox* sp : spins) sp->deleteLater();
        spins.clear();
        QLayoutItem* item;
        while ((item = rows_lay->takeAt(0)) != nullptr) delete item;
        QString prof = profile_combo->currentData().toString();
        std::vector<int> matches;
        for (int i = 0; i < neon::ADV_SCHEMA_ROWS; i++)
            if (prof.toStdString() == neon::ADV_SCHEMA[i][0]) matches.push_back(i);
        if (matches.empty()) {
            hint->setText("advanced parameters are not available for this profile");
            return;
        }
        hint->setText("overrides the profile's tuned defaults — leave untouched to keep the stock sound");
        for (int i : matches) {
            QHBoxLayout* l = new QHBoxLayout;
            QLabel* lbl = new QLabel(QString::fromLatin1(neon::ADV_SCHEMA[i][2]), rows);
            QDoubleSpinBox* sp = new QDoubleSpinBox(rows);
            float lo = neon::adv_lo(i), hi = neon::adv_hi(i), dflt = neon::adv_default(i);
            sp->setRange(double(lo), double(hi));
            sp->setDecimals(3);
            sp->setSingleStep(std::max((hi - lo) / 100.0, 0.001));
            sp->setValue(dflt);
            sp->setProperty("key", QString::fromLatin1(neon::ADV_SCHEMA[i][1]));
            spins.push_back(sp);
            l->addWidget(lbl, 1);
            l->addWidget(sp);
            rows_lay->addLayout(l);
        }
    }
};

// ---- main window ------------------------------------------------------------
class NeonifyGUI : public QMainWindow {
public:
    QListWidget* in_list = nullptr;
    QListWidget* out_list = nullptr;
    QStackedWidget* center = nullptr;
    SingleViewer* viewer = nullptr;
    SideBySideView* sbs = nullptr;
    VideoCompare* vcmp = nullptr;
    AudioCompare* acmp = nullptr;

    QComboBox* palette_combo = nullptr;
    QSlider* glow = nullptr; QLabel* glow_v = nullptr;
    QSlider* thr = nullptr; QLabel* thr_v = nullptr;
    QSlider* env = nullptr; QLabel* env_v = nullptr;
    QWidget* vis_section = nullptr;
    QWidget* aud_section = nullptr;
    QWidget* td_section = nullptr;
    QComboBox* profile_combo = nullptr;
    QCheckBox* neon_audio = nullptr;
    QCheckBox* spatial = nullptr;
    QCheckBox* next_to_input = nullptr;
    QCheckBox* keep_inside = nullptr;
    QPushButton* adv_btn = nullptr;
    std::map<std::string, float> advanced;
    QComboBox* turn_combo = nullptr;
    QSlider* depth = nullptr; QLabel* depth_v = nullptr;
    QSlider* azim = nullptr; QLabel* azim_v = nullptr;
    QSlider* elev = nullptr; QLabel* elev_v = nullptr;
    QCheckBox* export_mesh = nullptr;

    QPushButton* run_btn = nullptr;
    QProgressBar* bar = nullptr;
    QLabel* status = nullptr;
    ProcessingWorker* worker = nullptr;
    Preview3DWorker* prev3d = nullptr;
    int proc_done = 0;

    NeonifyGUI() {
        setWindowTitle("Neonify");
        setWindowIcon(QIcon(":/logo.png"));
        setAcceptDrops(true);
        resize(1280, 800);
        setMinimumSize(1040, 680);

        setStyleSheet(style_sheet() +
                      "QMainWindow{background:#0e0e0e;} QWidget#root{background:#0e0e0e;}");
        QWidget* central = new QWidget(this);
        central->setObjectName("root");
        setCentralWidget(central);
        QVBoxLayout* root = new QVBoxLayout(central);
        root->setContentsMargins(10, 8, 10, 8);
        root->setSpacing(8);

        root->addWidget(build_header());

        QHBoxLayout* body = new QHBoxLayout;
        body->setSpacing(8);
        body->addWidget(build_inputs_panel(), 0);
        body->addWidget(build_center(), 1);
        body->addWidget(build_results_panel(), 0);
        root->addLayout(body, 1);

        root->addWidget(build_settings());

        QHBoxLayout* foot = new QHBoxLayout;
        run_btn = new QPushButton("NEONIFY");
        run_btn->setObjectName("accent");
        run_btn->setEnabled(false);
        run_btn->setMinimumHeight(34);
        bar = new QProgressBar;
        bar->setValue(0);
        status = new QLabel(QString());
        status->setStyleSheet("color:#888888;font-size:11px;");
        foot->addWidget(run_btn);
        foot->addWidget(bar, 1);
        foot->addWidget(status, 2);
        root->addLayout(foot);

        connect(run_btn, &QPushButton::clicked, this, &NeonifyGUI::start_processing);
        refresh_sections();
        refresh_run_enabled();
    }

    QWidget* build_header() {
        QWidget* h = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(h);
        l->setContentsMargins(0, 0, 0, 0);
        QLabel* logo = new QLabel(h);
        QPixmap pm(":/logo.png");
        if (!pm.isNull())
            logo->setPixmap(pm.scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        QLabel* title = new QLabel("NEONIFY", h);
        title->setStyleSheet("color:#ffffff;font-size:17px;font-weight:bold;letter-spacing:3px;");
        l->addWidget(logo);
        l->addWidget(title);
        l->addStretch();
        return h;
    }

    QWidget* build_inputs_panel() {
        QFrame* p = new QFrame(this);
        p->setObjectName("panel");
        p->setStyleSheet(panel_style());
        p->setFixedWidth(330);
        QVBoxLayout* l = new QVBoxLayout(p);
        QLabel* t = new QLabel("INPUT FILES", p);
        t->setStyleSheet("color:#ffffff;font-weight:bold;font-size:12px;");
        l->addWidget(t);
        in_list = new QListWidget(p);
        l->addWidget(in_list, 1);
        QHBoxLayout* btns = new QHBoxLayout;
        QPushButton* add = new QPushButton("+ Add files", p);
        QPushButton* clr = new QPushButton("Clear", p);
        btns->addWidget(add);
        btns->addWidget(clr);
        l->addLayout(btns);
        connect(add, &QPushButton::clicked, this, [this] { add_files_dialog(); });
        connect(clr, &QPushButton::clicked, this, [this] {
            stop_center_media();
            in_list->clear();
            viewer->reset();
            center->setCurrentWidget(viewer);
            refresh_sections();
            refresh_run_enabled();
            status->setText(QString());
        });
        return p;
    }

    QWidget* build_results_panel() {
        QFrame* p = new QFrame(this);
        p->setObjectName("panel");
        p->setStyleSheet(panel_style());
        p->setFixedWidth(350);
        QVBoxLayout* l = new QVBoxLayout(p);
        QLabel* t = new QLabel("RESULTS", p);
        t->setStyleSheet("color:#ffffff;font-weight:bold;font-size:12px;");
        l->addWidget(t);
        out_list = new QListWidget(p);
        l->addWidget(out_list, 1);
        QHBoxLayout* btns = new QHBoxLayout;
        QPushButton* clr = new QPushButton("Clear previews", p);
        btns->addWidget(clr);
        btns->addStretch(1);
        l->addLayout(btns);
        connect(clr, &QPushButton::clicked, this, [this] {
            stop_center_media();
            out_list->clear();
            viewer->reset();
            center->setCurrentWidget(viewer);
            status->setText(QString());
        });
        return p;
    }

    QWidget* build_center() {
        center = new QStackedWidget(this);
        viewer = new SingleViewer;
        sbs = new SideBySideView;
        vcmp = new VideoCompare;
        acmp = new AudioCompare;
        center->addWidget(viewer);
        center->addWidget(sbs);
        center->addWidget(vcmp);
        center->addWidget(acmp);
        center->setCurrentIndex(0);
        return center;
    }

    QSlider* mk_slider(int lo, int hi, int val) {
        QSlider* s = new QSlider(Qt::Horizontal, this);
        s->setRange(lo, hi);
        s->setValue(val);
        return s;
    }

    QWidget* build_settings() {
        QScrollArea* sc = new QScrollArea(this);
        sc->setWidgetResizable(true);
        sc->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sc->setMaximumHeight(215);
        QWidget* panel = new QWidget;
        QVBoxLayout* root = new QVBoxLayout(panel);
        root->setContentsMargins(4, 2, 4, 2);
        root->setSpacing(6);

        vis_section = new QWidget(panel);
        QVBoxLayout* vl = new QVBoxLayout(vis_section);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(4);
        QHBoxLayout* r1 = new QHBoxLayout;
        r1->addWidget(new QLabel("palette", vis_section));
        palette_combo = new QComboBox(vis_section);
        const char* palettes[] = {"electric", "crimson", "ice", "toxic", "violet", "golden", "ghost"};
        for (const char* p : palettes) palette_combo->addItem(QString::fromLatin1(p));
        r1->addWidget(palette_combo, 1);
        r1->addWidget(new QLabel("glow", vis_section));
        glow = mk_slider(10, 100, 50);
        glow_v = new QLabel("50", vis_section);
        r1->addWidget(glow, 2);
        r1->addWidget(glow_v);
        vl->addLayout(r1);
        QHBoxLayout* r2 = new QHBoxLayout;
        r2->addWidget(new QLabel("edge threshold", vis_section));
        thr = mk_slider(2, 60, 22);
        thr_v = new QLabel("22", vis_section);
        r2->addWidget(thr, 2);
        r2->addWidget(thr_v);
        r2->addWidget(new QLabel("ambient detail", vis_section));
        env = mk_slider(10, 100, 50);
        env_v = new QLabel("50", vis_section);
        r2->addWidget(env, 2);
        r2->addWidget(env_v);
        vl->addLayout(r2);
        QHBoxLayout* r3 = new QHBoxLayout;
        keep_inside = new QCheckBox("keep the inside (original look inside the edges)", vis_section);
        r3->addWidget(keep_inside);
        r3->addStretch(1);
        vl->addLayout(r3);
        connect(glow, &QSlider::valueChanged, this, [this] {
            glow_v->setText(QString::number(glow->value()));
        });
        connect(thr, &QSlider::valueChanged, this, [this] {
            thr_v->setText(QString::number(thr->value()));
        });
        connect(env, &QSlider::valueChanged, this, [this] {
            env_v->setText(QString::number(env->value()));
        });
        root->addWidget(vis_section);

        aud_section = new QWidget(panel);
        QHBoxLayout* al = new QHBoxLayout(aud_section);
        al->setContentsMargins(0, 0, 0, 0);
        al->setSpacing(8);
        al->addWidget(new QLabel("audio profile", aud_section));
        profile_combo = new QComboBox(aud_section);
        int pc = int(sizeof(neon::AUDIO_PROFILES) / sizeof(neon::AUDIO_PROFILES[0]));
        for (int i = 0; i < pc; i++)
            profile_combo->addItem(QString("%1 — %2").arg(neon::AUDIO_PROFILES[i])
                                       .arg(QString::fromLatin1(neon::PROFILE_DESCRIPTIONS(neon::AUDIO_PROFILES[i]))),
                                   QString::fromLatin1(neon::AUDIO_PROFILES[i]));
        profile_combo->setCurrentIndex(pc - 1);
        al->addWidget(profile_combo, 2);
        neon_audio = new QCheckBox("neonify audio with video", aud_section);
        spatial = new QCheckBox("spatial glow", aud_section);
        spatial->setChecked(true);
        adv_btn = new QPushButton("Advanced…", aud_section);
        al->addWidget(neon_audio);
        al->addWidget(spatial);
        al->addWidget(adv_btn);
        root->addWidget(aud_section);
        connect(adv_btn, &QPushButton::clicked, this, [this] { open_advanced_audio(); });

        td_section = new QWidget(panel);
        QHBoxLayout* tl = new QHBoxLayout(td_section);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(8);
        tl->addWidget(new QLabel("3d orbit", td_section));
        turn_combo = new QComboBox(td_section);
        const struct { const char* name; int v; } opts3d[] = {
            {"single frame", 0}, {"24 frames", 24}, {"48 frames", 48},
            {"96 frames", 96}, {"full 360 (240)", 240}};
        for (auto& o : opts3d) turn_combo->addItem(QString::fromLatin1(o.name), o.v);
        turn_combo->setCurrentIndex(2);
        tl->addWidget(turn_combo, 1);
        tl->addWidget(new QLabel("relief depth", td_section));
        depth = mk_slider(5, 100, 43);
        depth_v = new QLabel("43", td_section);
        tl->addWidget(depth, 1);
        tl->addWidget(depth_v);
        tl->addWidget(new QLabel("view az / el", td_section));
        azim = mk_slider(0, 100, 8);
        azim_v = new QLabel("8", td_section);
        tl->addWidget(azim);
        tl->addWidget(azim_v);
        elev = mk_slider(0, 100, 22);
        elev_v = new QLabel("22", td_section);
        tl->addWidget(elev);
        tl->addWidget(elev_v);
        export_mesh = new QCheckBox("export .obj", td_section);
        tl->addWidget(export_mesh);
        root->addWidget(td_section);
        connect(depth, &QSlider::valueChanged, this, [this] { depth_v->setText(QString::number(depth->value())); });
        connect(azim, &QSlider::valueChanged, this, [this] { azim_v->setText(QString::number(azim->value())); });
        connect(elev, &QSlider::valueChanged, this, [this] { elev_v->setText(QString::number(elev->value())); });

        QHBoxLayout* out_row = new QHBoxLayout;
        out_row->setContentsMargins(0, 0, 0, 0);
        next_to_input = new QCheckBox("save next to input", panel);
        out_row->addWidget(next_to_input);
        out_row->addStretch(1);
        root->addLayout(out_row);

        sc->setWidget(panel);
        QFrame* wrap = new QFrame(this);
        wrap->setObjectName("panel");
        wrap->setStyleSheet(panel_style());
        QVBoxLayout* wl = new QVBoxLayout(wrap);
        wl->setContentsMargins(6, 4, 6, 4);
        wl->addWidget(sc);
        return wrap;
    }

    void add_files_dialog() {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Add media files", QString(),
            "Media (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff *.mp4 *.avi *.mkv *.mov "
            "*.webm *.gif *.wav *.mp3 *.flac *.ogg *.m4a *.aac *.wma *.obj *.ply *.stl);;All files (*.*)");
        add_paths(files);
    }

    void add_paths(const QStringList& paths) {
        QStringList skipped;
        int added = 0;
        for (const QString& p : paths) {
            Kind k = detect_kind(p);
            if (k == KUnknown) continue;
            if (contains_input(p)) {
                skipped << QFileInfo(p).fileName();
                continue;
            }
            QListWidgetItem* it = new QListWidgetItem(in_list);
            it->setSizeHint(QSize(0, 30));
            InputRow* row = new InputRow(p, k, in_list);
            in_list->setItemWidget(it, row);
            QString qp = p;
            connect(row->prev_btn, &QPushButton::clicked, this, [this, qp] { preview_single(qp); });
            connect(row->del_btn, &QPushButton::clicked, this, [this, qp] { remove_input(qp); });
            added++;
        }
        if (!skipped.isEmpty())
            QMessageBox::information(this, "Duplicates skipped",
                                     QStringLiteral("already in the queue, not added again:\n%1").arg(skipped.join("\n")));
        if (added) {
            refresh_sections();
            refresh_run_enabled();
            status->setText(QString("%1 file(s) added").arg(added));
        }
    }

    bool contains_input(const QString& p) const {
        QString want = QFileInfo(p).canonicalFilePath();
        for (int i = 0; i < in_list->count(); i++) {
            QWidget* w = in_list->itemWidget(in_list->item(i));
            InputRow* r = dynamic_cast<InputRow*>(w);
            if (r && QFileInfo(r->path).canonicalFilePath() == want)
                return true;
        }
        return false;
    }

    void remove_input(const QString& p) {
        for (int i = 0; i < in_list->count(); i++) {
            QWidget* w = in_list->itemWidget(in_list->item(i));
            InputRow* r = dynamic_cast<InputRow*>(w);
            if (r && r->path == p) {
                QListWidgetItem* it = in_list->takeItem(i);
                if (w) w->deleteLater();
                delete it;
                break;
            }
        }
        refresh_sections();
        refresh_run_enabled();
    }

    QStringList input_paths() const {
        QStringList out;
        for (int i = 0; i < in_list->count(); i++) {
            QWidget* w = in_list->itemWidget(in_list->item(i));
            InputRow* r = dynamic_cast<InputRow*>(w);
            if (r) out << r->path;
        }
        return out;
    }

    void refresh_sections() {
        bool has_vis = false, has_aud = false, has_3d = false;
        for (const QString& p : input_paths()) {
            Kind k = detect_kind(p);
            if (k == KImage || k == KVideo) has_vis = true;
            if (k == KAudio) has_aud = true;
            if (k == KMesh || k == KRelief) has_3d = true;
        }
        bool empty = in_list->count() == 0;
        vis_section->setVisible(has_vis || empty);
        aud_section->setVisible(has_aud || has_vis || empty);
        td_section->setVisible(has_3d || empty);
    }

    void refresh_run_enabled() {
        run_btn->setEnabled(in_list->count() > 0 && (!worker || !worker->isRunning()));
    }

    void preview_single(const QString& path) {
        Kind k = detect_kind(path);
        stop_center_media();
        if (k == KImage) {
            QPixmap pm(path);
            if (pm.isNull()) {
                QMessageBox::warning(this, "Neonify", "cannot open image:\n" + path);
                return;
            }
            viewer->show_image(pm);
            center->setCurrentWidget(viewer);
            status->setText("preview: " + QFileInfo(path).fileName());
        } else if (k == KVideo) {
            viewer->show_video(path);
            center->setCurrentWidget(viewer);
            status->setText("preview: " + QFileInfo(path).fileName());
        } else if (k == KAudio) {
            viewer->audio_page->load(path);
            viewer->show_audio();
            center->setCurrentWidget(viewer);
            status->setText("preview: " + QFileInfo(path).fileName());
        } else if (k == KMesh || k == KRelief) {
            viewer->show_text("rendering a 10s 360° orbit of " + QFileInfo(path).fileName() + "…");
            center->setCurrentWidget(viewer);
            if (prev3d && prev3d->isRunning()) return;
            status->setText("rendering 3d orbit preview…");
            prev3d = new Preview3DWorker(path, this);
            connect(prev3d, &Preview3DWorker::preview_done, this, [this](const QString& mp4) {
                status->setText("3d orbit ready — playing");
                viewer->show_video(mp4);
                center->setCurrentWidget(viewer);
                viewer->video->play();
            });
            connect(prev3d, &Preview3DWorker::preview_failed, this, [this](const QString& msg) {
                status->setText("3d preview failed");
                QMessageBox::warning(this, "Neonify", "3d preview failed:\n" + msg);
            });
            prev3d->start();
        }
    }

    void stop_center_media() {
        viewer->video->pause();
        viewer->audio_page->stop();
        vcmp->stop_all();
        acmp->stop_all();
    }

    void add_result(const QString& in, const QString& out, Kind k) {
        QListWidgetItem* it = new QListWidgetItem(out_list);
        it->setSizeHint(QSize(0, 30));
        ResultRow* row = new ResultRow(in, out, k, out_list);
        out_list->setItemWidget(it, row);
        connect(row->prev_btn, &QPushButton::clicked, this, [this, out] { preview_single(out); });
        connect(row->cmp_btn, &QPushButton::clicked, this, [this, in, out, k] {
            compare(in, out, k);
        });
    }

    void compare(const QString& in, const QString& out, Kind k) {
        stop_center_media();
        bool has_orig = QFile::exists(in);
        QString ext = QFileInfo(out).suffix().toLower();
        if (k == KAudio) {
            acmp->load(in, out);
            center->setCurrentWidget(acmp);
            return;
        }
        if (k == KVideo || ext == "mp4") {
            if (!has_orig) {
                preview_single(out);
                status->setText("original gone — showing result only");
                return;
            }
            vcmp->load(in, out);
            center->setCurrentWidget(vcmp);
            return;
        }
        QPixmap a, b;
        if (has_orig) a.load(in);
        b.load(out);
        if (b.isNull()) {
            QMessageBox::warning(this, "Neonify", "cannot open result:\n" + out);
            return;
        }
        sbs->set_pair(a, has_orig ? QFileInfo(in).fileName() : QStringLiteral("original removed"),
                      b, QFileInfo(out).fileName());
        center->setCurrentWidget(sbs);
    }

    void open_advanced_audio() {
        QString prof = profile_combo->currentData().toString();
        if (prof.isEmpty())
            prof = profile_combo->currentText().section(QStringLiteral(" —"), 0, 0);
        AdvancedAudioDialog dlg(prof, this);
        if (dlg.exec() == QDialog::Accepted) {
            advanced = dlg.overrides;
            status->setText(advanced.empty()
                                ? QStringLiteral("advanced audio: defaults")
                                : QStringLiteral("advanced audio: %1 override(s)").arg(int(advanced.size())));
        }
    }

    void start_processing() {
        QStringList files = input_paths();
        if (files.isEmpty()) return;
        if (worker && worker->isRunning()) return;

        neon::Options o;
        o.palette = palette_combo->currentText().toStdString();
        o.glow = glow->value() / 50.0f;
        o.threshold = 0.01f + thr->value() * 0.005f;
        o.env = env->value() / 50.0f;
        o.profile = profile_combo->currentData().toString().toStdString();
        if (o.profile.empty())
            o.profile = profile_combo->currentText().section(QStringLiteral(" —"), 0, 0).toStdString();
        o.spatial = spatial->isChecked();
        o.neon_audio = neon_audio->isChecked();
        o.keep_inside = keep_inside->isChecked();
        o.advanced.params = advanced;
        o.turntable = turn_combo->currentData().toInt();
        o.azimuth = azim->value() * 3.6f;
        o.elevation = elev->value() * 0.9f;
        o.depth = depth->value() / 100.0f * 2.0f;
        o.export_mesh = export_mesh->isChecked();
        o.next_to_input = next_to_input->isChecked();

        run_btn->setEnabled(false);
        bar->setValue(0);
        proc_done = 0;

        worker = new ProcessingWorker(this);
        worker->inputs = files;
        worker->opts = o;
        connect(worker, &ProcessingWorker::progress_signal, this, [this](const QString& label, double frac) {
            bar->setValue(int(frac * 100));
            status->setText(label);
        });
        connect(worker, &ProcessingWorker::file_done, this, [this](const QString& in, const QString& out, int kind) {
            proc_done++;
            add_result(in, out, Kind(kind));
            status->setText(QStringLiteral("[%1] %2").arg(proc_done).arg(QFileInfo(out).fileName()));
        });
        connect(worker, &ProcessingWorker::failed, this, [this](const QString& msg) {
            QMessageBox::warning(this, "Neonify", msg);
        });
        connect(worker, &ProcessingWorker::all_done, this, [this] {
            bar->setValue(100);
            status->setText(next_to_input->isChecked()
                                ? QStringLiteral("done — outputs next to the inputs")
                                : QStringLiteral("done — outputs in results/"));
            run_btn->setEnabled(true);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        status->setText("neonifying…");
        worker->start();
    }

    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }

    void dropEvent(QDropEvent* e) override {
        QStringList files;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile()) files << u.toLocalFile();
        add_paths(files);
    }
};

int run_gui(int argc, char** argv) {
    std::vector<std::string> clean;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i] ? argv[i] : "";
        if (a == "gui" || a == "cli") continue;
        clean.push_back(a);
    }
    std::vector<char*> cargv;
    for (auto& s : clean) cargv.push_back(const_cast<char*>(s.c_str()));
    int cargc = int(cargv.size());
    QApplication app(cargc, cargv.data());
    app.setApplicationName("Neonify");
    app.setWindowIcon(QIcon(":/logo.png"));

    NeonifyGUI w;
    w.show();
    return app.exec();
}

}  // namespace neon_gui

#include "neon_gui.moc"
#include "moc_neon_gui_widgets.cpp"
