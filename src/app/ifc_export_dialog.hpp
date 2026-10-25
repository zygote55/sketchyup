#pragma once
// R097.e2: the IFC export dialog, opened by File > Export IFC... (ADR 0171 section 16.8). It shows
// the plan computed from the open model before anything is written, with no options: what will be
// written (the structure, groups, bodies, component placements, colours, properties, hidden
// bodies and identifiers), what will not be written (one line per kind with its count), and the
// georeferencing outcome in advance (a green line when the site origin and CRS are written, an
// amber "Not written" with its reason and how to fix it otherwise). The Tab order is the form's
// scroll region, then Cancel, then Export to file...; Enter exports, Esc cancels, an arrow key
// scrolls the form. A cheap refusal (an empty model, a document or package over its bound) is a
// red banner here with Export disabled; the file is not chosen yet.
#include "core/model.hpp"
#include "app/ifc_availability.hpp"
#include <QDialog>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
namespace sketchy {
class IfcExportDialog : public QDialog {
    Q_OBJECT
  public:
    explicit IfcExportDialog(QWidget *parent = nullptr);
    void setProbe(const IfcProbe &probe); // the helper line (export needs the helper, not the kernel)
    void setDocument(const Document &document); // planIfcExport: the counts, or the cheap-refusal banner
    const QJsonObject &plan() const { return plan_; }
    bool refused() const { return refused_; }
  private:
    void build();
    void fillPlan(const QJsonObject &plan);
    void showRefusal(const std::string &code, const QString &message);
    QScrollArea *formScroll{};
    QWidget *form{};
    QLabel *banner{};
    QLabel *helperLine{};
    QPushButton *go{};
    QPushButton *cancel{};
    QJsonObject plan_;
    bool refused_ = false;
};
} // namespace sketchy
