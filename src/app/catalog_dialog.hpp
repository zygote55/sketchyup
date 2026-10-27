#pragma once
// R100.f3 (UX_DESIGN §18.1, §18.5 to §18.7; ADR 0174 sections 4, 5 and 8): the Asset catalogs
// dialog. It lists the catalogs in the library folder and any file the person opens, groups their
// entries with a state and a reason, and inserts the selection as one catalog.import batch: one undo
// step, checked first by the engine's shared dry run. A refused insert opens the missing-dependency
// report. The dialog never changes a catalog file. A catalog that cannot be read is listed as
// refused, and its entries are never inserted.
#include "core/model.hpp"
#include "io/catalog_io.hpp"

#include <QDialog>
#include <QJsonArray>
#include <QString>

#include <optional>
#include <set>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTreeWidget;

namespace sketchy {

class Viewport;

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

  protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    // A selected entry: the path of its catalog and its index in that catalog's entry list.
    using Ref = std::pair<QString, size_t>;

    const CatalogSource *sourceAt(const QString &path) const;
    bool matches(const CatalogSource &source, const CatalogEntryReport &entry) const;
    std::vector<Ref> chosen() const;
    QJsonArray commands(const std::vector<Ref> &refs) const;
    QString insertLabel(const std::vector<Ref> &refs) const;

    void refreshLibrary();
    void rebuildTree();
    void chooseFolder();
    void openFile();
    void selectionChanged();
    void showDetails();
    void setInsertState(bool enabled, const QString &text, const QString &reason);
    void refreshInsert();
    void insertSelected();
    void reloadSources(const std::set<QString> &paths);
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

// How a catalog dependency stands (ADR 0174 section 5). Present is the only state an insert accepts.
enum class DependencyState { Present, Missing, Tampered, NotImportable, NotEarlier };

// The state of one dependency of the entry numbered `owner`. An entry may depend only on an
// earlier entry.
DependencyState dependencyState(const CatalogPackage &package, unsigned owner,
                                const CatalogDependency &dependency);

// Why a catalog entry cannot be imported by its own record: the first dependency that is not
// present, or the reader's reason for its own hash or payload. Empty when it can be imported.
QString entryRefusal(const CatalogPackage &package, const CatalogEntryReport &entry);

} // namespace sketchy
