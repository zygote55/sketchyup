#include "app/styles_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
namespace {
constexpr std::array modes{ModelStyleMode::Textured, ModelStyleMode::Shaded,
                           ModelStyleMode::Monochrome, ModelStyleMode::Wireframe,
                           ModelStyleMode::XRay};
QComboBox *modeBox() {
    auto *box = new QComboBox;
    box->addItems({"Textured", "Shaded", "Monochrome", "Wireframe", "X-ray"});
    return box;
}
int modeIndex(ModelStyleMode mode) {
    return int(std::find(modes.begin(), modes.end(), mode) - modes.begin());
}
QString decimal(double value) {
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(value, 'g', 12);
}
double scalar(QLineEdit *field, double original) {
    if (!field->isModified())
        return original;
    bool valid = false;
    const auto value = QLocale().toDouble(field->text(), &valid);
    if (!valid || !std::isfinite(value))
        throw std::runtime_error("Enter a finite number");
    return value;
}
} // namespace
StylesPanel::StylesPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("stylesPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        "Display settings are saved with this model. Application theme is set in View → Theme.");
    note->setWordWrap(true);
    layout->addWidget(note);
    mode_ = modeBox();
    mode_->setObjectName("styleMode");
    mode_->setAccessibleName("Model display mode");
    layout->addWidget(mode_);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!syncing_ && index >= 0 && index < int(modes.size()))
            change([&](ModelStyle &style) { style.mode = modes[size_t(index)]; });
    });
    auto flag = [&](const QString &text, const QString &name, bool ModelStyle::*field) {
        auto *box = new QCheckBox(text);
        box->setObjectName(name);
        layout->addWidget(box);
        flags_.push_back({box, field});
        connect(box, &QCheckBox::toggled, this, [this, field](bool checked) {
            if (!syncing_)
                change([&](ModelStyle &style) { style.*field = checked; });
        });
    };
    flag("Edges", "styleEdges", &ModelStyle::edgesVisible);
    flag("Profiles", "styleProfiles", &ModelStyle::profiles);
    flag("Grid", "styleGrid", &ModelStyle::gridVisible);
    flag("Axes", "styleAxes", &ModelStyle::axesVisible);
    flag("Ground", "styleGround", &ModelStyle::groundVisible);
    auto *edit = new QPushButton("Colors and details…");
    edit->setObjectName("styleCustomize");
    layout->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, [this] { customize(); });
    auto *reset = new QPushButton("Reset model style");
    reset->setObjectName("styleReset");
    layout->addWidget(reset);
    connect(reset, &QPushButton::clicked, this,
            [this] { change([](ModelStyle &style) { style = {}; }); });
    auto *hint =
        new QLabel("Wireframe keeps edges visible. Shaded and monochrome retain transparent "
                   "cutouts. Ground is a visual reference and does not block selection.");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    error_ = new QLabel;
    error_->setObjectName("styleError");
    error_->setWordWrap(true);
    layout->addWidget(error_);
    layout->addStretch();
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}
void StylesPanel::refresh() {
    syncing_ = true;
    mode_->setCurrentIndex(modeIndex(doc_.style().mode));
    for (auto [box, field] : flags_)
        box->setChecked(doc_.style().*field);
    syncing_ = false;
}
void StylesPanel::change(const std::function<void(ModelStyle &)> &operation) {
    try {
        auto style = doc_.style();
        operation(style);
        view_.applyModelStyle(style);
        error_->clear();
        refresh();
    } catch (const std::exception &error) {
        error_->setText(error.what());
        refresh();
    }
}
void StylesPanel::customize() {
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    const auto original = doc_.style();
    auto candidate = original;
    QDialog dialog(this);
    dialog.setObjectName("styleDialog");
    dialog.setWindowTitle("Model style");
    auto *form = new QFormLayout(&dialog);
    auto *mode = modeBox();
    mode->setObjectName("styleDialogMode");
    mode->setCurrentIndex(modeIndex(original.mode));
    form->addRow("Display mode", mode);
    auto color = [&](const QString &label, const QString &name,
                     std::array<float, 3> ModelStyle::*field) {
        auto *button = new QPushButton;
        button->setObjectName(name);
        auto update = [&, button, field] {
            const auto &rgb = candidate.*field;
            const auto value = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
            button->setText(value.name(QColor::HexRgb));
            QPixmap swatch(24, 16);
            swatch.fill(value);
            button->setIcon(QIcon(swatch));
        };
        update();
        form->addRow(label, button);
        connect(button, &QPushButton::clicked, &dialog, [&, field, update, label] {
            const auto &rgb = candidate.*field;
            QColorDialog picker(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]), &dialog);
            picker.setWindowTitle(label);
            picker.setOption(QColorDialog::DontUseNativeDialog);
            // Qt initializes this chooser at 8-bit channel precision. An unchanged
            // acceptance must preserve the original document's float channels.
            const auto baseline = picker.currentColor();
            if (picker.exec() == QDialog::Accepted && picker.currentColor() != baseline) {
                const auto selected = picker.currentColor();
                candidate.*field = {float(selected.redF()), float(selected.greenF()),
                                    float(selected.blueF())};
                update();
            }
        });
    };
    color("Background", "styleBackgroundColor", &ModelStyle::background);
    color("Ground", "styleGroundColor", &ModelStyle::ground);
    color("Monochrome front", "styleFrontColor", &ModelStyle::front);
    color("Monochrome back", "styleBackColor", &ModelStyle::back);
    color("Edges and profiles", "styleEdgeColor", &ModelStyle::edge);
    auto *ground = new QLineEdit(displayLength(original.groundHeight, doc_.displayUnits(), fullDisplayPrecision));
    ground->setObjectName("styleGroundHeight");
    form->addRow("Ground height", ground);
    auto *width = new QLineEdit(decimal(original.profileWidth));
    width->setObjectName("styleProfileWidth");
    form->addRow("Profile width (px)", width);
    auto *opacity = new QLineEdit(decimal(original.xrayOpacity * 100));
    opacity->setObjectName("styleXrayOpacity");
    form->addRow("X-ray opacity (%)", opacity);
    auto *error = new QLabel;
    error->setObjectName("styleDialogError");
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error(
                    "The model changed while editing. Cancel and reopen the style editor.");
            candidate.mode = modes.at(size_t(mode->currentIndex()));
            candidate.groundHeight =
                ground->isModified()
                    ? parseLength(ground->text(), inputUnit(doc_.displayUnits()), QLocale())
                    : original.groundHeight;
            candidate.profileWidth = scalar(width, original.profileWidth);
            candidate.xrayOpacity = scalar(opacity, original.xrayOpacity * 100) / 100;
            // Preserve untouched doubles exactly, including values not representable in percent.
            if (!opacity->isModified())
                candidate.xrayOpacity = original.xrayOpacity;
            candidate.validate();
            view_.applyModelStyle(candidate);
            dialog.accept();
            refresh();
        } catch (const std::exception &exception) {
            error->setText(exception.what());
        }
    });
    dialog.exec();
}
} // namespace sketchy
