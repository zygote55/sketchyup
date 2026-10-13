#pragma once
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QColorDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QWidget>
namespace sketchy {
// Keyboard focus in a spin box or editable combo box lands on its internal line edit, which
// Qt exposes to accessibility clients without the composite's name or form label (R084.o).
// Call after the form is built: each unnamed internal editor below `root` takes its
// composite control's accessible name.
inline void nameInternalEditors(QWidget *root) {
    auto apply = [](QWidget *composite, QLineEdit *edit) {
        if (!edit || !edit->accessibleName().isEmpty())
            return;
        auto name = composite->accessibleName();
        // A combo box reports its value as its name on Linux; its form label is a relation.
        auto *accessible = QAccessible::queryAccessibleInterface(composite);
        if (name.isEmpty() && accessible) {
            const auto labels = accessible->relations(QAccessible::Label);
            if (!labels.isEmpty() && labels.front().first)
                name = labels.front().first->text(QAccessible::Name);
            else if (!qobject_cast<QComboBox *>(composite))
                name = accessible->text(QAccessible::Name);
        }
        name.remove('&');
        if (!name.isEmpty())
            edit->setAccessibleName(name);
    };
    for (auto *spin : root->findChildren<QAbstractSpinBox *>())
        apply(spin, spin->findChild<QLineEdit *>());
    for (auto *combo : root->findChildren<QComboBox *>())
        if (combo->isEditable())
            apply(combo, combo->lineEdit());
}
// Qt's widget colour dialog leaves its hue/saturation field, value strip and numeric editors
// unnamed. Call before showing it.
inline void nameColorDialog(QColorDialog &dialog) {
    for (auto *widget : dialog.findChildren<QWidget *>()) {
        if (widget->inherits("QtPrivate::QColorPicker"))
            widget->setAccessibleName("Hue and saturation");
        else if (widget->inherits("QtPrivate::QColorLuminancePicker"))
            widget->setAccessibleName("Value");
    }
    nameInternalEditors(&dialog);
}
} // namespace sketchy
