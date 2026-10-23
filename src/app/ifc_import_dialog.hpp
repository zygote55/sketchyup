#pragma once
// R097.e1: the IFC import options dialog (ADR 0171 section 16.2). One file, its size and schema,
// the unit for files that declare none, the IFC Space tag choice, the georeference choice, the
// maximum deviation, the helper line, a banner for a refusal or the limited state, and the
// "what import does" / "what to do next" column. The cheap checks run on the chosen file; the real
// work happens later, in the progress sheet.
#include "app/settled_dialog.hpp"
#include "io/ifc_import.hpp"
#include "app/ifc_availability.hpp"
#include <QComboBox>
#include <QLabel>
#include <QRadioButton>
#include <QDoubleSpinBox>
namespace sketchy {
class IfcImportDialog : public Settled<QDialog> {
    Q_OBJECT
  public:
    explicit IfcImportDialog(QWidget *parent = nullptr);
    void setProbe(const IfcProbe &probe); // helper line and the limited banner
    void setFile(const QString &path); // runs the cheap checks; sets the facts or a refusal banner
    IfcImportOptions options() const;
    IfcWorkerOptions workerOptions() const; // deviation and unit (thread count and limits are fixed)
    bool refused() const;
    QString fileName() const;
    void chooseFile(); // the "Choose…" / "Choose another file" button
  private:
    void build();
    QLabel *fileRow;
    QLabel *fileFacts;
    QComboBox *unit;
    QRadioButton *spaceHidden;
    QRadioButton *spaceShown;
    QRadioButton *spaceSkip;
    QRadioButton *geoKeep;
    QRadioButton *geoGrid;
    QDoubleSpinBox *deviation;
    QLabel *helperLine;
    QLabel *banner;
    QPushButton *importButton;
    bool refused_ = false;
    qint64 bytes_ = 0;
    QString schema_;
    IfcProbe probe_;
};
} // namespace sketchy
