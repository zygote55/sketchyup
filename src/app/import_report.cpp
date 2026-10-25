#include "app/import_report.hpp"
#include <QPushButton>
namespace sketchy {
// The report chip (the design's mockup `after`): while the imported model is open it says
// what the import produced and reopens the report; the accessible name follows the design's
// 16.9 wording. A cancel or a failed import (the model was not replaced) leaves the previous
// state, and a new model clears it.
void setImportReportChip(QPushButton *chip, const ImportReportFace &face,
                          const ImportReportState &state) {
    if (!chip)
        return;
    if (!state.done) {
        chip->hide();
        return;
    }
    const int attention = face.attention(state);
    if (attention > 0) {
        chip->setText(QString("\u26a0 %1 import \u00b7 %2 need attention \u00b7 Show report")
                         .arg(face.chipWord)
                         .arg(attention));
        chip->setAccessibleName(
            QString("%1 import, %2 items need attention. Show report").arg(face.chipWord).arg(attention));
    } else {
        chip->setText(QString("%1 import report").arg(face.chipWord));
        chip->setAccessibleName(QString("%1 import report").arg(face.chipWord));
    }
    chip->show();
}
} // namespace sketchy
