#pragma once
// R100.f3 (UX_DESIGN §18.1, §18.5 to §18.7; ADR 0174 sections 4, 5 and 8): the Asset catalogs
// dialog. It lists the catalogs in the library folder and any file the person opens, groups their
// entries with a state and a reason, and inserts the selection as one catalog.import batch: one
// undo step, checked first by the engine's shared dry run. A refused insert opens the
// missing-dependency report. The dialog never changes a catalog file. A catalog that cannot be read
// is listed as refused, and its entries are never inserted.
#include "app/exchange_runner.hpp"
#include "automation/catalog_commands.hpp"
#include "core/model.hpp"
#include "io/catalog_io.hpp"

#include <QDialog>
#include <QJsonArray>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class QComboBox;
class QEventLoop;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeWidget;

namespace sketchy {

class ExchangeProgressSheet;
class Viewport;
struct CatalogInsertOutcome;

// One catalog the dialog lists: a file in the library folder, or a file the person opened. A file
// that cannot be read has no package, and `error` says why in the words of UX_DESIGN 18.6.
struct CatalogSource {
    bool fromLibrary{};
    QString folder; // the library folder, for a library catalog
    QString name;   // the file's base name, for a library catalog
    QString path;   // the file read, and the key of its entries
    std::optional<CatalogPackage> package;
    QString error;
    QString title() const;
};

class CatalogDialog : public QDialog {
  public:
    CatalogDialog(Document &doc, Viewport &view, QWidget *parent = nullptr);
    // The status line after an insert, or after a neutral result. Empty when nothing was inserted.
    QString insertedStatus() const { return inserted_; }

    // Tests only (as QuantitiesPanel::runGateForTesting): the insert's worker calls this once it has
    // started, with its cancel check, so a test can hold the insert until its progress sheet is seen.
    // Empty in the application.
    static inline std::function<void(const std::function<bool()> &)> insertGateForTesting;
    // Tests only: how long a slow insert runs before its progress sheet opens. The application's
    // value is one second, the delay of UX_DESIGN 19.3 for a run that shows progress.
    static inline int insertProgressDelayMs = 1000;

  protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    // A selected entry: the path of its catalog and its index in that catalog's entry list.
    using Ref = std::pair<QString, size_t>;

    const CatalogSource *sourceAt(const QString &path) const;
    bool matches(const CatalogSource &source, const CatalogEntryReport &entry) const;
    std::vector<Ref> chosen() const;
    // The batch for `refs`. A library catalog is named; any other is passed as the package listed
    // when it was opened, under a key of `opened`, so its bytes are not sent and decoded again.
    QJsonArray commands(const std::vector<Ref> &refs, OpenedCatalogs &opened) const;
    QString insertLabel(const std::vector<Ref> &refs) const;

    void refreshLibrary();
    void rebuildTree();
    void chooseFolder();
    void openFile();
    void selectionChanged();
    void showDetails();
    void setInsertState(bool enabled, const QString &text, const QString &reason);
    void refreshInsert();
    // Starts the insert on a worker thread (R100.i) and returns once it is committed, reported or
    // cancelled, as an insert run in line did. The progress sheet opens after the delay if the insert
    // is still running, and insertFinished settles the result.
    void insertSelected();
    void showInsertProgress();
    void insertFinished();
    void settleInsert();
    // Takes the catalogs the insert read into the listing, or the reasons they were refused.
    void applyReads(const CatalogInsertOutcome &outcome);
    // The report of an insert that did not commit, after the listing has taken what the insert read.
    void reportRefusal(const CatalogInsertOutcome &outcome, const QString &refusal);
    void showReport(const QString &refusal);

    Document &doc_;
    Viewport &view_;
    QString folder_;
    std::vector<CatalogSource> sources_;
    std::set<Ref> chosen_;
    QString inserted_;
    QString refusal_; // the engine's refusal of the last dry run, empty when it passed
    bool rebuilding_{};
    bool insertEnabled_{};
    // The insert in progress, if any: its runner, what it produces, the timer that opens the
    // progress sheet, the sheet once it is open, and the loop that insertSelected waits in.
    std::unique_ptr<ExchangeRunner> insertRunner_;
    std::shared_ptr<CatalogInsertOutcome> insertOutcome_;
    QTimer *insertDelay_{};
    ExchangeProgressSheet *insertSheet_{};
    QEventLoop *insertLoop_{};

    QLabel *folderLabel_{};
    QLineEdit *search_{};
    QComboBox *kind_{};
    QSplitter *split_{};
    QTreeWidget *list_{};
    QPlainTextEdit *details_{};
    QLabel *notice_{};
    QLabel *reason_{};
    QPushButton *open_{};
    QPushButton *insert_{};
};

// How a catalog dependency stands (ADR 0174 section 5). Present is the only state an insert
// accepts.
enum class DependencyState { Present, Missing, Tampered, NotImportable, NotEarlier };

// The state of one dependency of the entry numbered `owner`. An entry may depend only on an
// earlier entry.
DependencyState dependencyState(const CatalogPackage &package, unsigned owner,
                                const CatalogDependency &dependency);

// Why a catalog entry cannot be imported by its own record: the first dependency that is not
// present, or the reader's reason for its own hash or payload. Empty when it can be imported.
QString entryRefusal(const CatalogPackage &package, const CatalogEntryReport &entry);

} // namespace sketchy
