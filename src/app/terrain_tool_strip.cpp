#include "app/terrain_tool_strip.hpp"
#include "app/flow_layout.hpp"
#include "app/keypad_decimal.hpp"
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/measurements.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <cmath>
namespace sketchy {
namespace {
// A label and its field, kept together when the strip wraps. The label is the field's buddy, so
// the field's accessible name is the label's text unless set.
QWidget *labelled(const QString &text, QWidget *field, const QString &name) {
    auto *box = new QWidget;
    box->setObjectName(name + "Box");
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(5);
    auto *label = new QLabel(text);
    label->setObjectName("terrainStripLabel");
    label->setBuddy(field);
    row->addWidget(label);
    row->addWidget(field);
    return box;
}
// A segmented control: checkable buttons in one exclusive group, the container named for the
// screen reader ("Terrain tool", "Scope") and each button by its text.
QWidget *segments(const QString &name, const QString &accessible,
                  const std::vector<std::pair<QString, int>> &choices, QButtonGroup *&group) {
    auto *box = new QWidget;
    box->setObjectName(name);
    box->setAccessibleName(accessible);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    group = new QButtonGroup(box);
    group->setExclusive(true);
    for (const auto &[text, id] : choices) {
        auto *button = new QToolButton;
        button->setObjectName(name + "." + text.toLower());
        button->setText(text);
        button->setAccessibleName(text);
        button->setAccessibleDescription(accessible);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        group->addButton(button, id);
        row->addWidget(button);
    }
    return box;
}
QLineEdit *field(const QString &name, const QString &accessible, int width) {
    auto *edit = new QLineEdit;
    edit->setObjectName(name);
    edit->setAccessibleName(accessible);
    edit->setMinimumWidth(width);
    edit->setMaximumWidth(width + 40);
    acceptKeypadDecimal(edit);
    return edit;
}
} // namespace

TerrainToolStrip::TerrainToolStrip(Viewport &viewport)
    : QFrame(&viewport), viewport_(viewport) {
    setObjectName("terrainToolStrip");
    setAccessibleName("Terrain tool options");
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(true);
    auto *flow = new FlowLayout(this, 10, 6);
    flow->setContentsMargins(8, 6, 8, 6);
    flow->addWidget(segments("terrainToolSwitcher", "Terrain tool",
                             {{"Sculpt", int(Viewport::Tool::Sculpt)},
                              {"Smooth", int(Viewport::Tool::Smooth)},
                              {"Contours", int(Viewport::Tool::Contours)}},
                             tools_));
    connect(tools_, &QButtonGroup::idClicked, this, [this](int id) {
        if (!syncing_)
            emit toolRequested(id);
    });
    auto *divider = new QFrame;
    divider->setObjectName("terrainStripDivider");
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedWidth(1);
    flow->addWidget(divider);
    flow->addWidget(segments("terrainScope", "Scope",
                             {{"Brush", int(TerrainToolOptions::Scope::Brush)},
                              {"Region", int(TerrainToolOptions::Scope::Region)}},
                             scopes_));
    connect(scopes_, &QButtonGroup::idClicked, this, [this](int id) {
        if (syncing_)
            return;
        auto options = viewport_.terrainOptions();
        options.scope = TerrainToolOptions::Scope(id);
        viewport_.setTerrainOptions(options);
    });
    radius_ = field("terrainRadius", "Radius", 70);
    radiusBox_ = labelled("Radius", radius_, "terrainRadius");
    flow->addWidget(radiusBox_);
    falloff_ = new QComboBox;
    falloff_->setObjectName("terrainFalloff");
    falloff_->setAccessibleName("Falloff");
    falloff_->addItem("Smooth", int(TerrainFalloff::Smooth));
    falloff_->addItem("Linear", int(TerrainFalloff::Linear));
    falloff_->addItem("Constant", int(TerrainFalloff::Constant));
    flow->addWidget(labelled("Falloff", falloff_, "terrainFalloff"));
    connect(falloff_, &QComboBox::activated, this, [this](int index) {
        if (syncing_)
            return;
        auto options = viewport_.terrainOptions();
        options.falloff = TerrainFalloff(falloff_->itemData(index).toInt());
        viewport_.setTerrainOptions(options);
    });
    width_ = field("terrainFalloffWidth", "Falloff width", 70);
    widthBox_ = labelled("Falloff width", width_, "terrainFalloffWidth");
    flow->addWidget(widthBox_);
    amount_ = field("terrainAmount", "Amount", 70);
    amountBox_ = labelled("Amount", amount_, "terrainAmount");
    flow->addWidget(amountBox_);
    strength_ = field("terrainStrength", "Strength", 50);
    strengthBox_ = labelled("Strength", strength_, "terrainStrength");
    flow->addWidget(strengthBox_);
    passes_ = new QSpinBox;
    passes_->setObjectName("terrainPasses");
    passes_->setAccessibleName("Passes");
    passes_->setRange(1, terrainSmoothIterationsMax);
    passesBox_ = labelled("Passes", passes_, "terrainPasses");
    flow->addWidget(passesBox_);
    connect(passes_, &QSpinBox::valueChanged, this, [this](int value) {
        if (syncing_)
            return;
        auto options = viewport_.terrainOptions();
        options.passes = value;
        viewport_.setTerrainOptions(options);
    });
    hint_ = new QLabel("Ctrl lowers · Shift smooths");
    hint_->setObjectName("terrainStripHint");
    flow->addWidget(hint_);
    for (auto *control : std::initializer_list<QWidget *>{
             scopes_->buttons().front()->parentWidget(), radiusBox_,
             falloff_->parentWidget(), widthBox_, amountBox_, strengthBox_, passesBox_, hint_})
        sculptControls_ << control;
    // R096.f2 (UX §15.3): Edit contours. The selected source ("Contour 105.00 m · source 17"), its
    // Role and Elevation, then the Add buttons, Promote and Remove.
    sourceSummary_ = new QLabel;
    sourceSummary_->setObjectName("terrainSourceSummary");
    sourceSummary_->setTextFormat(Qt::RichText);
    flow->addWidget(sourceSummary_);
    role_ = new QComboBox;
    role_->setObjectName("terrainSourceRole");
    role_->setAccessibleName("Role of the selected source");
    role_->addItem("Contour", int(TerrainSourceRole::Contour));
    role_->addItem("Breakline", int(TerrainSourceRole::Breakline));
    roleBox_ = labelled("Role", role_, "terrainSourceRole");
    flow->addWidget(roleBox_);
    connect(role_, &QComboBox::activated, this, [this](int index) {
        if (syncing_)
            return;
        viewport_.setSelectedSourceRole(TerrainSourceRole(role_->itemData(index).toInt()));
    });
    elevation_ = field("terrainSourceElevation", "Elevation of the selected source", 80);
    elevationBox_ = labelled("Elevation", elevation_, "terrainSourceElevation");
    flow->addWidget(elevationBox_);
    auto button = [&](const QString &text, const QString &name, const QString &description,
                      bool checkable) {
        auto *b = new QToolButton;
        b->setObjectName(name);
        b->setProperty("terrainStripButton", true);
        b->setText(text);
        b->setAccessibleName(text);
        b->setAccessibleDescription(description);
        b->setToolTip(description);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::StrongFocus);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->installEventFilter(this);
        flow->addWidget(b);
        return b;
    };
    addContour_ = button("Add contour", "terrainAddContour",
                         "Draw a new source contour on the terrain; Enter finishes it", true);
    addBreakline_ = button("Add breakline", "terrainAddBreakline",
                           "Draw a new breakline on the terrain; Enter finishes it", true);
    addSpot_ = button("Add spot", "terrainAddSpot", "Place spot points on the terrain", true);
    promote_ = button("Promote", "terrainPromote",
                      "Make the display contour under the pointer a source contour", true);
    removeSource_ = button("Remove", "terrainRemoveSource",
                           "Remove the selected source (Shift+Delete)", false);
    const std::vector<std::pair<QToolButton *, Viewport::ContourMode>> modes{
        {addContour_, Viewport::ContourMode::AddContour},
        {addBreakline_, Viewport::ContourMode::AddBreakline},
        {addSpot_, Viewport::ContourMode::AddSpot},
        {promote_, Viewport::ContourMode::Promote}};
    for (const auto &[b, mode] : modes)
        connect(b, &QToolButton::clicked, this, [this, mode = mode] {
            if (!syncing_)
                viewport_.setContourMode(mode);
        });
    connect(removeSource_, &QToolButton::clicked, this, [this] {
        if (!syncing_)
            viewport_.removeSelectedSource();
    });
    contourControls_ << sourceSummary_ << roleBox_ << elevationBox_ << addContour_
                     << addBreakline_ << addSpot_ << promote_ << removeSource_;
    elevation_->installEventFilter(this);
    connect(elevation_, &QLineEdit::editingFinished, this, [this] { apply(elevation_); });
    connect(elevation_, &QLineEdit::textEdited, this, [this] {
        elevation_->setProperty("invalid", false);
        elevation_->style()->unpolish(elevation_);
        elevation_->style()->polish(elevation_);
    });
    role_->installEventFilter(this);
    for (auto *edit : {radius_, width_, amount_, strength_}) {
        edit->installEventFilter(this);
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] { apply(edit); });
        connect(edit, &QLineEdit::textEdited, this, [edit] {
            edit->setProperty("invalid", false);
            edit->style()->unpolish(edit);
            edit->style()->polish(edit);
        });
    }
    for (QWidget *control : std::initializer_list<QWidget *>{falloff_, passes_})
        control->installEventFilter(this);
    for (auto *button : tools_->buttons())
        button->installEventFilter(this);
    for (auto *button : scopes_->buttons())
        button->installEventFilter(this);
    connect(&viewport_, &Viewport::terrainToolChanged, this, &TerrainToolStrip::sync);
    hide();
}
QToolButton *TerrainToolStrip::toolButton(int tool) const {
    return qobject_cast<QToolButton *>(tools_->button(tool));
}
QToolButton *TerrainToolStrip::scopeButton(TerrainToolOptions::Scope scope) const {
    return qobject_cast<QToolButton *>(scopes_->button(int(scope)));
}
QToolButton *TerrainToolStrip::contourButton(const QString &name) const {
    for (auto *b : {addContour_, addBreakline_, addSpot_, promote_, removeSource_})
        if (b->text() == name)
            return b;
    return nullptr;
}
QWidget *TerrainToolStrip::firstField() const {
    return tools_->checkedButton() ? tools_->checkedButton() : tools_->buttons().front();
}
QString TerrainToolStrip::lengthText(double metres) const {
    const auto &doc = viewport_.document();
    return displayLength(metres, doc.displayUnits(), doc.displayPrecision());
}
// Elevations as the labels give them: two decimals unless the document asks for another precision.
QString TerrainToolStrip::elevationText(double metres) const {
    const auto &doc = viewport_.document();
    return displayLength(metres, doc.displayUnits(),
                         doc.displayPrecision() == fullDisplayPrecision ? 2 : doc.displayPrecision());
}
void TerrainToolStrip::setColors(const ThemeColors &colors) {
    setStyleSheet(QString("#terrainToolStrip {background:%1;border:1px solid %2;border-radius:6px;}"
                          "#terrainStripLabel, #terrainStripHint {color:%3;}"
                          "#terrainStripDivider {color:%2;}"
                          "#terrainToolSwitcher QToolButton, #terrainScope QToolButton "
                          "{padding:2px 8px;border:1px solid %2;background:%4;}"
                          "#terrainToolSwitcher QToolButton:checked, #terrainScope "
                          "QToolButton:checked {background:%5;color:%6;font-weight:bold;}"
                          "QToolButton[terrainStripButton=\"true\"] {padding:3px 9px;border:1px "
                          "solid %2;border-radius:4px;background:%4;}"
                          "QToolButton[terrainStripButton=\"true\"]:checked {background:%5;color:%6;"
                          "font-weight:bold;}"
                          "QToolButton[terrainStripButton=\"true\"]:disabled {color:%3;}"
                          "QLineEdit[invalid=\"true\"] {border:2px solid %7;}")
                      .arg(colors.surface.name(), colors.border.name(), colors.muted.name(),
                           colors.input.name(), colors.accent.name(),
                           colors.dark ? colors.surface.name() : QString("#ffffff"),
                           colors.failure.name()));
}
void TerrainToolStrip::sync() {
    const auto tool = viewport_.tool();
    if (!Viewport::terrainTool(tool)) {
        hide();
        return;
    }
    syncing_ = true;
    const auto &options = viewport_.terrainOptions();
    if (auto *button = tools_->button(int(tool)))
        button->setChecked(true);
    if (auto *button = scopes_->button(int(options.scope)))
        button->setChecked(true);
    const bool region = options.scope == TerrainToolOptions::Scope::Region;
    const bool sculpt = tool == Viewport::Tool::Sculpt;
    const bool brush = Viewport::sculptTool(tool);
    for (auto *control : contourControls_)
        control->setVisible(tool == Viewport::Tool::Contours);
    if (brush) {
        for (auto *control : sculptControls_)
            control->setVisible(true);
        radiusBox_->setVisible(!region);
        widthBox_->setVisible(region);
        amountBox_->setVisible(sculpt);
        strengthBox_->setVisible(!sculpt);
        passesBox_->setVisible(!sculpt);
        hint_->setVisible(sculpt);
    } else
        for (auto *control : sculptControls_)
            control->setVisible(false);
    if (tool == Viewport::Tool::Contours) {
        const auto view = viewport_.contourTool();
        const auto muted = palette().color(QPalette::PlaceholderText).name();
        sourceSummary_->setText(view.selected ? "<b>" + view.selectedName.toHtmlEscaped() +
                                                    "</b> <span style=\"color:" + muted + "\">· " +
                                                    view.selectedDetail.toHtmlEscaped() + "</span>"
                                              : "<span style=\"color:" + muted +
                                                    "\">No source selected</span>");
        sourceSummary_->setAccessibleName(view.selected ? "Selected source: " + view.selectedName +
                                                              ", " + view.selectedDetail
                                                        : QString("No source selected"));
        const bool line = view.selected && view.role != TerrainSourceRole::Points;
        role_->setEnabled(line && view.editable);
        if (line)
            role_->setCurrentIndex(role_->findData(int(view.role)));
        elevation_->setEnabled(line && view.editable);
        if (!elevation_->hasFocus() && !elevation_->property("invalid").toBool())
            elevation_->setText(view.elevation ? elevationText(*view.elevation) : QString());
        removeSource_->setEnabled(view.selected && view.editable);
        addContour_->setChecked(view.mode == Viewport::ContourMode::AddContour);
        addBreakline_->setChecked(view.mode == Viewport::ContourMode::AddBreakline);
        addSpot_->setChecked(view.mode == Viewport::ContourMode::AddSpot);
        promote_->setChecked(view.mode == Viewport::ContourMode::Promote);
    }
    falloff_->setCurrentIndex(falloff_->findData(int(options.falloff)));
    passes_->setValue(options.passes);
    auto fill = [](QLineEdit *edit, const QString &text) {
        if (!edit->hasFocus() && !edit->property("invalid").toBool())
            edit->setText(text);
    };
    fill(radius_, lengthText(options.radius));
    fill(width_, lengthText(options.falloffWidth));
    fill(amount_, (options.amount > 0 ? "+" : "") + lengthText(options.amount));
    fill(strength_, QLocale().toString(options.strength, 'f', 2));
    syncing_ = false;
    show();
    raise();
    place(0);
}
void TerrainToolStrip::place(int) {
    if (!isVisible())
        return;
    auto *view = parentWidget();
    int left = 16, top = 8;
    // Beside the breadcrumb where it fits (UX mockup), otherwise under it; under the component
    // banner while one is shown.
    const auto *crumb = view->findChild<QWidget *>("contextBreadcrumb", Qt::FindDirectChildrenOnly);
    const auto *banner =
        view->findChild<QWidget *>("componentScopeBanner", Qt::FindDirectChildrenOnly);
    if (crumb && crumb->isVisible()) {
        left = crumb->geometry().right() + 12;
        if (view->width() - left - 14 < 320) {
            left = 16;
            top = crumb->geometry().bottom() + 8;
        }
    }
    if (banner && banner->isVisible()) {
        left = 16;
        top = std::max(top, banner->geometry().bottom() + 8);
    }
    const int width = std::max(120, view->width() - left - 14);
    const int height = layout()->heightForWidth(width);
    setGeometry(left, top, width, height);
}
void TerrainToolStrip::apply(QLineEdit *edit) {
    if (syncing_ || !edit->isModified())
        return;
    auto options = viewport_.terrainOptions();
    const auto &doc = viewport_.document();
    const auto unit = inputUnit(doc.displayUnits());
    const auto locale = QLocale();
    QString error;
    if (edit == elevation_) {
        // R096.f2: the selected source's elevation, one "Edit contour" edit.
        try {
            const double value = parseLength(edit->text(), unit, locale);
            edit->setModified(false);
            viewport_.setSelectedSourceElevation(value);
        } catch (const std::exception &failure) {
            error = failure.what();
        }
        edit->setProperty("invalid", !error.isEmpty());
        edit->setAccessibleDescription(error);
        edit->style()->unpolish(edit);
        edit->style()->polish(edit);
        emit problem(error);
        if (error.isEmpty())
            sync();
        return;
    }
    try {
        if (edit == strength_) {
            bool ok = false;
            const double value = locale.toDouble(edit->text().trimmed(), &ok);
            if (!ok || !std::isfinite(value) || value < terrainToolStrengthMin || value > 1)
                throw std::runtime_error("The smoothing strength is 0.05 to 1");
            options.strength = value;
        } else {
            const double value = parseLength(edit->text(), unit, locale);
            if (edit == radius_) {
                if (value < terrainSculptRadiusMin || value > terrainSculptRadiusMax)
                    throw std::runtime_error("The brush radius is 0.01 to 1,000 m");
                options.radius = value;
            } else if (edit == width_) {
                if (value < 0 || value > terrainSculptWidthMax)
                    throw std::runtime_error("The falloff width is 0 to 1,000 m");
                options.falloffWidth = value;
            } else {
                if (value == 0 || std::abs(value) > terrainSculptAmountMax)
                    throw std::runtime_error("The amount is above 0 and at most 1,000 m either way "
                                             "(negative lowers)");
                options.amount = value;
            }
        }
    } catch (const std::exception &failure) {
        error = failure.what();
    }
    edit->setProperty("invalid", !error.isEmpty());
    edit->setAccessibleDescription(error);
    edit->style()->unpolish(edit);
    edit->style()->polish(edit);
    emit problem(error);
    if (!error.isEmpty())
        return;
    edit->setModified(false);
    viewport_.setTerrainOptions(options);
    // The applied value in document units (the field keeps focus).
    const auto &applied = viewport_.terrainOptions();
    if (edit == radius_)
        edit->setText(lengthText(applied.radius));
    else if (edit == width_)
        edit->setText(lengthText(applied.falloffWidth));
    else if (edit == amount_)
        edit->setText((applied.amount > 0 ? "+" : "") + lengthText(applied.amount));
    else
        edit->setText(QLocale().toString(applied.strength, 'f', 2));
    sync();
}
void TerrainToolStrip::keyPressEvent(QKeyEvent *event) {
    // A key a control ignored (Enter in Passes, [ on a button, digits) would otherwise travel to
    // the viewport, its parent, and act on the brush. The strip keeps the keyboard while it has
    // focus; Tab, F6 and shortcuts are handled before a key press is delivered.
    event->accept();
}
void TerrainToolStrip::keyReleaseEvent(QKeyEvent *event) {
    // Releases too: a Shift released in a field must not reach the viewport and replan.
    event->accept();
}
bool TerrainToolStrip::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            // Escape restores the field and gives the keyboard back to the viewport (UX §4.2).
            if (auto *edit = qobject_cast<QLineEdit *>(object)) {
                edit->setModified(false);
                edit->setProperty("invalid", false);
                edit->setAccessibleDescription({});
                edit->style()->unpolish(edit);
                edit->style()->polish(edit);
                emit problem({});
            }
            viewport_.setFocus(Qt::OtherFocusReason);
            syncing_ = false;
            sync();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            qobject_cast<QLineEdit *>(object)) {
            apply(static_cast<QLineEdit *>(object));
            return true;
        }
    }
    return QFrame::eventFilter(object, event);
}
} // namespace sketchy
