#pragma once
// R100.f3 (UX_DESIGN §18.1, §18.6; ADR 0174 section 4): Export asset catalog. The dialog builds a
// `.sketchycat` from the chosen components, materials and the document style, through the engine's
// exportCatalog, and writes it as a new file. It changes nothing in the model. An existing file is
// never replaced, and a record that is missing refuses the export as a whole.
#include "core/model.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace sketchy {

class CatalogExportDialog : public QDialog {
  public:
    CatalogExportDialog(Document &doc, QWidget *parent = nullptr);
    // The status line after an export, empty when nothing was written.
    QString exportedStatus() const { return exported_; }

  private:
    void refreshState();
    void exportToFile();

    Document &doc_;
    QString exported_;
    QLineEdit *name_{};
    QPlainTextEdit *description_{};
    QLineEdit *labels_{};
    QLineEdit *version_{};
    QLineEdit *license_{};
    QListWidget *components_{};
    QListWidget *materials_{};
    QCheckBox *style_{};
    QLabel *reason_{};
    QPushButton *exportButton_{};
};

} // namespace sketchy
