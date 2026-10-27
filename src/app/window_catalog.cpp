#include "app/catalog_dialog.hpp"
#include "app/catalog_export_dialog.hpp"
#include "app/window.hpp"

namespace sketchy {

// R100.f3 (UX_DESIGN §18.5): File > Asset catalogs… The dialog is modal, as the component library's
// Insert dialog is. Its insert is one undo step, and the status bar names what it did.
void Window::catalogDialog() {
    CatalogDialog dialog(doc_, *viewport_, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.insertedStatus().isEmpty())
        status_->setText(dialog.insertedStatus());
}

// R100.f3 (UX_DESIGN §18.1, §18.6): File > Export asset catalog… It writes a new file and never
// changes the model, so it asks no unsaved-changes question.
void Window::exportCatalogDialog() {
    CatalogExportDialog dialog(doc_, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.exportedStatus().isEmpty())
        status_->setText(dialog.exportedStatus());
}

} // namespace sketchy
