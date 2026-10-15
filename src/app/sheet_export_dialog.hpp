#pragma once
#include "app/settled_dialog.hpp"
#include "app/theme.hpp"
#include "io/sheet_export.hpp"
#include <QDialog>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <map>
#include <optional>
class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPrinter;
class QPushButton;
class QRadioButton;
class QVBoxLayout;
namespace sketchy {
class SheetExportFindingBlock;
// R092.b: "Export sheets as PDF" and "Print sheets" (ADR 0164 "R092 export", the approved
// ExportPreflight design). The pages, the output file or printer, and the live "Checked before
// publishing" list built from preflightSheetExport findings, grouped by code. The preflight
// runs on a worker against a document snapshot and runs again whenever the options or the
// document change. The confirming button stays disabled while a blocking finding is
// unresolved; the opt-in check boxes set allowBroken and allowChanged.
//
// The dialog never edits the document or writes a file: closing it reports an outcome, and the
// window exports, prints or goes to the view.
class SheetExportDialog final : public Settled<QDialog> {
  public:
    enum class Mode { Export, Print };
    enum class Outcome { Cancel, Confirm, GoToView, ReviewChanged, ChooseFont };
    struct Setup {
        Mode mode{Mode::Export};
        QString documentName; // The document's name without its extension ("house").
        Id currentSheet{};    // The page shown in Sheets; zero when there is none.
        QString path;         // Export: the proposed output file.
        QPrinter *printer{};  // Print: the window's printer and paper.
        ThemeColors colors;
    };
    SheetExportDialog(const Document &doc, Setup setup, QWidget *parent);
    ~SheetExportDialog() override;

    // The window calls this after every change; a new revision runs the preflight again.
    void documentChanged();
    void setTheme(const ThemeColors &colors);

    Outcome outcome() const { return outcome_; }
    // The view for Go to view and Review and accept, or the item for Choose another font.
    Id target() const { return target_; }
    // The chosen pages and opt-ins. The raster renderer is the caller's.
    SheetExportOptions options() const;
    QString path() const { return path_; }
    // The latest preflight for the current options and revision, if it has arrived.
    const std::optional<SheetExportPreflight> &preflight() const { return preflight_; }
    bool checking() const { return running_ || pending_ || rerun_.isActive(); }
    // Print: the pages larger than the printer's paper (printed at 100% and cut off).
    std::vector<Id> oversizedPages() const;

  private:
    void build();
    void schedule(int delayMs = 0);
    void start();
    void finished(std::uint64_t generation, std::optional<SheetExportPreflight> result,
                  QString error);
    void refreshPages();
    void refreshList();
    void refreshFooter();
    void refreshTarget();
    void chooseFile();
    void choosePrinter();
    void close(Outcome outcome, Id target);
    std::optional<std::vector<Id>> selectedSheets(QString *error) const;
    QString pageLabel(Id sheet) const;
    QString styleSheetFor(const ThemeColors &colors) const;

    const Document &doc_;
    Setup setup_;
    Outcome outcome_{Outcome::Cancel};
    Id target_{};
    QString path_;
    // The preflight worker: one run at a time, against a readSnapshot of the document.
    QThreadPool pool_;
    QTimer rerun_;
    bool running_{}, pending_{};
    std::uint64_t generation_{}, revision_{};
    std::optional<SheetExportPreflight> preflight_;
    QString preflightError_;

    QLabel *subline_{};
    QRadioButton *all_{}, *current_{}, *range_{};
    QButtonGroup *pages_{};
    QLineEdit *rangeText_{};
    QLabel *targetLabel_{};
    QPushButton *choose_{};
    QWidget *checksArea_{};
    QVBoxLayout *list_{};
    QLabel *checkingLabel_{};
    std::map<QString, SheetExportFindingBlock *> blocks_;
    bool allowBroken_{}, allowChanged_{};
    QLabel *hint_{};
    QPushButton *cancel_{}, *confirm_{};
};
} // namespace sketchy
