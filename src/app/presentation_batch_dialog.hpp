#pragma once
// R099.d3 (UX_DESIGN §17.4, §17.5, §17.8, §17.10): the batch dialog. It chooses a camera path, up
// to four presets of one device family, a device, a new folder and, optionally, a video. On the
// right it shows the live budget and whether Start is possible, with the reason. Start is the only
// action that starts a batch, and it re-checks Blender and the video encoder first. The dialog
// never runs a nested event loop: its probes report through signals.
#include "app/presentation_batches.hpp"
#include "core/model.hpp"
#include <QDialog>
#include <QPointer>

class QBoxLayout;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;

namespace sketchy {

class BlenderJob;
class Viewport;

class PresentationBatchDialog : public QDialog {
    Q_OBJECT
  public:
    PresentationBatchDialog(PresentationBatches &batches, Document &document, Viewport &view,
                            QWidget *parent = nullptr);
    ~PresentationBatchDialog() override;
    // The reason Start is disabled, or empty when it can start (the first failing rule of §17.4).
    QString startProblem() const;
    // Whether the video encoder check's answer for the chosen Blender is known and says it can
    // encode.
    bool videoAvailable() const;

  signals:
    // A batch started; the Jobs list opens with it selected.
    void started(const QString &id);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    struct Choice {
        std::vector<Id> presets;
        Id path{};
        bool video{};
        PresentationVideoCodec codec{PresentationVideoCodec::H264};
        bool burnCredits{true};
    };
    PresentationBatches &batches_;
    Document &doc_;
    Viewport &view_;
    QBoxLayout *columns_{};
    QComboBox *path_{}, *backend_{}, *device_{}, *codec_{};
    QListWidget *presets_{};
    QLineEdit *folder_{};
    QCheckBox *fallback_{}, *burnCredits_{}, *video_{};
    QPushButton *chooseFolder_{}, *recheckVideo_{}, *chooseBlender_{}, *checkBlender_{};
    QPushButton *cancel_{}, *start_{};
    QFrame *setup_{}, *group_{};
    QLabel *pathNote_{}, *presetNote_{}, *videoState_{}, *folderNote_{}, *burnNote_{},
        *setupText_{};
    QLabel *blenderLine_{}, *refusal_{}, *startLine_{}, *footerNote_{};
    QLabel *frameValue_{}, *frameDetail_{}, *outputValue_{}, *outputDetail_{};
    QLabel *spaceValue_{}, *spaceDetail_{}, *blenderValue_{}, *blenderDetail_{};
    QFrame *frameCard_{}, *outputCard_{}, *spaceCard_{}, *blenderCard_{};
    QPointer<BlenderJob> devices_;
    // The device check's answer: the Blender's version and its devices, once checked.
    QString blenderVersion_;
    bool devicesReady_{}, devicesChecking_{}, videoChecking_{}, pendingStart_{};
    bool blenderMissing_{};
    QString blenderFailure_;
    // The folder the dialog suggested, so a path change replaces it and never a folder typed by
    // hand.
    QString suggestion_;

    Choice choice() const;
    BlenderJob::Options options() const;
    // Blender options for a device probe: the device is not chosen, so the probe names only the
    // list.
    BlenderJob::Options probeOptions() const;
    QString executable() const;
    void fillPaths();
    void fillPresets();
    void presetRowChanged();
    void chooseParent();
    void probeDevices();
    void devicesFinished();
    void chooseExecutable();
    void refreshVideo(bool force);
    void videoFinished();
    void updateVideoState();
    void updateAll();
    void updateBudget(const Choice &selected);
    void updateStart();
    void startBatch();
    void showRefusal(const QString &text);
    void setBlenderState();
    void stackForText();
    QString defaultFolder() const;
    QString pathName() const;
    void updatePathNote() const;
    qint64 outputCeiling(const Choice &selected) const;
    int plannedFrames(const Choice &selected) const;
    QString folderProblem() const;
    QString oddSizeProblem(const Choice &selected) const;
    QString presetMismatch(const Choice &selected) const;
    QString diskProblem(const Choice &selected) const;
};

} // namespace sketchy
