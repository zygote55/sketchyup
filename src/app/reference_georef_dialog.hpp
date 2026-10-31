#pragma once
// R107.b2.2 (UX §21): "Place georeferenced image". A PNG or JPEG with a world file, or a GeoTIFF, is read
// on a worker, placed against the site as the person sets its zone, datum, elevation, credit and name, and
// imported as one undoable edit through the R107.b2.1 engine. Nothing is imported until Import is chosen.

#include "io/orthophoto_import.hpp"

#include <QDialog>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTimer;
class QWidget;

namespace sketchy {

class Document;
class Viewport;

class ReferenceGeorefDialog : public QDialog {
  public:
    // The file is read on a worker as soon as the dialog opens. The read touches no document; placing
    // reads the document's site on the UI thread, and Import checks the document has not changed since.
    ReferenceGeorefDialog(Document &document, Viewport &view, const QString &path, QWidget *parent = nullptr);
    ~ReferenceGeorefDialog() override;

    // After an accepted import: the new body, the status line's text, and the note the panel shows.
    Id body() const { return body_; }
    QString status() const { return status_; }
    QString note() const { return note_; }
    // True when Choose another file… was chosen after a file-level refusal, so the caller opens the chooser.
    bool chooseAnotherFile() const { return chooseAnother_; }

  protected:
    void reject() override;

  private:
    struct ReadState;
    struct Refusal {
        std::string code; // the refusal code; empty for a message that is not a refusal
        QString sentence;
    };

    Document &doc_;
    Viewport &view_;
    QString path_;
    std::shared_ptr<ReadState> read_;
    std::optional<OrthophotoFile> file_;
    std::optional<OrthophotoPlacement> placement_;
    std::optional<std::uint64_t> placedRevision_;
    std::optional<Document::SaveStamp> placedStamp_;
    std::optional<Refusal> refusal_;
    bool fileRefused_{}; // the refusal is about the file, so the settings are not usable
    bool zoneNeeded_{};  // a world file with no zone to take: Import waits for the zone
    bool reading_{true};
    bool chooseAnother_{};
    Id body_{};
    QString status_;
    QString note_;
    QTimer *poll_{};

    // The settings, in the Tab order of UX §21.5.
    QLineEdit *name_{}, *elevation_{}, *credit_{};
    QComboBox *zone_{};
    QRadioButton *wgs84_{}, *nad83_{};
    QLabel *image_{}, *position_{}, *zoneFixed_{}, *datumFixed_{}, *datumCaption_{}, *corners_{}, *origin_{},
        *originNote_{}, *banner_{}, *statusLabel_{};
    QPlainTextEdit *report_{};
    QWidget *reportPane_{};
    QProgressBar *progress_{};
    QPushButton *copyReport_{}, *saveReport_{}, *showReport_{}, *chooseAnother_button_{}, *cancel_{}, *import_{};

    void startRead();
    void readFinished(ReadState &state);
    void updateProgress();
    void place();
    void refresh();
    bool worldFile() const;
    bool zoneChosen() const;
    OrthophotoOptions options() const;
    std::string nextStep() const;
    QJsonObject reportJson() const;
    QString reportText(bool fullDigest) const;
    void saveReport(const QString &path) const;
    void importNow();
    void announce(QWidget *widget, const QString &message) const;
};

} // namespace sketchy
