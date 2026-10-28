#include "app/style_dialogs.hpp"
#include "app/window.hpp"
#include <QFileDialog>

namespace sketchy {

// R100.f2 (UX_DESIGN §18.2): File > Import style…. The system chooser comes first, then the Import
// style dialog. Nothing changes until its primary button is pressed. "Choose another file…" reopens
// the chooser, and Esc cancels.
void Window::importStyleDialog() {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return;
    }
    for (;;) {
        QFileDialog file(this, "Import style");
        file.setObjectName("styleImportFileDialog");
        file.setNameFilters({"SketchyUp styles (*.sketchystyle)", "All files (*)"});
        file.setFileMode(QFileDialog::ExistingFile);
        if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
            return;
        StyleImportDialog dialog(doc_, *viewport_, file.selectedFiles().front(), this);
        const auto result = dialog.exec();
        if (result == StyleImportDialog::kChooseAnother)
            continue;
        if (result == QDialog::Accepted) {
            viewport_->cancel();
            sync();
            status_->setText(dialog.outcome());
        }
        viewport_->setFocus(Qt::OtherFocusReason);
        return;
    }
}

// R100.f2 (UX_DESIGN §18.2): File > Export style…. The dialog writes a new file only; an existing
// name is refused, and a missing edge texture is named before anything is written.
void Window::exportStyleDialog() {
    StyleExportDialog dialog(doc_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    status_->setText("Exported style “" + styleNameFromPath(dialog.exportedPath()) +
                     "” · Model unchanged");
    viewport_->setFocus(Qt::OtherFocusReason);
}

} // namespace sketchy
