// NEONIFY native — qt widgets gui (qt 5.15, same qt imder uses)
#include "neon_common.h"
#include "neon_image.h"
#include "neon_audio.h"
#include "neon_video.h"
#include "neon_mesh.h"
#include "neon_options.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QFrame>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QScrollArea>
#include <QtCore/QThread>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QImage>
#include <QtCore/QMutex>

#include <cstring>
#include <memory>

namespace neon_gui {

using namespace neon;

static const char* THEME_BG = "#0A0A0A";
static const char* THEME_SURFACE = "#1a1a1a";
static const char* THEME_TEXT = "#E5E5E5";
static const char* THEME_TEXT_DIM = "#A0A0A0";
static const char* THEME_BORDER = "#404040";

struct Job {
    std::string command;
    std::string input;
    std::string output;
    Options opts;
};

class Worker : public QThread {
    Q_OBJECT
public:
    std::vector<Job> jobs;
    void run() override {
        for (const auto& j : jobs) {
            if (cancelled) break;
            StageTracker tr(true, false);
            tr.quiet = true;
            tr.hook = [this](const std::string& label, double frac) {
                emit progress(int(frac * 100.0), QString::fromStdString(label));
            };
            try {
                if (j.command == "image") {
                    tr.set_stages({"neonize", "save"});
                    tr.begin_stage(0, "neonize");
                    tr.step(0.5);
                    cv::Mat img = cv::imread(j.input, cv::IMREAD_COLOR);
                    if (img.empty()) throw std::runtime_error("cannot read image: " + j.input);
                    cv::Mat out;
                    EdgeAux aux;
                    process_image_neon(img, j.opts.palette, j.opts.glow, j.opts.threshold,
                                       j.opts.env, out, aux);
                    tr.step(1.0);
                    tr.complete_stage(0);
                    tr.begin_stage(1, "save");
                    tr.step(0.9);
                    std::string finalp = unique_output_path(j.output);
                    cv::imwrite(finalp, out, {cv::IMWRITE_PNG_COMPRESSION, 6});
                    tr.complete_stage(1);
                    tr.finish();
                    emit file_done(QString::fromStdString(j.input), QString::fromStdString(finalp));
                } else if (j.command == "video") {
                    auto r = process_video_file(j.input, j.output, j.opts.palette, j.opts.glow,
                                                j.opts.threshold, j.opts.env, j.opts.profile,
                                                j.opts.spatial, j.opts.neon_audio, j.opts.advanced,
                                                &tr);
                    emit file_done(QString::fromStdString(j.input), QString::fromStdString(r.first));
                } else if (j.command == "audio") {
                    std::string path = neonize_audio_file(j.input, j.output,
                                                          j.opts.profile.empty() ? "slash" : j.opts.profile,
                                                          j.opts.glow, j.opts.advanced, &tr);
                    emit file_done(QString::fromStdString(j.input), QString::fromStdString(path));
                } else {
                    bool relief = true;
                    std::string e = lower_ext(j.input);
                    if (e == ".obj" || e == ".ply" || e == ".stl") relief = false;
                    std::string path;
                    if (relief)
                        path = neonize_relief_file(j.input, j.output, j.opts.palette, j.opts.glow,
                                                   j.opts.depth, j.opts.turntable > 0 ? j.opts.turntable : 48,
                                                   j.opts.azimuth, j.opts.elevation, &tr);
                    else
                        path = neonize_mesh_file(j.input, j.output, j.opts.palette, j.opts.glow,
                                                 j.opts.turntable, j.opts.azimuth, j.opts.elevation, &tr);
                    emit file_done(QString::fromStdString(j.input), QString::fromStdString(path));
                }
                emit progress(100, "Complete");
            } catch (const std::exception& e) {
                emit failed(QString::fromStdString(e.what()));
                return;
            }
        }
        emit all_done();
    }
    void cancel() { cancelled = true; }
    bool cancelled = false;

signals:
    void progress(int percent, QString step);
    void file_done(QString input, QString output);
    void all_done();
    void failed(QString message);
};

#include "neon_gui.moc"

class AdvancedAudioDialog : public QDialog {
public:
    AdvancedAudioDialog(const QString& profile, QWidget* parent) : QDialog(parent) {
        setWindowTitle("Advanced audio settings");
        setMinimumWidth(480);
        setStyleSheet(QString("QDialog{background-color:#121212;} QLabel{color:%1;}").arg(THEME_TEXT));
        combo = new QComboBox();
        for (const char* p : AUDIO_PROFILES) {
            QString s = QString("%1 — %2").arg(p, PROFILE_DESCRIPTIONS(p));
            combo->addItem(s, QString(p));
        }
        int idx = combo->findData(profile);
        if (idx >= 0) combo->setCurrentIndex(idx);
        auto lay = new QVBoxLayout(this);
        auto prow = new QHBoxLayout();
        prow->addWidget(new QLabel("Profile"));
        prow->addWidget(combo, 1);
        lay->addLayout(prow);
        hint = new QLabel("Overrides the profile's tuned defaults — leave untouched to keep the stock sound.");
        hint->setWordWrap(true);
        hint->setStyleSheet("color:#A0A0A0;font-size:11px;");
        lay->addWidget(hint);
        rows_widget = new QWidget();
        rows = new QVBoxLayout(rows_widget);
        rows->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(rows_widget);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        lay->addWidget(buttons);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { rebuild(); });
        rebuild();
    }

    Advanced result_advanced() const {
        Advanced adv;
        for (auto it = spins.constBegin(); it != spins.constEnd(); ++it)
            adv.params[it.key().toStdString()] = float(it.value()->value());
        return adv;
    }

    QString result_profile() const { return combo->currentData().toString(); }

private:
    void rebuild() {
        spins.clear();
        if (layout()) {
            while (QLayoutItem* it = rows->takeAt(0)) {
                if (auto* w = it->widget()) w->deleteLater();
                delete it;
            }
        }
        QString profile = combo->currentData().toString();
        int count = int(sizeof(ADV_SCHEMA) / sizeof(ADV_SCHEMA[0]));
        bool found = false;
        for (int i = 0; i < count; i++) {
            if (profile != ADV_SCHEMA[i][0]) continue;
            found = true;
            auto* row = new QHBoxLayout();
            auto* lab = new QLabel(ADV_SCHEMA[i][2]);
            auto* spin = new QDoubleSpinBox();
            float lo = 0, hi = 1, dflt = 0;
            if (schema_bounds(profile, ADV_SCHEMA[i][1], lo, hi, dflt)) {
                spin->setRange(lo, hi);
                spin->setSingleStep(std::max((hi - lo) / 100.0, 0.01));
                spin->setValue(dflt);
            }
            row->addWidget(lab, 1);
            row->addWidget(spin);
            rows->addLayout(row);
            spins[QString(ADV_SCHEMA[i][1])] = spin;
        }
        hint->setVisible(found);
    }
    struct Bound { const char* key; float lo, hi, dflt; };
    static const std::map<std::string, std::vector<Bound>>& schema_table() {
        static const std::map<std::string, std::vector<Bound>> t = {
            {"fire", {{"drive", 0, 1, 0.52f}, {"flicker_rate", 1, 12, 5.5f}, {"flicker_depth", 0, 0.6f, 0.25f},
                       {"crackle", 0, 1.5f, 0.5f}, {"rumble", 0, 1.5f, 0.6f}, {"rumble_hz", 40, 160, 90}}},
            {"ice", {{"shimmer_mix", 0, 1, 0.4f}, {"breath_rate", 0.1f, 2, 0.5f}, {"breath_depth", 0, 10, 3.5f},
                      {"time", 0.05f, 1, 0.19f}, {"fb", 0, 0.9f, 0.3f}, {"damp", 1000, 12000, 7000}, {"mix", 0, 1, 0.28f}}},
            {"robotic", {{"ring_hz", 40, 220, 88}, {"ring_mix", 0, 1, 0.6f}, {"comb", 0, 1, 0.45f}, {"bits", 6, 16, 10}}},
            {"ghost", {{"fog_mix", 0, 1, 0.5f}, {"whisper_rate", 0.1f, 2, 0.37f}, {"whisper_depth", 0, 12, 5},
                        {"time", 0.05f, 1.5f, 0.42f}, {"fb", 0, 0.9f, 0.42f}, {"damp", 500, 8000, 2600}, {"mix", 0, 1, 0.3f}}},
            {"void", {{"pitch_mix", 0, 1, 0.45f}, {"mix", 0, 1, 0.5f}, {"tone", 500, 6000, 1500},
                       {"time", 0.05f, 1.5f, 0.55f}, {"fb", 0, 0.9f, 0.5f}, {"damp", 500, 8000, 1800}}},
            {"echo", {{"time", 0.05f, 1.5f, 0.31f}, {"fb", 0, 0.9f, 0.45f}, {"damp", 500, 12000, 4200},
                       {"mix", 0, 1, 0.35f}, {"lp", 1000, 16000, 9000}}},
            {"slash", {{"gain", 0, 2, 0.55f}}},
        };
        return t;
    }
    static bool schema_bounds(const QString& profile, const QString& key, float& lo, float& hi, float& dflt) {
        const auto& t = schema_table();
        auto it = t.find(profile.toStdString());
        if (it == t.end()) return false;
        for (const auto& b : it->second) {
            if (key == b.key) {
                lo = b.lo;
                hi = b.hi;
                dflt = b.dflt;
                return true;
            }
        }
        return false;
    }
    void spins_clear_placeholder() {}
    QComboBox* combo;
    QLabel* hint;
    QWidget* rows_widget;
    QVBoxLayout* rows;
    QMap<QString, QDoubleSpinBox*> spins;
};

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("NEONIFY — Procedural Neon Art Tool (native)");
        setStyleSheet(QString("QMainWindow{background-color:%1;}").arg(THEME_BG));
        resize(1180, 720);
        auto* central = new QWidget();
        setCentralWidget(central);
        auto* root = new QVBoxLayout(central);

        auto* body = new QHBoxLayout();
        auto* left = new QVBoxLayout();
        auto* right = new QVBoxLayout();

        auto* files_panel = new QFrame();
        files_panel->setStyleSheet(
            QString("QFrame{background-color:#121212;border:2px solid #E5E5E5;border-radius:8px;}"
                    "QLabel{border:none;} QListWidget{background-color:%1;color:%2;"
                    "border:1px solid %3;border-radius:4px;}").arg(THEME_SURFACE, THEME_TEXT, THEME_BORDER));
        auto* files_lay = new QVBoxLayout(files_panel);
        files_lay->addWidget(new QLabel("FILES — drop or add"));
        list = new QListWidget();
        list->setAcceptDrops(true);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->viewport()->setAcceptDrops(true);
        list->viewport()->installEventFilter(this);
        files_lay->addWidget(list, 1);
        auto* add_row = new QHBoxLayout();
        auto* add_btn = new QPushButton("Add files");
        auto* clear_btn = new QPushButton("Clear");
        add_btn->setStyleSheet(btn_style());
        clear_btn->setStyleSheet(btn_style());
        connect(add_btn, &QPushButton::clicked, this, [this] { add_files(); });
        connect(clear_btn, &QPushButton::clicked, this, [this] { list->clear(); });
        add_row->addWidget(add_btn);
        add_row->addWidget(clear_btn);
        files_lay->addLayout(add_row);
        left->addWidget(files_panel, 3);

        auto* settings_panel = new QFrame();
        settings_panel->setStyleSheet(
            QString("QFrame{background-color:#121212;border:2px solid #E5E5E5;border-radius:8px;}"
                    "QLabel{border:none;color:%1;} QCheckBox{color:%1;}").arg(THEME_TEXT));
        auto* settings = new QVBoxLayout(settings_panel);
        settings->addWidget(new QLabel("SETTINGS"));

        auto add_combo = [&](const QString& label, QComboBox** box) {
            auto* row = new QHBoxLayout();
            row->addWidget(new QLabel(label));
            *box = new QComboBox();
            (*box)->setStyleSheet(QString("QComboBox{background-color:%1;color:%2;border:1px solid %3;border-radius:4px;padding:4px;}").arg(THEME_SURFACE, THEME_TEXT, THEME_BORDER));
            row->addWidget(*box, 1);
            settings->addLayout(row);
        };
        add_combo("Mode", &mode_combo);
        mode_combo->addItems({"Auto-detect", "Neon (images/videos)", "Audio", "Mesh / Relief"});
        add_combo("Palette", &palette_combo);
        for (const char* p : PALETTE_NAMES) palette_combo->addItem(QString(p).left(1).toUpper() + QString(p).mid(1), QString(p));
        add_combo("Audio profile", &profile_combo);
        for (const char* p : AUDIO_PROFILES) {
            profile_combo->addItem(QString("%1 — %2").arg(p, PROFILE_DESCRIPTIONS(p)), QString(p));
        }
        auto add_slider = [&](const QString& label, QSlider** slider, QLabel** value, int lo, int hi, int val) {
            auto* head = new QHBoxLayout();
            head->addWidget(new QLabel(label));
            head->addStretch();
            *value = new QLabel(QString::number(val / 100.0, 'f', 2));
            head->addWidget(*value);
            auto* box = new QVBoxLayout();
            *slider = new QSlider(Qt::Horizontal);
            (*slider)->setRange(lo, hi);
            (*slider)->setValue(val);
            box->addLayout(head);
            box->addWidget(*slider);
            settings->addLayout(box);
        };
        add_slider("Glow", &glow_slider, &glow_value, 20, 240, 100);
        connect(glow_slider, &QSlider::valueChanged, this, [this](int v) {
            glow_value->setText(QString::number(v / 100.0, 'f', 2));
        });
        add_slider("Edge threshold", &thr_slider, &thr_value, 2, 50, 12);
        connect(thr_slider, &QSlider::valueChanged, this, [this](int v) {
            thr_value->setText(QString::number(v / 100.0, 'f', 2));
        });

        turntable_check = new QCheckBox("Mesh / image -> turntable orbit video");
        spatial_check = new QCheckBox("Spatial glow (stereo pan)");
        spatial_check->setChecked(true);
        neon_audio_check = new QCheckBox("Neonify audio with video");
        hwaccel_check = new QCheckBox("ffmpeg hwaccel decode (optional)");
        for (auto* c : {turntable_check, spatial_check, neon_audio_check, hwaccel_check}) {
            c->setStyleSheet(QString("QCheckBox{color:%1;}").arg(THEME_TEXT));
            settings->addWidget(c);
        }
        auto* adv_btn = new QPushButton("Advanced audio settings…");
        adv_btn->setStyleSheet(btn_style());
        connect(adv_btn, &QPushButton::clicked, this, [this] {
            AdvancedAudioDialog dlg(profile_combo->currentData().toString(), this);
            if (dlg.exec() == QDialog::Accepted) {
                advanced = dlg.result_advanced();
                advanced_on = true;
            }
        });
        settings->addWidget(adv_btn);

        process_btn = new QPushButton("NEONIFY");
        process_btn->setStyleSheet(
            "QPushButton{background-color:#E5E5E5;color:#0A0A0A;border-radius:8px;"
            "font-size:14px;font-weight:bold;padding:10px;}"
            "QPushButton:disabled{background-color:#3a3a3a;color:#666;}");
        connect(process_btn, &QPushButton::clicked, this, [this] { process(); });
        settings->addWidget(process_btn);
        bar = new QProgressBar();
        bar->setStyleSheet(
            QString("QProgressBar{background-color:%1;border:1px solid %3;border-radius:4px;"
                    "text-align:center;color:%2;}"
                    "QProgressBar::chunk{background-color:#E5E5E5;border-radius:3px;}")
                .arg(THEME_SURFACE, THEME_TEXT, THEME_BORDER));
        settings->addWidget(bar);
        status = new QLabel("Ready — drop files to begin");
        status->setWordWrap(true);
        status->setStyleSheet("color:#A0A0A0;font-size:11px;");
        settings->addWidget(status);
        settings->addStretch();
        left->addWidget(settings_panel, 2);

        auto* preview_panel = new QFrame();
        preview_panel->setStyleSheet(
            QString("QFrame{background-color:#121212;border:2px solid #E5E5E5;border-radius:8px;}"
                    "QLabel{border:none;}"));
        auto* preview_lay = new QVBoxLayout(preview_panel);
        preview_lay->addWidget(new QLabel("PREVIEW"));
        preview = new QLabel("Results appear here after processing");
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet("color:#A0A0A0;");
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setWidget(preview);
        preview_scroll = scroll;
        preview_lay->addWidget(scroll, 1);
        right->addWidget(preview_panel, 1);

        body->addLayout(left, 1);
        body->addLayout(right, 1);
        root->addLayout(body, 1);
    }

    QString detect_command(const QString& p) const {
        QString e = QFileInfo(p).suffix().toLower();
        if (e == "obj" || e == "ply" || e == "stl") return "mesh";
        if (e == "wav" || e == "mp3" || e == "flac" || e == "ogg" || e == "m4a" || e == "aac" || e == "wma")
            return "audio";
        if (e == "mp4" || e == "avi" || e == "mov" || e == "mkv" || e == "webm" || e == "gif")
            return "video";
        if (e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "webp" || e == "tif" || e == "tiff")
            return "image";
        return "";
    }

    QString default_output(const QString& p, const QString& command) const {
        QFileInfo fi(p);
        QString stamp = QString::fromStdString(timestamp_suffix());
        if (command == "audio") return fi.absolutePath() + "/" + fi.completeBaseName() + "_neon" + stamp + ".wav";
        if (command == "mesh")
            return fi.absolutePath() + "/" + fi.completeBaseName() + "_neon3d" + stamp +
                   (turntable_check->isChecked() ? ".mp4" : ".png");
        return fi.absolutePath() + "/" + fi.completeBaseName() + "_neon" + stamp + fi.suffix();
    }

    void process() {
        QStringList paths;
        for (int i = 0; i < list->count(); i++) paths << list->item(i)->text();
        if (paths.isEmpty()) {
            QMessageBox::warning(this, "NEONIFY", "Add or drop at least one supported file first.");
            return;
        }
        worker = new Worker();
        for (const auto& p : paths) {
            QString command = detect_command(p);
            if (command.isEmpty()) continue;
            Job j;
            j.command = command.toStdString();
            j.input = p.toStdString();
            j.output = default_output(p, command).toStdString();
            j.opts.palette = palette_combo->currentData().toString().toStdString();
            j.opts.glow = glow_slider->value() / 100.0f;
            j.opts.threshold = thr_slider->value() / 100.0f;
            j.opts.env = 1.0f;
            j.opts.spatial = spatial_check->isChecked();
            j.opts.neon_audio = neon_audio_check->isChecked();
            j.opts.profile = profile_combo->currentData().toString().toStdString();
            j.opts.advanced = advanced;
            if (command == "mesh" && turntable_check->isChecked()) j.opts.turntable = 120;
            worker->jobs.push_back(j);
        }
        if (worker->jobs.empty()) {
            QMessageBox::warning(this, "NEONIFY", "No files matched the selected mode.");
            delete worker;
            worker = nullptr;
            return;
        }
        process_btn->setEnabled(false);
        bar->setValue(0);
        status->setText(QString("Processing %1 file(s)...").arg(worker->jobs.size()));
        connect(worker, &Worker::progress, this, [this](int percent, const QString& step) {
            bar->setValue(percent);
            status->setText(step);
        });
        connect(worker, &Worker::file_done, this, [this](const QString&, const QString& out) {
            last_outputs.push_back(out);
        });
        connect(worker, &Worker::all_done, this, [this] {
            process_btn->setEnabled(true);
            status->setText(QString("Done — %1 file(s) generated").arg(last_outputs.size()));
            if (!last_outputs.isEmpty()) show_preview(last_outputs.last());
            worker = nullptr;
        });
        connect(worker, &Worker::failed, this, [this](const QString& msg) {
            process_btn->setEnabled(true);
            bar->setValue(0);
            status->setText("Processing failed — " + msg);
            worker = nullptr;
        });
        worker->start();
    }

    void show_preview(const QString& path) {
        QFileInfo fi(path);
        QString e = fi.suffix().toLower();
        QPixmap pm(path);
        if (!pm.isNull()) {
            preview->setPixmap(pm.scaled(preview_scroll->viewport()->size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
            return;
        }
        if (e == "wav" || e == "mp3" || e == "flac") {
            preview->setText("Audio written — " + fi.fileName());
            return;
        }
        if (e == "mp4" || e == "mov" || e == "mkv" || e == "webm") {
            std::string snap = (fi.absolutePath() + "/neon_preview_" + QString::fromStdString(timestamp_suffix().substr(1)) + ".png").toStdString();
            bool ok = run_ok({"ffmpeg", "-y", "-loglevel", "error", "-ss", "0.5", "-i", path.toStdString(),
                              "-frames:v", "1", snap});
            if (ok) {
                QPixmap sp(QString::fromStdString(snap));
                if (!sp.isNull()) {
                    preview->setPixmap(sp.scaled(preview_scroll->viewport()->size(), Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));
                    return;
                }
            }
        }
        preview->setText("Output — " + fi.fileName());
    }

    void add_files() {
        QStringList files = QFileDialog::getOpenFileNames(this, "Select media files", QString(),
                                                          "Media files (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff "
                                                          "*.mp4 *.avi *.mov *.mkv *.webm *.gif *.wav *.mp3 *.flac *.ogg *.m4a *.aac "
                                                          "*.obj *.ply *.stl);;All files (*.*)");
        for (const auto& f : files) list->addItem(f);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == list->viewport() && ev->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(ev);
            if (de->mimeData()->hasUrls()) de->acceptProposedAction();
            return true;
        }
        if (obj == list->viewport() && ev->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(ev);
            for (const auto& url : de->mimeData()->urls()) {
                QString p = url.toLocalFile();
                if (!p.isEmpty()) list->addItem(p);
            }
            de->acceptProposedAction();
            return true;
        }
        return QMainWindow::eventFilter(obj, ev);
    }

private:
    static QString btn_style() {
        return QString("QPushButton{background-color:%1;color:%2;border:1px solid #3a3a3a;"
                       "border-radius:5px;padding:6px 12px;}"
                       "QPushButton:hover{border:1px solid %2;}")
            .arg(THEME_SURFACE, THEME_TEXT);
    }
    QListWidget* list;
    QComboBox* mode_combo;
    QComboBox* palette_combo;
    QComboBox* profile_combo;
    QSlider* glow_slider;
    QSlider* thr_slider;
    QLabel* glow_value;
    QLabel* thr_value;
    QCheckBox* turntable_check;
    QCheckBox* spatial_check;
    QCheckBox* neon_audio_check;
    QCheckBox* hwaccel_check;
    QPushButton* process_btn;
    QProgressBar* bar;
    QLabel* status;
    QLabel* preview;
    QScrollArea* preview_scroll = nullptr;
    Worker* worker = nullptr;
    QStringList last_outputs;
    Advanced advanced;
    bool advanced_on = false;
};

int run_gui(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QString here = QCoreApplication::applicationDirPath();
    for (const QString& c : {here + "/logo.png", here + "/../assets/logo.png",
                             here + "/../share/neonify/logo.png"}) {
        if (QFileInfo::exists(c)) {
            app.setWindowIcon(QIcon(c));
            break;
        }
    }
    MainWindow w;
    w.show();
    return app.exec();
}

}  // namespace neon_gui
