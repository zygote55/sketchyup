#include "app/presentations_dialog.hpp"
#include "app/render_panel.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"

namespace sketchy {

// R099.d2 (UX_DESIGN §17.1): Camera > Presentations… opens the dialog on its Camera paths tab. The
// dialog is made once and shown again after that, so an unsaved draft path survives closing it.
void Window::presentationsDialog() {
    if (!presentations_)
        presentations_ = new PresentationsDialog(
            doc_, *viewport_,
            [this] { return render_ ? render_->setupSettings() : RenderSettings{}; }, this);
    // R099.d3: the footer's Export batch… opens the batch dialog, as the palette entry does.
    presentations_->setExportBatch([this] { render_->showBatchDialog(); });
    presentations_->showCameraPaths();
}

} // namespace sketchy
