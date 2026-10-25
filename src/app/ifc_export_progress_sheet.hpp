#pragma once
// R097.e2: the IFC export progress sheet (ADR 0171 section 16.8) — the import sheet's own screen
// for the export: titled "Exporting IFC" with the four steps (Capture the model, Build the file,
// Check its size, Publish the file), a bar, the elapsed time and a Cancel that ends the helper,
// removes the private temporary file and writes nothing. The export runs on a worker thread
// (IfcExportRunner); this sheet only shows it and ends it.
#include "app/settled_dialog.hpp"
#include "app/ifc_export_runner.hpp"
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
namespace sketchy {
class IfcExportProgressSheet : public Settled<QDialog> {
    Q_OBJECT
  public:
    IfcExportProgressSheet(IfcExportRunner *runner, const QString &fileName, QWidget *parent = nullptr);
    // Escape or the title bar's close while the export runs is Cancel: it ends the helper, and
    // the sheet closes when the runner has finished (so the window never reads a running export).
    void reject() override;
  public slots:
    void phase(const QString &name, qint64 value);
  private:
    void build(const QString &fileName);
    void markStep(int index, bool working);
    IfcExportRunner *runner;
    QLabel *fileLabel;
    QLabel *stepState[4];
    QProgressBar *bar;
    QLabel *elapsed;
    QTimer *clock;
    int currentStep = 0; // the "capture" step
};
} // namespace sketchy
