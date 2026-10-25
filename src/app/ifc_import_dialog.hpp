#pragma once
// R097.e1: the IFC import options dialog (ADR 0171 section 16.2). One file, its size and schema,
// the unit for files that declare none, the IFC Space tag choice, the georeference choice, the
// maximum deviation, the helper line, a banner for a refusal or the limited state, and the
// "what import does" / "what to do next" column. The cheap checks run on the chosen file; the real
// work happens later, in the progress sheet.
#include "app/settled_dialog.hpp"
#include "io/ifc_import.hpp"
#include "io/ifc_worker.hpp"
#include "io/step_worker.hpp"
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
    // R098.a3: the same screen as the STEP import's options (ADR 0172 section 8): the IFC-only
    // choices (spaces, georeference) hidden and the tessellation's crease angle in their place.
    // Call it before setFile; the default is IFC.
    enum class Kind { Ifc, Step };
    void setKind(Kind kind);
    bool isStep() const { return kind_ == Kind::Step; }
    StepWorkerOptions stepWorkerOptions() const; // deviation, angle and the unit for a file that declares none
    bool refused() const;
    QString fileName() const;
    void chooseFile(); // the "Choose…" / "Choose another file" button
  private:
    void build();
    QLabel *title;
    QLabel *subline;
    QLabel *fileRow;
    QLabel *fileFacts;
    QComboBox *unit;
    QWidget *spacesSection;
    QLabel *spacesHeading;
    QRadioButton *spaceHidden;
    QRadioButton *spaceShown;
    QRadioButton *spaceSkip;
    QWidget *geoSection;
    QLabel *geoHeading;
    QRadioButton *geoKeep;
    QRadioButton *geoGrid;
    QDoubleSpinBox *deviation;
    QLabel *angleLabel;
    QDoubleSpinBox *angle; // the STEP crease angle, in degrees (the STEP kind only)
    QLabel *helperLine;
    QLabel *banner;
    QLabel *doesText;
    QLabel *nextText;
    QPushButton *importButton;
    bool refused_ = false;
    qint64 bytes_ = 0;
    QString schema_;
    Kind kind_ = Kind::Ifc;
    IfcProbe probe_;
    void applyProbeBanner(); // the banner is the probe's own warning: shown when Limited, hidden otherwise
};
} // namespace sketchy
