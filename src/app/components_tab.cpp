#include "app/components_tab.hpp"
#include "app/collapsible_section.hpp"
#include "app/parameter_value_field.hpp"
#include "automation/component_parameter_commands.hpp"
#include "io/library_catalog.hpp"
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <cmath>
namespace sketchy {
namespace {
constexpr int CardRole = Qt::UserRole, CaptionRole = Qt::UserRole + 1;
constexpr int cardPadding = 8, cardGap = 6;
QFont captionFont(QFont font) {
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, int(std::lround(font.pixelSize() * 11.0 / 12))));
    else
        font.setPointSizeF(font.pointSizeF() * 11 / 12);
    return font;
}
QFont titleFont(QFont font) {
    font.setBold(true);
    return font;
}
// A card: its name over "Cuts opening · 4 parameters", wrapped to the card's width. The
// current card is the design's pressed card: the selection fill and an accent border; with
// keyboard focus in the list its border is the ink colour.
class CardDelegate final : public QStyledItemDelegate {
  public:
    CardDelegate(const ThemeColors *colors, QObject *parent)
        : QStyledItemDelegate(parent), colors_(colors) {}
    static int height(const QFont &font, const QString &title, const QString &caption,
                      int width) {
        const auto inner = std::max(16, width - 2 * cardPadding - 2 * cardGap / 2);
        const QFontMetrics titleMetrics(titleFont(font)), captionMetrics(captionFont(font));
        const auto titleHeight =
            titleMetrics.boundingRect(QRect(0, 0, inner, 10000), Qt::TextWordWrap, title).height();
        const auto captionHeight =
            captionMetrics.boundingRect(QRect(0, 0, inner, 10000), Qt::TextWordWrap, caption)
                .height();
        return titleHeight + 2 + captionHeight + 2 * cardPadding + cardGap;
    }
    // Each card fills its grid cell.
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (const auto *view = qobject_cast<const QListView *>(option.widget);
            view && view->gridSize().isValid())
            return view->gridSize();
        return QStyledItemDelegate::sizeHint(option, index);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const auto &c = *colors_;
        const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
        const bool current = view && view->currentIndex() == index;
        const bool focused = current && view->hasFocus();
        const auto card = QRectF(option.rect).adjusted(cardGap / 2.0, cardGap / 2.0,
                                                       -cardGap / 2.0, -cardGap / 2.0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const auto width = current ? 2.0 : 1.0;
        painter->setPen(QPen(focused ? c.ink : current ? c.accent : c.muted, width));
        painter->setBrush(current ? c.selected : c.input);
        painter->drawRoundedRect(card.adjusted(width / 2, width / 2, -width / 2, -width / 2), 6,
                                 6);
        const auto inner = card.toRect().adjusted(cardPadding, cardPadding, -cardPadding,
                                                  -cardPadding);
        const auto title = index.data(Qt::DisplayRole).toString();
        const auto caption = index.data(CaptionRole).toString();
        painter->setPen(c.ink);
        painter->setFont(titleFont(option.font));
        const auto titleRect = painter->boundingRect(inner, Qt::TextWordWrap, title);
        painter->drawText(inner, Qt::TextWordWrap, title);
        // On the pressed card the caption is ink, as the design's darker caption: muted text
        // on the selection fill is below 4.5:1.
        painter->setPen(current ? c.ink : c.muted);
        painter->setFont(captionFont(option.font));
        painter->drawText(inner.adjusted(0, titleRect.height() + 2, 0, 0), Qt::TextWordWrap,
                          caption);
        painter->restore();
    }

  private:
    const ThemeColors *colors_;
};
Id rootPlacement(const Document &doc) {
    for (const auto &[id, body] : doc.bodies())
        if (!body->parent)
            return id;
    return 0;
}
} // namespace
ComponentsTab::ComponentsTab(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view), colors_(themeColors(false)) {
    setObjectName("componentsTab");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("componentsScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    outer->addWidget(scroll);
    auto *stack = new QWidget;
    stack->setObjectName("componentsStack");
    // The stack takes the Tray's width, as Entity info's does (ADR 0166 §8): rows and cards
    // wrap rather than scroll sideways.
    stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    scroll->setWidget(stack);
    auto *layout = new QVBoxLayout(stack);
    layout->setContentsMargins(0, 4, 0, 8);
    layout->setSpacing(6);

    // Components: Show and the cards.
    auto *library = new QWidget;
    auto *libraryLayout = new QVBoxLayout(library);
    libraryLayout->setContentsMargins(8, 4, 8, 4);
    libraryLayout->setSpacing(8);
    auto *showRow = new QHBoxLayout;
    auto *showLabel = new QLabel("Show");
    showLabel->setObjectName("componentsShowLabel");
    filter_ = new QComboBox;
    filter_->setObjectName("componentsFilter");
    filter_->addItem("Configurable assemblies");
    filter_->addItem("All components");
    filter_->setAccessibleName("Show");
    filter_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    filter_->setMinimumContentsLength(8);
    showLabel->setBuddy(filter_);
    showRow->addWidget(showLabel);
    showRow->addWidget(filter_, 1);
    libraryLayout->addLayout(showRow);
    model_ = new QStandardItemModel(this);
    cards_ = new QListView;
    cards_->setObjectName("componentCards");
    cards_->setAccessibleName("Components");
    cards_->setModel(model_);
    cards_->setViewMode(QListView::IconMode);
    cards_->setFlow(QListView::LeftToRight);
    cards_->setWrapping(true);
    cards_->setMovement(QListView::Static);
    cards_->setResizeMode(QListView::Adjust);
    cards_->setUniformItemSizes(true);
    cards_->setSelectionMode(QAbstractItemView::SingleSelection);
    cards_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cards_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cards_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cards_->setFrameShape(QFrame::NoFrame);
    cards_->setSpacing(0);
    cards_->setItemDelegate(new CardDelegate(&colors_, cards_));
    cards_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // The grid follows the list's width and font (a narrow Tray, 200 % text).
    cards_->installEventFilter(this);
    libraryLayout->addWidget(cards_);
    message_ = new QLabel;
    message_->setObjectName("componentsMessage");
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    message_->hide();
    libraryLayout->addWidget(message_);
    components_ = new CollapsibleSection("Components", library, true);
    components_->setObjectName("componentsSection");
    layout->addWidget(components_);

    // Values for this placement.
    values_ = new QWidget;
    values_->setObjectName("assemblyValues");
    auto *valuesLayout = new QVBoxLayout(values_);
    valuesLayout->setContentsMargins(8, 6, 8, 6);
    valuesLayout->setSpacing(8);
    valuesTitle_ = new QLabel;
    valuesTitle_->setObjectName("assemblyValuesTitle");
    valuesTitle_->setWordWrap(true);
    valuesTitle_->setTextFormat(Qt::PlainText);
    valuesLayout->addWidget(valuesTitle_);
    rows_ = new QWidget;
    rowsLayout_ = new QVBoxLayout(rows_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(6);
    valuesLayout->addWidget(rows_);
    valuesNote_ = new QLabel("These become this placement's values. The library copy keeps its "
                             "defaults. Change them later in Entity info.");
    valuesNote_->setObjectName("assemblyValuesNote");
    valuesNote_->setWordWrap(true);
    valuesLayout->addWidget(valuesNote_);
    auto *buttons = new QHBoxLayout;
    useDefaults_ = new QPushButton("Use defaults");
    useDefaults_->setObjectName("assemblyUseDefaults");
    useDefaults_->setAutoDefault(false);
    cancel_ = new QPushButton;
    cancel_->setObjectName("assemblyCancel");
    cancel_->setAutoDefault(false);
    cancel_->setAccessibleName("Cancel");
    cancel_->setAccessibleDescription("Esc");
    cancel_->setToolTip("End placing (Esc)");
    // "Cancel" with its key, as the design's button shows it.
    auto *cancelLayout = new QHBoxLayout(cancel_);
    cancelLayout->setContentsMargins(11, 4, 11, 4);
    cancelLayout->setSpacing(6);
    auto *cancelText = new QLabel("Cancel");
    auto *cancelKey = new QLabel("Esc");
    cancelKey->setObjectName("assemblyCancelKey");
    for (auto *label : {cancelText, cancelKey}) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setFocusPolicy(Qt::NoFocus);
        cancelLayout->addWidget(label);
    }
    cancel_->setMinimumSize(cancelLayout->sizeHint());
    buttons->addWidget(useDefaults_);
    buttons->addWidget(cancel_);
    buttons->addStretch();
    valuesLayout->addLayout(buttons);
    values_->hide();
    layout->addWidget(values_);

    // About these assemblies.
    auto *about = new QWidget;
    about->setObjectName("assembliesAbout");
    auto *aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(8, 6, 8, 6);
    aboutLayout->setSpacing(6);
    auto *aboutTitle = new QLabel("About these assemblies");
    aboutTitle->setObjectName("assembliesAboutTitle");
    auto *aboutText = new QLabel("Bundled with SketchyUp and saved into your model when placed. "
                                 "SketchUp Dynamic Components are not imported or converted.");
    aboutText->setObjectName("assembliesAboutText");
    aboutText->setWordWrap(true);
    aboutLayout->addWidget(aboutTitle);
    aboutLayout->addWidget(aboutText);
    layout->addWidget(about);
    layout->addStretch(1);

    validation_ = new QTimer(this);
    validation_->setSingleShot(true);
    validation_->setInterval(150);
    connect(validation_, &QTimer::timeout, this, [this] {
        if (auto *field = pending_.data()) {
            pending_ = nullptr;
            validate(field);
        }
    });
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] {
        if (!detached_)
            refresh();
    });
    connect(cards_, &QListView::activated, this, [this](const QModelIndex &index) {
        start(index.data(CardRole).toInt());
    });
    connect(cards_, &QListView::clicked, this, [this](const QModelIndex &index) {
        start(index.data(CardRole).toInt());
    });
    connect(useDefaults_, &QPushButton::clicked, this, [this] { useDefaults(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { cancelPlacement(); });
    connect(&view_, &Viewport::assemblyPlacementEnded, this,
            [this](bool placed, qulonglong placement) { ended(placed, placement); });
    setFocusProxy(filter_);
    cards_->setDragEnabled(false);
    // Header, Show, the cards, then the form's fields and buttons (buildForm).
    setTabOrder(components_->header(), filter_);
    setTabOrder(filter_, cards_);
}
QString ComponentsTab::assemblyFolder() {
    const auto base = QCoreApplication::applicationDirPath();
    for (const auto &candidate : {base + "/../share/sketchyup/assemblies",
                                  base + "/share/sketchyup/assemblies"})
        if (QFileInfo(candidate).isDir())
            return QDir::cleanPath(candidate);
    return QDir::cleanPath(base + "/../share/sketchyup/assemblies");
}
void ComponentsTab::setTheme(const ThemeColors &colors) {
    colors_ = colors;
    cards_->viewport()->update();
}
ParameterValueField *ComponentsTab::field(const QString &name) const {
    for (auto *field : fields_)
        if (field->name() == name)
            return field;
    return nullptr;
}
void ComponentsTab::detach() {
    detached_ = true;
    validation_->stop();
    disconnect(&view_, nullptr, this, nullptr);
}
void ComponentsTab::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refresh();
}
void ComponentsTab::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutCards();
}
bool ComponentsTab::eventFilter(QObject *object, QEvent *event) {
    if (object == cards_ && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange))
        QTimer::singleShot(0, this, [this] {
            if (!detached_)
                layoutCards();
        });
    return QWidget::eventFilter(object, event);
}
void ComponentsTab::scan() {
    entries_.clear();
    QStringList notices;
    notice_.clear();
    auto add = [&](const QString &folder, bool assemblies) {
        LibraryCatalog catalog;
        try {
            catalog = scanLibraryDirectory(folder);
        } catch (const std::exception &error) {
            // A missing or unreadable folder adds no cards; the reason shows under them.
            notice_ = QDir::toNativeSeparators(folder) + ": " + QString::fromUtf8(error.what());
            return;
        }
        for (const auto &entry : catalog.entries) {
            if (entry.kind != LibraryKind::Component || !entry.error.isEmpty())
                continue;
            Card card;
            card.path = entry.path;
            card.name = entry.metadata.name;
            if (assemblies) {
                // The shipped assemblies are small: decode them now for their parameters.
                try {
                    auto bundle = std::make_shared<ComponentBundle>(loadComponentBundle(entry.path));
                    const auto &definition =
                        *bundle->document.definitions().at(bundle->definition);
                    if (!definition.parameters)
                        continue;
                    card.assembly = true;
                    card.cuts = definition.glue && definition.glue->cutsOpening;
                    card.parameters = int(definition.parameters->parameters.size());
                    card.identity = definition.parameters->assembly;
                    card.bundle = std::move(bundle);
                } catch (const std::exception &) {
                    continue;
                }
            }
            entries_.push_back(std::move(card));
        }
    };
    add(assemblyFolder(), true);
    // The shipped assemblies in the design's order; others by name after them.
    static const QStringList order{"sketchyup/interior-door", "sketchyup/casement-window",
                                   "sketchyup/dining-table",  "sketchyup/bookshelf",
                                   "sketchyup/base-cabinet",  "sketchyup/straight-stair"};
    auto rank = [](const Card &card) {
        const auto index =
            card.identity ? order.indexOf(QString::fromStdString(card.identity->id)) : -1;
        return index < 0 ? int(order.size()) : int(index);
    };
    std::stable_sort(entries_.begin(), entries_.end(),
                     [&](const Card &a, const Card &b) { return rank(a) < rank(b); });
    if (filter_->currentIndex() == 1)
        add(QSettings("SketchyUp", "SketchyUp")
                .value("libraryFolder",
                       QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           "/library")
                .toString(),
            false);
    scanned_ = filter_->currentIndex();
}
void ComponentsTab::refresh() {
    if (detached_)
        return;
    if (scanned_ != filter_->currentIndex()) {
        if (view_.assemblyPlacing())
            cancelPlacement();
        active_ = -1;
        scan();
    }
    showCards();
}
void ComponentsTab::showCards() {
    const auto current = cards_->currentIndex().isValid()
                             ? cards_->currentIndex().data(CardRole).toInt()
                             : active_;
    // The same cards update in place, so the current card and keyboard focus stay.
    const bool same = model_->rowCount() == int(entries_.size());
    if (!same)
        model_->clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto &card = entries_[i];
        QStringList caption, spoken{card.name};
        if (card.assembly) {
            if (card.cuts) {
                caption << "Cuts opening";
                spoken << "cuts opening";
            }
            const auto count = QString("%1 parameter%2")
                                   .arg(card.parameters)
                                   .arg(card.parameters == 1 ? "" : "s");
            caption << count;
            spoken << count;
            if (card.identity && findLibraryAssembly(doc_, *card.identity)) {
                caption << "In this model";
                spoken << "in this model";
            }
        } else {
            caption << "Component";
            spoken << "component";
        }
        auto *item = same ? model_->item(int(i)) : new QStandardItem;
        item->setText(card.name);
        item->setData(int(i), CardRole);
        item->setData(caption.join(" · "), CaptionRole);
        item->setData(spoken.join(", "), Qt::AccessibleTextRole);
        item->setData(caption.join(" · "), Qt::ToolTipRole);
        item->setEditable(false);
        if (!same)
            model_->appendRow(item);
    }
    message_->setVisible(entries_.empty() || !notice_.isEmpty());
    message_->setText(entries_.empty() ? "No components were found in " +
                                             QDir::toNativeSeparators(assemblyFolder()) + "."
                                       : notice_);
    if (!same) {
        if (current >= 0 && current < model_->rowCount())
            cards_->setCurrentIndex(model_->index(current, 0));
        else if (model_->rowCount())
            cards_->setCurrentIndex(model_->index(0, 0));
    }
    layoutCards();
}
void ComponentsTab::layoutCards() {
    // Two columns across the list's viewport; the icon-mode layout needs a pixel to spare.
    const int width = std::max(64, cards_->viewport()->width() - 2);
    const int column = width / 2;
    int tallest = 0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const auto index = model_->index(row, 0);
        tallest = std::max(tallest, CardDelegate::height(cards_->font(),
                                                         index.data(Qt::DisplayRole).toString(),
                                                         index.data(CaptionRole).toString(),
                                                         column));
    }
    tallest = std::max(tallest, 24);
    cards_->setGridSize({column, tallest});
    const int rows = (model_->rowCount() + 1) / 2;
    const int height = std::max(1, rows) * tallest + 2;
    if (cards_->height() != height)
        cards_->setFixedHeight(height);
    cards_->doItemsLayout();
}
void ComponentsTab::start(int index) {
    if (detached_ || index < 0 || index >= int(entries_.size()))
        return;
    auto &card = entries_[size_t(index)];
    if (index == active_ && view_.assemblyPlacing())
        return;
    try {
        if (!card.bundle)
            card.bundle = std::make_shared<ComponentBundle>(loadComponentBundle(card.path));
    } catch (const std::exception &error) {
        message_->setText(QString::fromUtf8(error.what()));
        message_->show();
        return;
    }
    message_->hide();
    {
        // Starting replaces any placement in progress without ending this one's form.
        const QScopedValueRollback guard(starting_, true);
        active_ = index;
        buildForm(card);
        try {
            view_.beginAssemblyPlacement(card.bundle, overrides());
        } catch (const std::exception &error) {
            active_ = -1;
            values_->hide();
            message_->setText(QString::fromUtf8(error.what()));
            message_->show();
            return;
        }
    }
    values_->show();
    cards_->viewport()->update();
}
void ComponentsTab::buildForm(const Card &card) {
    validation_->stop();
    pending_ = nullptr;
    fields_.clear();
    while (auto *item = rowsLayout_->takeAt(0)) {
        if (auto *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    valuesTitle_->setText(card.name + " · values for this placement");
    const auto &bundle = *card.bundle;
    if (!bundle.document.definitions().at(bundle.definition)->parameters) {
        relayoutAncestors(rows_);
        return; // a component without parameters has no values to choose
    }
    const auto description = componentParametersDescription(bundle.document, bundle.definition);
    const auto values =
        componentParameterValues(bundle.document, rootPlacement(bundle.document))["values"]
            .toArray();
    auto ordered = description["parameters"].toArray().toVariantList();
    std::stable_sort(ordered.begin(), ordered.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["position"].toInt() < b.toMap()["position"].toInt();
    });
    QWidget *previous = cards_;
    for (const auto &entry : QJsonArray::fromVariantList(ordered)) {
        const auto parameter = entry.toObject();
        if (parameter["mode"] != "input")
            continue;
        auto *field = new ParameterValueField(parameter);
        QJsonObject stored;
        for (const auto &value : values)
            if (value.toObject()["name"] == parameter["name"])
                stored = value.toObject();
        // The defaults in this model's units.
        if (field->quantity())
            stored["display"] = parameterValueText(stored["value"].toDouble(), field->type(),
                                                   doc_.displayUnits(), doc_.displayPrecision());
        field->setStored(stored, "default", {}, doc_.displayUnits(), doc_.displayPrecision(),
                         false);
        field->resetButton()->hide();
        rowsLayout_->addWidget(field);
        fields_.push_back(field);
        connect(field, &ParameterValueField::textEdited, this, [this, field] {
            try {
                field->value();
                field->clearError();
                pending_ = field;
                validation_->start();
            } catch (const std::exception &error) {
                pending_ = nullptr;
                validation_->stop();
                field->setError(QString::fromUtf8(error.what()));
            }
        });
        connect(field, &ParameterValueField::commitRequested, this, [this, field] {
            if (validate(field).isEmpty())
                valuesChanged();
        });
        QWidget::setTabOrder(previous, field->control());
        previous = field->control();
    }
    QWidget::setTabOrder(previous, useDefaults_);
    QWidget::setTabOrder(useDefaults_, cancel_);
    relayoutAncestors(rows_);
}
QJsonObject ComponentsTab::overrides() const {
    QJsonObject result;
    for (auto *field : fields_) {
        if (!field->edited() || !field->error().isEmpty())
            continue;
        try {
            const auto value = field->value();
            if (!field->equalsStored(value))
                result[field->name()] = value;
        } catch (const std::exception &) {
        }
    }
    return result;
}
QJsonObject ComponentsTab::validate(ParameterValueField *field) {
    if (detached_ || active_ < 0 || !field)
        return {};
    QJsonValue value;
    try {
        value = field->value();
    } catch (const std::exception &error) {
        field->setError(QString::fromUtf8(error.what()));
        return {{"message", QString::fromUtf8(error.what())}};
    }
    auto values = overrides();
    if (field->equalsStored(value))
        values.remove(field->name());
    else
        values[field->name()] = value;
    const auto &bundle = *entries_[size_t(active_)].bundle;
    const auto result =
        componentParametersEvaluation(bundle.document, bundle.definition, {{"overrides", values}});
    if (result["status"].toString() == "error") {
        const auto error = result["error"].toObject();
        field->setError(parameterErrorSentence(error, field->text(), field->parameter(),
                                               doc_.displayUnits(), doc_.displayPrecision(),
                                               value),
                        error["message"].toString());
        return error;
    }
    field->clearError();
    return {};
}
void ComponentsTab::valuesChanged() {
    if (detached_ || !view_.assemblyPlacing())
        return;
    const auto refusal = view_.setAssemblyValues(overrides());
    if (!refusal.isEmpty()) {
        message_->setText(refusal["message"].toString());
        message_->show();
    } else
        message_->hide();
}
void ComponentsTab::useDefaults() {
    validation_->stop();
    pending_ = nullptr;
    for (auto *field : fields_) {
        field->setEdited(false);
        field->restore();
    }
    valuesChanged();
}
void ComponentsTab::cancelPlacement() {
    validation_->stop();
    if (view_.assemblyPlacing())
        view_.endAssemblyPlacement(); // ended() hides the form
    else
        ended(false, 0);
}
void ComponentsTab::ended(bool placed, qulonglong placement) {
    if (detached_ || starting_)
        return;
    validation_->stop();
    pending_ = nullptr;
    active_ = -1;
    const bool hadFocus = values_->isAncestorOf(QApplication::focusWidget());
    values_->hide();
    if (hadFocus)
        cards_->setFocus(Qt::OtherFocusReason);
    showCards();
    if (placed)
        emit this->placed(placement);
}
void ComponentsTab::keyPressEvent(QKeyEvent *event) {
    // Cancel (Esc) ends placement from anywhere in the tab (ADR 0166 §6.3).
    if (event->key() == Qt::Key_Escape && view_.assemblyPlacing()) {
        event->accept();
        cancelPlacement();
        return;
    }
    QWidget::keyPressEvent(event);
}
} // namespace sketchy
