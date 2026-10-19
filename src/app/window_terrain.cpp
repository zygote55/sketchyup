#include "app/file_operation.hpp"
#include "app/terrain_import_dialog.hpp"
#include "app/window.hpp"
#include "io/terrain_source.hpp"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QScopedValueRollback>
namespace sketchy {
// R095.d (ADR 0168 "Decisions > Interaction design"): File > Import terrain…. Unlike the other
// importers, terrain import adds to the open document (owner decision 11).
void Window::importTerrainDialog() {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return;
    }
    QFileDialog file(this, "Import terrain");
    file.setObjectName("terrainImportFileDialog");
    file.setNameFilters({"Terrain sources (*.dxf *.csv *.txt *.xyz *.pts)", "DXF contours (*.dxf)",
                         "Point files (*.csv *.txt *.xyz *.pts)"});
    file.setFileMode(QFileDialog::ExistingFile);
    if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
        return;
    importTerrainPath(file.selectedFiles().front());
}
void Window::importTerrainPath(const QString &path) {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return;
    }
    const QFileInfo info(path);
    if (info.size() > TerrainSourceLimits::bytes)
        throw std::runtime_error("A terrain source is at most 64 MiB; this file is larger.");
    QByteArray bytes;
    {
        QScopedValueRollback busy(fileOperation_, true);
        bytes = runFileOperation(this, "Reading terrain source…", [path] {
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly))
                throw std::runtime_error("Could not open the terrain source: " +
                                         source.errorString().toStdString());
            auto data = source.read(TerrainSourceLimits::bytes + 1);
            if (data.size() > TerrainSourceLimits::bytes)
                throw std::runtime_error("A terrain source is at most 64 MiB; this file is larger.");
            return data;
        });
    }
    if (bytes.isEmpty())
        throw std::runtime_error("The terrain source is empty.");
    TerrainImportDialog dialog(doc_, {info.fileName(), bytes, sheetColors_}, this);
    terrainImport_ = &dialog;
    const bool imported = dialog.exec() == QDialog::Accepted;
    terrainImport_.clear();
    activateWindow();
    if (!imported) {
        viewport_->setFocus(Qt::OtherFocusReason);
        return;
    }
    const auto &operation = dialog.operation();
    const auto terrain = Id(operation["terrain"].toString().toULongLong());
    viewport_->cancel();
    sync();
    if (doc_.bodies().contains(terrain))
        viewport_->setSelection(terrain);
    const auto report = operation["importReport"].toObject()["terrain"].toObject();
    status_->setText(QString("Imported a terrain: %1 triangles%2 · Undo removes it")
                         .arg(QLocale().toString(qulonglong(report["triangles"].toDouble())))
                         .arg(operation["siteEstablished"].toBool()
                                  ? QString(" · site origin set")
                                  : QString()));
    viewport_->setFocus(Qt::OtherFocusReason);
}
} // namespace sketchy
