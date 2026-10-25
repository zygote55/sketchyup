#pragma once
// R098.c2 (ADR 0172 sections 2, 3 and 6): the STEP export dialog, opened by File > Export STEP... It
// shows the plan computed from the open model before anything is written (the capture's own plan, the
// numbers the export will check): the schema choice (AP214 by default, AP203), what will be written,
// one line per kind that will not, the size warning above about 250,000 triangles and What this is (a
// mesh, not parametric surfaces). The Tab order is the schema choice, then the form's scroll region,
// then Cancel, then Export to file...; Enter exports, Esc cancels, an arrow key scrolls the form. A
// cheap refusal (an empty model, a body or a package over its bound) is a red banner here with Export
// disabled; the file is not chosen yet.
#include "app/ifc_availability.hpp"
#include "core/model.hpp"
#include "io/step_export.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <vector>
namespace sketchy {
// A kind the STEP export does not write (ADR 0172 sections 2 and 3, and the R098.c notes). The plan
// counts a few of them (countKey names the plan's member); the rest are always listed by name.
struct StepNotWrittenKind {
    const char *key;
    const char *label;
    const char *countKey; // nullptr: no count, the line is always shown
};
// The kinds in the order the dialog and the report show them.
const std::vector<StepNotWrittenKind> &stepNotWrittenKinds();
// The count of one kind in a plan (0 for a kind without a count).
int stepNotWrittenCount(const QJsonObject &plan, const StepNotWrittenKind &kind);
class StepExportDialog : public QDialog {
    Q_OBJECT
  public:
    // The triangles above which a file may exceed the 256 MiB limit (ADR 0172 section 6): an amber
    // note, not a refusal. A test may set its own threshold.
    static constexpr qint64 kTriangleWarningLimit = 250000;
    explicit StepExportDialog(QWidget *parent = nullptr);
    void setProbe(const IfcProbe &probe); // the helper line (the export needs the helper, not the kernel)
    void setDocument(const Document &document); // the capture's plan, or the cheap-refusal banner
    void setTriangleWarningLimit(qint64 triangles);
    StepExportOptions options() const; // the schema the user chose
    const QJsonObject &plan() const { return plan_; }
    bool refused() const { return refused_; }
  private:
    void build();
    void fillPlan(const QJsonObject &plan);
    void showRefusal(const std::string &code, const QString &message);
    void updateSizeWarning();
    QScrollArea *formScroll{};
    QWidget *form{};
    QRadioButton *schemaAp214{};
    QRadioButton *schemaAp203{};
    QLabel *banner{};
    QLabel *sizeWarning{};
    QLabel *helperLine{};
    QPushButton *go{};
    QPushButton *cancel{};
    QJsonObject plan_;
    bool refused_ = false;
    qint64 triangleLimit_ = kTriangleWarningLimit;
};
} // namespace sketchy
